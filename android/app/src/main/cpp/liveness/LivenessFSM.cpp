#include "LivenessFSM.h"
#include <cstring>
#include <algorithm>

LivenessFSM::LivenessFSM()
    : state_(LivenessState::IDLE),
      currentChallenge_(ChallengeType::NONE),
      lastChallenge_(ChallengeType::NONE),
      lastChallengeTimeMs_(0),
      stateEntryTimeMs_(0),
      terminalEntryTimeMs_(0),
      frameCount_(0),
      blinkCloseDetected_(false),
      varianceAccumulator_(),
      varianceFrameCount_(0),
      rng_(static_cast<uint32_t>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {
}

void LivenessFSM::reset() {
    state_ = LivenessState::IDLE;
    currentChallenge_ = ChallengeType::NONE;
    lastChallenge_ = ChallengeType::NONE;
    lastChallengeTimeMs_ = 0;
    stateEntryTimeMs_ = 0;
    terminalEntryTimeMs_ = 0;
    frameCount_ = 0;
    blinkCloseDetected_ = false;
    varianceAccumulator_.reset();
    varianceFrameCount_ = 0;
}

int64_t LivenessFSM::nowMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch())
        .count();
}

float LivenessFSM::distance2D(const Landmark& a, const Landmark& b) const {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float LivenessFSM::distance3D(const Landmark& a, const Landmark& b) const {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float LivenessFSM::computeEAR(const Landmark* lm) const {
    float rightVertA = distance2D(lm[RE_P2], lm[RE_P6]);
    float rightVertB = distance2D(lm[RE_P3], lm[RE_P5]);
    float rightHoriz = distance2D(lm[RE_P1], lm[RE_P4]);

    float leftVertA = distance2D(lm[LE_P2], lm[LE_P6]);
    float leftVertB = distance2D(lm[LE_P3], lm[LE_P5]);
    float leftHoriz = distance2D(lm[LE_P1], lm[LE_P4]);

    float rightEAR = 0.0f;
    if (rightHoriz > 1e-6f) {
        rightEAR = (rightVertA + rightVertB) / (2.0f * rightHoriz);
    }

    float leftEAR = 0.0f;
    if (leftHoriz > 1e-6f) {
        leftEAR = (leftVertA + leftVertB) / (2.0f * leftHoriz);
    }

    return (rightEAR + leftEAR) / 2.0f;
}

float LivenessFSM::computeMAR(const Landmark* lm) const {
    float vertOuter = distance2D(lm[LIP_TOP], lm[LIP_BOTTOM]);
    float vertInner = distance2D(lm[LIP_UPPER_INNER], lm[LIP_LOWER_INNER]);
    float horiz = distance2D(lm[LIP_LEFT], lm[LIP_RIGHT]);

    if (horiz < 1e-6f) {
        return 0.0f;
    }

    float avgVert = (vertOuter + vertInner) / 2.0f;
    return avgVert / horiz;
}

void LivenessFSM::computeHeadPose(const Landmark* lm, float imageWidth,
                                   float imageHeight, float& outYaw,
                                   float& outPitch) const {
    float leftEyeX = (lm[LEFT_EYE_OUTER].x + lm[LEFT_EYE_INNER].x) / 2.0f;
    float leftEyeY = (lm[LEFT_EYE_OUTER].y + lm[LEFT_EYE_INNER].y) / 2.0f;
    float rightEyeX = (lm[RIGHT_EYE_OUTER].x + lm[RIGHT_EYE_INNER].x) / 2.0f;
    float rightEyeY = (lm[RIGHT_EYE_OUTER].y + lm[RIGHT_EYE_INNER].y) / 2.0f;

    float eyeMidX = (leftEyeX + rightEyeX) / 2.0f;
    // float eyeMidY = (leftEyeY + rightEyeY) / 2.0f;

    float iod = distance2D(
        Landmark{leftEyeX, leftEyeY, 0.0f},
        Landmark{rightEyeX, rightEyeY, 0.0f});

    if (iod < 1e-6f) {
        outYaw = 0.0f;
        outPitch = 0.0f;
        return;
    }

    float noseTipX = lm[NOSE_TIP].x;
    float noseTipY = lm[NOSE_TIP].y;

    float noseOffsetX = noseTipX - eyeMidX;
    float normalizedOffsetX = noseOffsetX / iod;
    float clampedX = std::max(-1.0f, std::min(1.0f, normalizedOffsetX * 2.0f));
    outYaw = std::asin(clampedX) * (180.0f / 3.14159265358979323846f);

    float foreheadY = lm[FOREHEAD].y;
    float chinY = lm[CHIN].y;
    float faceHeight = std::abs(chinY - foreheadY);

    if (faceHeight < 1e-6f) {
        outPitch = 0.0f;
        return;
    }

    float noseRelY = noseTipY - foreheadY;
    float normalizedY = noseRelY / faceHeight;
    float expectedRatio = 0.63f;
    float pitchOffset = (normalizedY - expectedRatio) * 2.5f;
    float clampedY = std::max(-1.0f, std::min(1.0f, pitchOffset));
    outPitch = std::asin(clampedY) * (180.0f / 3.14159265358979323846f);
}

ChallengeType LivenessFSM::pickChallenge() {
    int64_t now = nowMs();
    bool exclusionActive = (now - lastChallengeTimeMs_) < CHALLENGE_EXCLUSION_MS;

    std::array<ChallengeType, 4> candidates = {
        ChallengeType::BLINK,
        ChallengeType::SMILE,
        ChallengeType::TURN_LEFT,
        ChallengeType::TURN_RIGHT
    };

    std::array<ChallengeType, 4> filtered;
    int filteredCount = 0;

    for (int i = 0; i < 4; i++) {
        if (exclusionActive && candidates[i] == lastChallenge_) {
            continue;
        }
        filtered[filteredCount] = candidates[i];
        filteredCount++;
    }

    if (filteredCount == 0) {
        filteredCount = 4;
        for (int i = 0; i < 4; i++) {
            filtered[i] = candidates[i];
        }
    }

    std::uniform_int_distribution<int> dist(0, filteredCount - 1);
    int index = dist(rng_);

    ChallengeType chosen = filtered[index];
    lastChallenge_ = chosen;
    lastChallengeTimeMs_ = now;

    return chosen;
}

void LivenessFSM::transitionTo(LivenessState newState) {
    state_ = newState;
    stateEntryTimeMs_ = nowMs();
}

void LivenessFSM::processFrame(const Landmark* landmarks, int landmarkCount,
                                float detectionConfidence,
                                const uint8_t* grayPixels,
                                int imageWidth, int imageHeight,
                                FaceAuthResult& result) {
    frameCount_++;
    int64_t now = nowMs();

    if (state_ == LivenessState::LIVENESS_PASS) {
        int64_t elapsed = now - terminalEntryTimeMs_;
        result.livenessState = LivenessState::LIVENESS_PASS;
        result.activeChallenge = currentChallenge_;
        result.frameCount = frameCount_;
        if (elapsed >= PASS_HOLD_MS) {
            reset();
            result.livenessState = LivenessState::IDLE;
            result.activeChallenge = ChallengeType::NONE;
        }
        return;
    }

    if (state_ == LivenessState::LIVENESS_FAIL ||
        state_ == LivenessState::TEMPORAL_VARIANCE_FAIL) {
        int64_t elapsed = now - terminalEntryTimeMs_;
        result.livenessState = state_;
        result.activeChallenge = currentChallenge_;
        result.frameCount = frameCount_;
        if (elapsed >= FAIL_HOLD_MS) {
            reset();
            result.livenessState = LivenessState::IDLE;
            result.activeChallenge = ChallengeType::NONE;
        }
        return;
    }

    if (landmarkCount < 468 || detectionConfidence < DETECT_CONF_MIN) {
        if (state_ != LivenessState::IDLE) {
            reset();
        }
        result.livenessState = LivenessState::IDLE;
        result.activeChallenge = ChallengeType::NONE;
        result.frameCount = frameCount_;
        return;
    }

    float iod = distance2D(landmarks[RIGHT_EYE_OUTER], landmarks[LEFT_EYE_OUTER]);
    if (iod < static_cast<float>(IOD_PIXELS_MIN)) {
        if (state_ != LivenessState::IDLE) {
            reset();
        }
        result.livenessState = LivenessState::IDLE;
        result.activeChallenge = ChallengeType::NONE;
        result.frameCount = frameCount_;
        return;
    }

    float ear = computeEAR(landmarks);
    float mar = computeMAR(landmarks);
    float yaw = 0.0f;
    float pitch = 0.0f;
    computeHeadPose(landmarks, static_cast<float>(imageWidth),
                    static_cast<float>(imageHeight), yaw, pitch);

    result.ear = ear;
    result.mar = mar;
    result.yaw = yaw;
    result.pitch = pitch;
    result.frameCount = frameCount_;

    switch (state_) {
        case LivenessState::IDLE: {
            transitionTo(LivenessState::DETECTED);
            varianceAccumulator_.reset();
            varianceFrameCount_ = 0;
            result.livenessState = LivenessState::DETECTED;
            result.activeChallenge = ChallengeType::NONE;
            break;
        }

        case LivenessState::DETECTED: {
            transitionTo(LivenessState::VARIANCE_CHECK);
            varianceAccumulator_.reset();
            varianceFrameCount_ = 0;
            result.livenessState = LivenessState::VARIANCE_CHECK;
            result.activeChallenge = ChallengeType::NONE;
            break;
        }

        case LivenessState::VARIANCE_CHECK: {
            float rightEyeX = landmarks[RIGHT_EYE_OUTER].x;
            float rightEyeY = landmarks[RIGHT_EYE_OUTER].y;
            float leftEyeX = landmarks[LEFT_EYE_OUTER].x;
            float leftEyeY = landmarks[LEFT_EYE_OUTER].y;

            float eyeCenterX = (rightEyeX + leftEyeX) / 2.0f;
            float eyeCenterY = (rightEyeY + leftEyeY) / 2.0f;

            int cropX = static_cast<int>(eyeCenterX) - 5;
            int cropY = static_cast<int>(eyeCenterY) - 5;

            cropX = std::max(0, std::min(cropX, imageWidth - 10));
            cropY = std::max(0, std::min(cropY, imageHeight - 10));

            varianceAccumulator_.pushFrame(grayPixels, imageWidth, imageHeight,
                                           cropX, cropY);
            varianceFrameCount_++;

            if (varianceFrameCount_ >= VARIANCE_FRAMES) {
                float variance = varianceAccumulator_.computeVariance();
                result.tempVariance = variance;

                if (variance < VARIANCE_PASS) {
                    state_ = LivenessState::TEMPORAL_VARIANCE_FAIL;
                    terminalEntryTimeMs_ = nowMs();
                    result.livenessState = LivenessState::TEMPORAL_VARIANCE_FAIL;
                    result.activeChallenge = ChallengeType::NONE;
                    std::strncpy(result.errorCode, "ERR_LIVENESS_FAIL", 31);
                    result.errorCode[31] = '\0';
                    return;
                }

                currentChallenge_ = pickChallenge();
                blinkCloseDetected_ = false;
                transitionTo(LivenessState::CHALLENGE_ACTIVE);
                result.livenessState = LivenessState::CHALLENGE_ACTIVE;
                result.activeChallenge = currentChallenge_;
            } else {
                result.livenessState = LivenessState::VARIANCE_CHECK;
                result.activeChallenge = ChallengeType::NONE;
            }
            break;
        }

        case LivenessState::CHALLENGE_ACTIVE: {
            int64_t elapsed = now - stateEntryTimeMs_;

            if (elapsed > CHALLENGE_TIMEOUT_MS) {
                state_ = LivenessState::LIVENESS_FAIL;
                terminalEntryTimeMs_ = nowMs();
                result.livenessState = LivenessState::LIVENESS_FAIL;
                result.activeChallenge = currentChallenge_;
                std::strncpy(result.errorCode, "ERR_TIMEOUT", 31);
                result.errorCode[31] = '\0';
                return;
            }

            bool challengePassed = false;

            switch (currentChallenge_) {
                case ChallengeType::BLINK: {
                    if (!blinkCloseDetected_) {
                        if (ear < EAR_BLINK_CLOSE) {
                            blinkCloseDetected_ = true;
                        }
                    } else {
                        if (ear > EAR_BLINK_OPEN) {
                            challengePassed = true;
                        }
                    }
                    break;
                }

                case ChallengeType::SMILE: {
                    if (mar > MAR_SMILE_THRESH) {
                        challengePassed = true;
                    }
                    break;
                }

                case ChallengeType::TURN_LEFT: {
                    if (yaw < -YAW_TURN_THRESH) {
                        challengePassed = true;
                    }
                    break;
                }

                case ChallengeType::TURN_RIGHT: {
                    if (yaw > YAW_TURN_THRESH) {
                        challengePassed = true;
                    }
                    break;
                }

                case ChallengeType::NONE: {
                    break;
                }
            }

            if (challengePassed) {
                state_ = LivenessState::LIVENESS_PASS;
                terminalEntryTimeMs_ = nowMs();
                result.livenessState = LivenessState::LIVENESS_PASS;
                result.activeChallenge = currentChallenge_;
                std::memset(result.errorCode, 0, sizeof(result.errorCode));
            } else {
                result.livenessState = LivenessState::CHALLENGE_ACTIVE;
                result.activeChallenge = currentChallenge_;
            }
            break;
        }

        case LivenessState::LIVENESS_PASS:
        case LivenessState::LIVENESS_FAIL:
        case LivenessState::TEMPORAL_VARIANCE_FAIL: {
            break;
        }
    }
}
