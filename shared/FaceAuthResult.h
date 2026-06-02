#pragma once

#include <string>
#include <cstdint>

static constexpr const char* LEDGER_GENESIS_HASH =
    "0000000000000000000000000000000000000000000000000000000000000000";

enum class LivenessState : uint8_t {
    IDLE = 0,
    DETECTED = 1,
    VARIANCE_CHECK = 2,
    CHALLENGE_ACTIVE = 3,
    LIVENESS_PASS = 4,
    LIVENESS_FAIL = 5,
    TEMPORAL_VARIANCE_FAIL = 6
};

enum class ChallengeType : uint8_t {
    NONE = 0,
    BLINK = 1,
    SMILE = 2,
    TURN_LEFT = 3,
    TURN_RIGHT = 4
};

struct FaceAuthResult {
    LivenessState livenessState = LivenessState::IDLE;
    ChallengeType activeChallenge = ChallengeType::NONE;
    float matchScore = 0.0f;
    char matchedId[37] = {};
    uint8_t embeddingBytes[512] = {};
    bool embeddingReady = false;
    float inferenceMs = 0.0f;
    float ear = 0.0f;
    float mar = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float tempVariance = 0.0f;
    uint32_t frameCount = 0;
    int32_t nativeFps = 0;
    uint32_t nativeHeapKB = 0;
    char errorCode[32] = {};
};
