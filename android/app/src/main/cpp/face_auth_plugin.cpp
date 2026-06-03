#include <jni.h>
#include <jsi/jsi.h>
#include <android/log.h>

#undef LOG_TAG
#define LOG_TAG "FaceAuthPlugin"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace facebook::jsi;

// ─── Stub state ──────────────────────────────────────────────────────────────
static bool sEngineRunning = false;
static int  sFrameCount = 0;
static uint64_t sLastTimeMs = 0;
static float sCurrentFps = 0.0f;

static uint64_t getNowMs() {
    struct timespec res;
    clock_gettime(CLOCK_MONOTONIC, &res);
    return 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;
}

// ─── Helper: install all 8 JSI global functions ──────────────────────────────
static void installFaceAuth(Runtime& runtime) {
    LOGI("installFaceAuth: registering 8 JSI global functions");

    // 1) startFaceAuthEngine() -> boolean
    auto startFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "startFaceAuthEngine"),
        0,
        [](Runtime& rt, const Value&, const Value*, size_t) -> Value {
            sEngineRunning = true;
            sFrameCount = 0;
            sLastTimeMs = getNowMs();
            sCurrentFps = 0.0f;
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
            LOGI("stopFaceAuthEngine called");
            return Value(true);
        });
    runtime.global().setProperty(runtime, "stopFaceAuthEngine", std::move(stopFn));

    // 3) processVisionFrame(frameBuffer, width, height, stride) -> boolean
    auto processFn = Function::createFromHostFunction(
        runtime,
        PropNameID::forAscii(runtime, "processVisionFrame"),
        4,
        [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (!sEngineRunning) return Value(false);
            
            sFrameCount++;
            
            // Calculate pseudo FPS every 10 frames
            if (sFrameCount % 10 == 0) {
                uint64_t now = getNowMs();
                uint64_t diff = now - sLastTimeMs;
                if (diff > 0) {
                    sCurrentFps = 1000.0f * 10.0f / diff;
                }
                sLastTimeMs = now;
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
            
            // Simulate FSM based on frame count
            int state = 0; // IDLE
            int challenge = 0; // NONE
            bool ready = false;
            
            if (sFrameCount > 180) {
                state = 4; // LIVENESS_PASS
                ready = true;
            } else if (sFrameCount > 120) {
                state = 3; // CHALLENGE_ACTIVE
                challenge = 1; // BLINK
            } else if (sFrameCount > 60) {
                state = 2; // VARIANCE_CHECK
            } else if (sFrameCount > 10) {
                state = 1; // DETECTED
            }

            result.setProperty(rt, "livenessState",   state);
            result.setProperty(rt, "activeChallenge", challenge);
            result.setProperty(rt, "matchScore",       0.98);
            result.setProperty(rt, "matchedId",        String::createFromAscii(rt, "stub-user-123"));
            result.setProperty(rt, "embeddingReady",   ready);
            result.setProperty(rt, "inferenceMs",      16.5);
            result.setProperty(rt, "ear",              0.28);
            result.setProperty(rt, "mar",              0.10);
            result.setProperty(rt, "yaw",              2.0);
            result.setProperty(rt, "pitch",            1.5);
            result.setProperty(rt, "tempVariance",     0.15);
            result.setProperty(rt, "frameCount",       sFrameCount);
            result.setProperty(rt, "nativeFps",        sCurrentFps);
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
            sFrameCount = 0;
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
            LOGI("insertFaceProfile: %s", profileId.c_str());
            return Value(true);
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
            status.setProperty(rt, "indexReady",       true);
            status.setProperty(rt, "profileCount",     0);
            status.setProperty(rt, "mailboxHasFrame",  false);
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
    jobject callInvokerHolder
) {
    LOGI("nativeInstall called with JSI ptr: %lld", (long long)jsiPtr);
    auto runtime = reinterpret_cast<Runtime*>(jsiPtr);
    installFaceAuth(*runtime);
}

extern "C" JNIEXPORT void JNICALL
Java_com_drishti_FaceAuthModule_nativeCleanup(
    JNIEnv *env,
    jclass  clazz
) {
    LOGI("nativeCleanup called");
    sEngineRunning = false;
    sFrameCount = 0;
}
