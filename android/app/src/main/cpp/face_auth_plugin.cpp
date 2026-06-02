#include <jni.h>
#include <android/log.h>
#include <fbjni/fbjni.h>
#include <jsi/jsi.h>
#include <ReactCommon/CallInvokerHolder.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

#include "FaceAuthResult.h"
#include "preprocessing/AdaptiveClahe.h"
#include "preprocessing/FrameMailbox.h"
#include "liveness/LivenessFSM.h"
#include "matching/LSHIndex.h"
#include "matching/CosineSimilarity.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "FaceAuthPlugin"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::unique_ptr<FrameMailbox> g_mailbox;
static std::unique_ptr<AdaptiveClahe> g_clahe;
static std::unique_ptr<LivenessFSM> g_fsm;
static std::unique_ptr<LSHIndex> g_lshIndex;
static FaceAuthResult g_latestResult;

static std::atomic<bool> g_engineRunning{false};
static std::thread g_inferenceThread;

static facebook::jsi::Runtime* g_runtime = nullptr;
static std::shared_ptr<facebook::react::CallInvoker> g_callInvoker;

static int64_t nowMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch())
        .count();
}

static void inferenceLoop() {
    LOGD("Inference thread started");

    int64_t lastFpsTime = nowMs();
    int fpsCounter = 0;

    while (g_engineRunning.load(std::memory_order_acquire)) {
        FrameData frame;
        if (!g_mailbox->fetch(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        if (!frame.valid || !frame.yPlane) {
            continue;
        }

        auto inferStart = std::chrono::steady_clock::now();

        g_clahe->process(frame.yPlane, frame.width, frame.height, frame.stride);

        g_fsm->processFrame(nullptr, 0, 0.0f,
                            frame.yPlane, frame.width, frame.height,
                            g_latestResult);

        auto inferEnd = std::chrono::steady_clock::now();
        float inferMs = std::chrono::duration<float, std::milli>(
                            inferEnd - inferStart)
                            .count();
        g_latestResult.inferenceMs = inferMs;

        fpsCounter++;
        int64_t now = nowMs();
        int64_t elapsed = now - lastFpsTime;
        if (elapsed >= 1000) {
            g_latestResult.nativeFps = static_cast<int32_t>(
                (fpsCounter * 1000) / elapsed);
            fpsCounter = 0;
            lastFpsTime = now;
        }
    }

    LOGD("Inference thread stopped");
}

static void installFaceAuth(facebook::jsi::Runtime& runtime,
                             std::shared_ptr<facebook::react::CallInvoker> callInvoker) {
    g_runtime = &runtime;
    g_callInvoker = callInvoker;

    g_mailbox = std::make_unique<FrameMailbox>();
    g_clahe = std::make_unique<AdaptiveClahe>();
    g_fsm = std::make_unique<LivenessFSM>();
    g_lshIndex = std::make_unique<LSHIndex>();

    LOGD("FaceAuth engine components initialized");

    auto startEngine = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "startFaceAuthEngine"),
        0,
        [](facebook::jsi::Runtime& rt,
           const facebook::jsi::Value& thisVal,
           const facebook::jsi::Value* args,
           size_t count) -> facebook::jsi::Value {
            if (g_engineRunning.load(std::memory_order_acquire)) {
                return facebook::jsi::Value(false);
            }

            g_fsm->reset();
            g_clahe->reset();
            g_mailbox->drain();
            g_latestResult = FaceAuthResult{};

            g_engineRunning.store(true, std::memory_order_release);
            g_inferenceThread = std::thread(inferenceLoop);

            LOGD("Engine started");
            return facebook::jsi::Value(true);
        });

    auto stopEngine = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "stopFaceAuthEngine"),
        0,
        [](facebook::jsi::Runtime& rt,
           const facebook::jsi::Value& thisVal,
           const facebook::jsi::Value* args,
           size_t count) -> facebook::jsi::Value {
            if (!g_engineRunning.load(std::memory_order_acquire)) {
                return facebook::jsi::Value(false);
            }

            g_engineRunning.store(false, std::memory_order_release);

            if (g_inferenceThread.joinable()) {
                g_inferenceThread.join();
            }

            g_mailbox->drain();

            LOGD("Engine stopped");
            return facebook::jsi::Value(true);
        });

    auto processFrame = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "processVisionFrame"),
        4,
        [](facebook::jsi::Runtime& rt,
           const facebook::jsi::Value& thisVal,
           const facebook::jsi::Value* args,
           size_t count) -> facebook::jsi::Value {
            if (count < 4) {
                return facebook::jsi::Value(false);
            }

            if (!g_engineRunning.load(std::memory_order_acquire)) {
                return facebook::jsi::Value(false);
            }

            if (!args[0].isObject()) {
                return facebook::jsi::Value(false);
            }

            auto frameObj = args[0].asObject(rt);

            if (!frameObj.isArrayBuffer(rt)) {
                return facebook::jsi::Value(false);
            }

            auto arrayBuffer = frameObj.getArrayBuffer(rt);
            uint8_t* yPlaneData = arrayBuffer.data(rt);
            size_t bufferSize = arrayBuffer.size(rt);

            int width = static_cast<int>(args[1].asNumber());
            int height = static_cast<int>(args[2].asNumber());
            int stride = static_cast<int>(args[3].asNumber());

            if (width <= 0 || height <= 0 || stride <= 0) {
                return facebook::jsi::Value(false);
            }

            int requiredSize = stride * height;
            if (static_cast<int>(bufferSize) < requiredSize) {
                return facebook::jsi::Value(false);
            }

            int64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();

            bool posted = g_mailbox->post(yPlaneData, width, height, stride,
                                          timestamp, 0);

            return facebook::jsi::Value(posted);
        });

    auto getResult = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "getFaceAuthResult"),
        0,
        [](facebook::jsi::Runtime& rt,
           const facebook::jsi::Value& thisVal,
           const facebook::jsi::Value* args,
           size_t count) -> facebook::jsi::Value {
            auto result = facebook::jsi::Object(rt);

            result.setProperty(rt, "livenessState",
                               static_cast<int>(g_latestResult.livenessState));
            result.setProperty(rt, "activeChallenge",
                               static_cast<int>(g_latestResult.activeChallenge));
            result.setProperty(rt, "matchScore",
                               static_cast<double>(g_latestResult.matchScore));
            result.setProperty(rt, "matchedId",
                               facebook::jsi::String::createFromUtf8(
                                   rt, std::string(g_latestResult.matchedId)));
            result.setProperty(rt, "embeddingReady",
                               g_latestResult.embeddingReady);
            result.setProperty(rt, "inferenceMs",
                               static_cast<double>(g_latestResult.inferenceMs));
            result.setProperty(rt, "ear",
                               static_cast<double>(g_latestResult.ear));
            result.setProperty(rt, "mar",
                               static_cast<double>(g_latestResult.mar));
            result.setProperty(rt, "yaw",
                               static_cast<double>(g_latestResult.yaw));
            result.setProperty(rt, "pitch",
                               static_cast<double>(g_latestResult.pitch));
            result.setProperty(rt, "tempVariance",
                               static_cast<double>(g_latestResult.tempVariance));
            result.setProperty(rt, "frameCount",
                               static_cast<int>(g_latestResult.frameCount));
            result.setProperty(rt, "nativeFps",
                               static_cast<int>(g_latestResult.nativeFps));
            result.setProperty(rt, "nativeHeapKB",
                               static_cast<int>(g_latestResult.nativeHeapKB));
            result.setProperty(rt, "errorCode",
                               facebook::jsi::String::createFromUtf8(
                                   rt, std::string(g_latestResult.errorCode)));

            if (g_latestResult.embeddingReady) {
                struct AllocBuffer : public facebook::jsi::MutableBuffer {
                    std::vector<uint8_t> buffer_;
                    AllocBuffer(size_t size) : buffer_(size) {}
                    ~AllocBuffer() override = default;
                    size_t size() const override { return buffer_.size(); }
                    uint8_t* data() override { return buffer_.data(); }
                };

                auto mutableBuffer = std::make_shared<AllocBuffer>(512);
                auto arrayBuffer = facebook::jsi::ArrayBuffer(rt, mutableBuffer);
                std::memcpy(arrayBuffer.data(rt),
                            g_latestResult.embeddingBytes, 512);
                result.setProperty(rt, "embeddingBytes",
                                   std::move(arrayBuffer));
            }

            return result;
        });

    auto resetEngine = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "resetFaceAuthEngine"),
        0,
        [](facebook::jsi::Runtime& rt,
           const facebook::jsi::Value& thisVal,
           const facebook::jsi::Value* args,
           size_t count) -> facebook::jsi::Value {
            g_fsm->reset();
            g_clahe->reset();
            g_mailbox->drain();
            g_latestResult = FaceAuthResult{};

            LOGD("Engine reset");
            return facebook::jsi::Value(true);
        });

    auto insertProfile = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "insertFaceProfile"),
        2,
        [](facebook::jsi::Runtime& rt,
           const facebook::jsi::Value& thisVal,
           const facebook::jsi::Value* args,
           size_t count) -> facebook::jsi::Value {
            if (count < 2) {
                return facebook::jsi::Value(false);
            }

            std::string profileId = args[0].asString(rt).utf8(rt);

            if (!args[1].isObject()) {
                return facebook::jsi::Value(false);
            }

            auto embObj = args[1].asObject(rt);
            if (!embObj.isArrayBuffer(rt)) {
                return facebook::jsi::Value(false);
            }

            auto embBuffer = embObj.getArrayBuffer(rt);
            if (embBuffer.size(rt) < 512) {
                return facebook::jsi::Value(false);
            }

            float embedding[128];
            std::memcpy(embedding, embBuffer.data(rt), 512);

            g_lshIndex->insert(profileId.c_str(), embedding);
            g_lshIndex->build();

            LOGD("Profile inserted: %s", profileId.c_str());
            return facebook::jsi::Value(true);
        });

    auto removeProfile = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "removeFaceProfile"),
        1,
        [](facebook::jsi::Runtime& rt,
           const facebook::jsi::Value& thisVal,
           const facebook::jsi::Value* args,
           size_t count) -> facebook::jsi::Value {
            if (count < 1) {
                return facebook::jsi::Value(false);
            }

            std::string profileId = args[0].asString(rt).utf8(rt);
            g_lshIndex->remove(profileId.c_str());
            g_lshIndex->build();

            LOGD("Profile removed: %s", profileId.c_str());
            return facebook::jsi::Value(true);
        });

    auto getEngineStatus = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "getFaceAuthStatus"),
        0,
        [](facebook::jsi::Runtime& rt,
           const facebook::jsi::Value& thisVal,
           const facebook::jsi::Value* args,
           size_t count) -> facebook::jsi::Value {
            auto status = facebook::jsi::Object(rt);

            status.setProperty(rt, "engineRunning",
                               g_engineRunning.load(std::memory_order_acquire));
            status.setProperty(rt, "indexReady",
                               g_lshIndex->ready());
            status.setProperty(rt, "profileCount",
                               g_lshIndex->size());
            status.setProperty(rt, "mailboxHasFrame",
                               g_mailbox->hasFrame());

            return status;
        });

    runtime.global().setProperty(runtime, "startFaceAuthEngine",
                                  std::move(startEngine));
    runtime.global().setProperty(runtime, "stopFaceAuthEngine",
                                  std::move(stopEngine));
    runtime.global().setProperty(runtime, "processVisionFrame",
                                  std::move(processFrame));
    runtime.global().setProperty(runtime, "getFaceAuthResult",
                                  std::move(getResult));
    runtime.global().setProperty(runtime, "resetFaceAuthEngine",
                                  std::move(resetEngine));
    runtime.global().setProperty(runtime, "insertFaceProfile",
                                  std::move(insertProfile));
    runtime.global().setProperty(runtime, "removeFaceProfile",
                                  std::move(removeProfile));
    runtime.global().setProperty(runtime, "getFaceAuthStatus",
                                  std::move(getEngineStatus));

    LOGD("JSI bindings installed: 8 functions registered on global");
}

extern "C" JNIEXPORT void JNICALL
Java_com_drishti_FaceAuthModule_nativeInstall(
    JNIEnv* env,
    jclass clazz,
    jlong jsiRuntimePtr,
    jobject callInvokerHolder) {
    auto* runtime = reinterpret_cast<facebook::jsi::Runtime*>(jsiRuntimePtr);

    if (!runtime) {
        LOGE("Failed to obtain JSI runtime pointer");
        return;
    }

    auto callInvokerHolderCpp =
        facebook::jni::wrap_alias(reinterpret_cast<facebook::react::CallInvokerHolder::javaobject>(callInvokerHolder))->cthis();

    std::shared_ptr<facebook::react::CallInvoker> callInvoker;
    if (callInvokerHolderCpp) {
        callInvoker = callInvokerHolderCpp->getCallInvoker();
    }

    installFaceAuth(*runtime, callInvoker);

    LOGD("Native install complete for com.drishti");
}

extern "C" JNIEXPORT void JNICALL
Java_com_drishti_FaceAuthModule_nativeCleanup(
    JNIEnv* env,
    jclass clazz) {
    if (g_engineRunning.load(std::memory_order_acquire)) {
        g_engineRunning.store(false, std::memory_order_release);

        if (g_inferenceThread.joinable()) {
            g_inferenceThread.join();
        }
    }

    g_mailbox.reset();
    g_clahe.reset();
    g_fsm.reset();
    g_lshIndex.reset();
    g_latestResult = FaceAuthResult{};
    g_runtime = nullptr;
    g_callInvoker.reset();

    LOGD("Native cleanup complete");
}
