#include <jni.h>
#include <jsi/jsi.h>
#include <android/log.h>
#include <memory>
#include <array>
#include <vector>
#include <cstring>
#include "engine/InferencePipeline.h"
#include "preprocessing/FrameMailbox.h"
#include "liveness/LivenessFSM.h"

#undef LOG_TAG
#define LOG_TAG "FaceAuthPlugin"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace facebook::jsi;
using namespace drishti;

class VectorMutableBuffer final : public MutableBuffer {
public:
    explicit VectorMutableBuffer(size_t size) : bytes_(size) {}
    size_t size() const override { return bytes_.size(); }
    uint8_t* data() override { return bytes_.data(); }

private:
    std::vector<uint8_t> bytes_;
};

// ─── Real state ──────────────────────────────────────────────────────────────
static bool sEngineRunning = false;
static std::shared_ptr<FrameMailbox> gMailbox;
static std::shared_ptr<LivenessFSM> gFSM;
static std::unique_ptr<InferencePipeline> gPipeline;
static std::shared_ptr<LSHIndex> gLshIndex;

// ─── Helper: install all 8 JSI global functions ──────────────────────────────
static void installFaceAuth(Runtime& runtime, const std::string& faceMeshPath, const std::string& mobileFaceNetPath) {
    LOGI("installFaceAuth: registering 8 JSI global functions");
    if (!gLshIndex) {
        gLshIndex = std::make_shared<LSHIndex>();
        gLshIndex->isReady = true;
    }

    // 1) startFaceAuthEngine() -> boolean
    auto startFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "startFaceAuthEngine"),
        0,
        [faceMeshPath, mobileFaceNetPath](Runtime& rt, const Value&, const Value*, size_t) -> Value {
            if (sEngineRunning) return Value(true);
            gMailbox = std::make_shared<FrameMailbox>();
            gFSM = std::make_shared<LivenessFSM>();
            if (!gLshIndex) {
                gLshIndex = std::make_shared<LSHIndex>();
                gLshIndex->isReady = true;
            }
            gPipeline = std::make_unique<InferencePipeline>(gMailbox, gFSM, gLshIndex, faceMeshPath, mobileFaceNetPath);
            gPipeline->start();
            sEngineRunning = true;
            LOGI("startFaceAuthEngine called");
            return Value(true);
        });
    runtime.global().setProperty(runtime, "startFaceAuthEngine", std::move(startFn));

    // 2) stopFaceAuthEngine() -> boolean
    auto stopFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "stopFaceAuthEngine"),
        0,
        [](Runtime& rt, const Value&, const Value*, size_t) -> Value {
            sEngineRunning = false;
            if (gPipeline) {
                gPipeline->stop();
                gPipeline.reset();
            }
            gMailbox.reset();
            gFSM.reset();
            LOGI("stopFaceAuthEngine called");
            return Value(true);
        });
    runtime.global().setProperty(runtime, "stopFaceAuthEngine", std::move(stopFn));

    // 3) processVisionFrame(frameBuffer, width, height, stride, rotation) -> boolean
    auto processFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "processVisionFrame"),
        5,
        [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (!sEngineRunning || !gMailbox || count < 5) return Value(false);
            
            if (args[0].isObject() && args[0].getObject(rt).isArrayBuffer(rt)) {
                auto arrayBuffer = args[0].getObject(rt).getArrayBuffer(rt);
                uint8_t* data = arrayBuffer.data(rt);
                
                int width = args[1].asNumber();
                int height = args[2].asNumber();
                int stride = args[3].asNumber();
                int rotation = args[4].asNumber();
                
                // Post to mailbox. Mailbox will copy the buffer internally.
                gMailbox->post(data, width, height, stride, 0, rotation);
            }
            return Value(true);
        });
    runtime.global().setProperty(runtime, "processVisionFrame", std::move(processFn));

    // 4) getFaceAuthResult() -> FaceAuthResultJS object
    auto getResultFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "getFaceAuthResult"),
        0,
        [](Runtime& rt, const Value&, const Value*, size_t) -> Value {
            auto result = Object(rt);
            
            int state = 0; // IDLE
            int challenge = 0; // NONE
            bool ready = false;
            
            if (gFSM) {
                state = static_cast<int>(gFSM->getCurrentState());
                challenge = static_cast<int>(gFSM->getActiveChallenge());
            }

            std::string matchedId = "";
            float matchScore = 0.0f;
            float inferenceMs = 0.0f;
            float confidence = 0.0f;
            float ear = 0.0f;
            float mar = 0.0f;
            float yaw = 0.0f;
            float pitch = 0.0f;
            float tempVariance = 0.0f;
            uint32_t frameCount = 0;
            int32_t nativeFps = 0;
            if (gPipeline) {
                matchedId = gPipeline->getLastMatchedId();
                matchScore = gPipeline->getLastMatchScore();
                inferenceMs = gPipeline->getLastInferenceMs();
                confidence = gPipeline->getLastConfidence();
                frameCount = gPipeline->getFrameCount();
                nativeFps = gPipeline->getNativeFps();
                ready = gPipeline->isEmbeddingReady();
            }
            if (gFSM) {
                ear = gFSM->getLastEar();
                mar = gFSM->getLastMar();
                yaw = gFSM->getLastYaw();
                pitch = gFSM->getLastPitch();
                tempVariance = gFSM->getLastTempVariance();
            }

            result.setProperty(rt, "livenessState",   state);
            result.setProperty(rt, "activeChallenge", challenge);
            result.setProperty(rt, "matchScore",       matchScore);
            result.setProperty(rt, "matchedId",        String::createFromAscii(rt, matchedId.c_str()));
            result.setProperty(rt, "embeddingReady",   ready);
            if (ready && gPipeline) {
                std::array<float, 128> embedding{};
                if (gPipeline->copyLastEmbedding(embedding)) {
                    auto mutableBuffer = std::make_shared<VectorMutableBuffer>(128 * sizeof(float));
                    std::memcpy(mutableBuffer->data(), embedding.data(), 128 * sizeof(float));
                    auto embeddingBuffer = ArrayBuffer(rt, mutableBuffer);
                    result.setProperty(rt, "embeddingBytes", std::move(embeddingBuffer));
                }
            }
            result.setProperty(rt, "inferenceMs",      inferenceMs);
            result.setProperty(rt, "ear",              ear);
            result.setProperty(rt, "mar",              mar);
            result.setProperty(rt, "yaw",              yaw);
            result.setProperty(rt, "pitch",            pitch);
            result.setProperty(rt, "tempVariance",     tempVariance);
            result.setProperty(rt, "faceConfidence",   confidence);
            result.setProperty(rt, "frameCount",       static_cast<double>(frameCount));
            result.setProperty(rt, "nativeFps",        static_cast<double>(nativeFps));
            result.setProperty(rt, "nativeHeapKB",     12450.0);
            result.setProperty(rt, "errorCode",        String::createFromAscii(rt, ""));
            return Value(rt, std::move(result));
        });
    runtime.global().setProperty(runtime, "getFaceAuthResult", std::move(getResultFn));

    // 5) resetFaceAuthEngine() -> boolean
    auto resetFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "resetFaceAuthEngine"),
        0,
        [](Runtime& rt, const Value&, const Value*, size_t) -> Value {
            if (gFSM) gFSM->reset();
            LOGI("resetFaceAuthEngine called");
            return Value(true);
        });
    runtime.global().setProperty(runtime, "resetFaceAuthEngine", std::move(resetFn));

    // 6) insertFaceProfile(profileId, embedding) -> boolean
    auto insertFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "insertFaceProfile"),
        2,
        [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) return Value(false);
            auto profileId = args[0].asString(rt).utf8(rt);
            
            if (args[1].isObject() && args[1].getObject(rt).isArrayBuffer(rt)) {
                auto arrayBuffer = args[1].getObject(rt).getArrayBuffer(rt);
                float* data = reinterpret_cast<float*>(arrayBuffer.data(rt));
                
                if (!gLshIndex) {
                    gLshIndex = std::make_shared<LSHIndex>();
                    gLshIndex->isReady = true;
                }
                LSHIndex::Candidate c;
                c.personnelId = profileId;
                std::memcpy(c.embedding, data, 128 * sizeof(float));
                gLshIndex->insert(c);
                LOGI("insertFaceProfile: %s success", profileId.c_str());
                return Value(true);
            }
            return Value(false);
        });
    runtime.global().setProperty(runtime, "insertFaceProfile", std::move(insertFn));

    // 7) removeFaceProfile(profileId) -> boolean
    auto removeFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "removeFaceProfile"),
        1,
        [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 1) return Value(false);
            auto profileId = args[0].asString(rt).utf8(rt);
            if (gLshIndex) {
                gLshIndex->remove(profileId);
            }
            LOGI("removeFaceProfile: %s", profileId.c_str());
            return Value(true);
        });
    runtime.global().setProperty(runtime, "removeFaceProfile", std::move(removeFn));

    // 8) getFaceAuthStatus() -> FaceAuthStatusJS object
    auto statusFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "getFaceAuthStatus"),
        0,
        [](Runtime& rt, const Value&, const Value*, size_t) -> Value {
            auto status = Object(rt);
            status.setProperty(rt, "engineRunning",    sEngineRunning);
            status.setProperty(rt, "indexReady",       gLshIndex != nullptr && gLshIndex->isReady);
            status.setProperty(rt, "profileCount",     0);
            status.setProperty(rt, "mailboxHasFrame",  gMailbox != nullptr && gMailbox->hasFrame());
            return Value(rt, std::move(status));
        });
    runtime.global().setProperty(runtime, "getFaceAuthStatus", std::move(statusFn));

    LOGI("installFaceAuth: all 8 JSI functions registered successfully");
}

// ─── JNI entry points ────────────────────────────────────────────────────────

extern "C" JNIEXPORT void JNICALL
Java_com_drishti_FaceAuthModule_nativeInstall(
    JNIEnv *env,
    jclass  clazz,
    jlong   jsiPtr,
    jobject callInvokerHolder,
    jstring faceMeshPath,
    jstring mobileFaceNetPath
) {
    LOGI("nativeInstall called with JSI ptr: %lld", (long long)jsiPtr);
    auto runtime = reinterpret_cast<Runtime*>(jsiPtr);
    
    const char* cPathFM = env->GetStringUTFChars(faceMeshPath, nullptr);
    std::string pathStrFM(cPathFM);
    env->ReleaseStringUTFChars(faceMeshPath, cPathFM);
    
    const char* cPathMN = env->GetStringUTFChars(mobileFaceNetPath, nullptr);
    std::string pathStrMN(cPathMN);
    env->ReleaseStringUTFChars(mobileFaceNetPath, cPathMN);
    
    installFaceAuth(*runtime, pathStrFM, pathStrMN);
}

extern "C" JNIEXPORT void JNICALL
Java_com_drishti_FaceAuthModule_nativeCleanup(
    JNIEnv *env,
    jclass  clazz
) {
    LOGI("nativeCleanup called");
    sEngineRunning = false;
    if (gPipeline) {
        gPipeline->stop();
        gPipeline.reset();
    }
    gMailbox.reset();
    gFSM.reset();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_drishti_FaceAuthModule_nativeProcessFrame(
    JNIEnv *env,
    jclass  clazz,
    jobject byteBuffer,
    jint    width,
    jint    height,
    jint    stride,
    jint    rotation
) {
    if (!sEngineRunning || !gMailbox) return JNI_FALSE;
    
    uint8_t* data = (uint8_t*)env->GetDirectBufferAddress(byteBuffer);
    if (data) {
        return gMailbox->post(data, width, height, stride, 0, rotation) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}
