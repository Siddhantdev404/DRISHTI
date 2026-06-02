#pragma once

#include <cstdint>
#include <cmath>
#include <chrono>
#include <random>
#include <array>
#include "FaceAuthResult.h"
#include "TemporalVariance.h"

struct Landmark {
    float x;
    float y;
    float z;
};

class LivenessFSM {
public:
    static constexpr float EAR_BLINK_CLOSE = 0.20f;
    static constexpr float EAR_BLINK_OPEN = 0.25f;
    static constexpr float MAR_SMILE_THRESH = 0.45f;
    static constexpr float YAW_TURN_THRESH = 15.0f;
    static constexpr float VARIANCE_PASS = 0.30f;
    static constexpr int VARIANCE_FRAMES = 10;
    static constexpr float DETECT_CONF_MIN = 0.80f;
    static constexpr int IOD_PIXELS_MIN = 50;
    static constexpr int64_t CHALLENGE_TIMEOUT_MS = 4000;
    static constexpr int64_t CHALLENGE_EXCLUSION_MS = 30000;
    static constexpr int64_t PASS_HOLD_MS = 1000;
    static constexpr int64_t FAIL_HOLD_MS = 2000;

    static constexpr int RE_P1 = 33;
    static constexpr int RE_P2 = 160;
    static constexpr int RE_P3 = 158;
    static constexpr int RE_P4 = 133;
    static constexpr int RE_P5 = 153;
    static constexpr int RE_P6 = 144;

    static constexpr int LE_P1 = 362;
    static constexpr int LE_P2 = 385;
    static constexpr int LE_P3 = 387;
    static constexpr int LE_P4 = 263;
    static constexpr int LE_P5 = 373;
    static constexpr int LE_P6 = 380;

    static constexpr int LIP_TOP = 13;
    static constexpr int LIP_BOTTOM = 14;
    static constexpr int LIP_LEFT = 61;
    static constexpr int LIP_RIGHT = 291;
    static constexpr int LIP_UPPER_INNER = 82;
    static constexpr int LIP_LOWER_INNER = 87;

    static constexpr int NOSE_TIP = 1;
    static constexpr int CHIN = 152;
    static constexpr int LEFT_EYE_OUTER = 263;
    static constexpr int RIGHT_EYE_OUTER = 33;
    static constexpr int LEFT_EYE_INNER = 362;
    static constexpr int RIGHT_EYE_INNER = 133;
    static constexpr int FOREHEAD = 10;

    LivenessFSM();

    void reset();

    void processFrame(const Landmark* landmarks, int landmarkCount,
                      float detectionConfidence,
                      const uint8_t* grayPixels, int imageWidth, int imageHeight,
                      FaceAuthResult& result);

    float computeEAR(const Landmark* landmarks) const;
    float computeMAR(const Landmark* landmarks) const;
    void computeHeadPose(const Landmark* landmarks, float imageWidth, float imageHeight,
                         float& outYaw, float& outPitch) const;

private:
    LivenessState state_;
    ChallengeType currentChallenge_;
    ChallengeType lastChallenge_;
    int64_t lastChallengeTimeMs_;

    int64_t stateEntryTimeMs_;
    int64_t terminalEntryTimeMs_;
    uint32_t frameCount_;

    bool blinkCloseDetected_;

    VarianceAccumulator varianceAccumulator_;
    int varianceFrameCount_;

    std::mt19937 rng_;

    int64_t nowMs() const;
    float distance2D(const Landmark& a, const Landmark& b) const;
    float distance3D(const Landmark& a, const Landmark& b) const;
    ChallengeType pickChallenge();
    void transitionTo(LivenessState newState);
};
