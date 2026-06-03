// liveness/LivenessFSM.h
#pragma once
#include <chrono>
#include <random>
#include <map>
#include <vector>
#include "EARDetector.h"
#include "MARDetector.h"
#include "HeadPoseEstimator.h"
#include "TemporalVariance.h"
#include "../shared/FaceAuthResult.h"

struct FSMOutput {
  LivenessState state;
  ChallengeType challenge;
};

class LivenessFSM {
public:
  // === THRESHOLDS — calibrated for MediaPipe FaceMesh v2 ===
  static constexpr float  EAR_BLINK_CLOSE  = 0.20f;  // below = eye closed
  static constexpr float  EAR_BLINK_OPEN   = 0.25f;  // above = eye re-opened (blink complete)
  static constexpr float  MAR_SMILE_THRESH = 0.45f;  // above = smile detected
  static constexpr float  YAW_TURN_THRESH  = 15.0f;  // degrees from neutral = turn
  static constexpr float  VARIANCE_PASS    = 0.30f;  // microsaccade variance threshold
  static constexpr int    VARIANCE_FRAMES  = 10;     // frames of variance check required
  static constexpr float  DETECT_CONF_MIN  = 0.80f;  // FaceMesh confidence gate
  static constexpr int    IOD_PIXELS_MIN   = 50;     // inter-ocular distance minimum
  static constexpr int    CHALLENGE_TIMEOUT_MS = 4000;
  static constexpr int    CHALLENGE_EXCLUSION_MS = 30000; // no repeat within 30s

  LivenessFSM() : rng_(std::random_device{}()) {}

  // Call once per frame from InferencePipeline.
  // landmarks: pointer to 468×3 float32 array from FaceMesh output tensor
  // confidenceScore: FaceMesh detection confidence [0,1]
  FSMOutput update(const float* landmarks, float confidenceScore, int imageWidth) {
    FSMOutput out;
    out.state = currentState_;

    switch (currentState_) {

      case LivenessState::IDLE:
        if (confidenceScore >= DETECT_CONF_MIN) {
          float iod = computeIOD(landmarks, imageWidth);
          if (iod >= IOD_PIXELS_MIN) {
            currentState_ = LivenessState::DETECTED;
            varianceFrameCount_ = 0;
            leftVarAcc_  = {};
            rightVarAcc_ = {};
          }
        }
        break;

      case LivenessState::DETECTED:
        if (confidenceScore < DETECT_CONF_MIN) {
          currentState_ = LivenessState::IDLE;  // face lost
          break;
        }
        currentState_ = LivenessState::VARIANCE_CHECK;
        [[fallthrough]];

      case LivenessState::VARIANCE_CHECK: {
        if (confidenceScore < DETECT_CONF_MIN) {
          currentState_ = LivenessState::IDLE; break;
        }
        // Run temporal variance on 10×10 patch around each eye landmark centroid
        // Landmark 159 = left eye center (FaceMesh v2 canonical indices)
        // Landmark 386 = right eye center
        bool leftPass  = checkTemporalVariance(leftVarAcc_,  landmarks, 159);
        bool rightPass = checkTemporalVariance(rightVarAcc_, landmarks, 386);
        varianceFrameCount_++;

        if (varianceFrameCount_ >= VARIANCE_FRAMES) {
          if (leftPass && rightPass) {
            currentState_ = LivenessState::CHALLENGE_ACTIVE;
            activeChallenge_ = selectChallenge();
            challengeStart_  = now();
            blinkEyeWasClosed_ = false;
          } else {
            currentState_ = LivenessState::TEMPORAL_VARIANCE_FAIL;
            failTimestamp_ = now();
          }
        }
        break;
      }

      case LivenessState::CHALLENGE_ACTIVE: {
        auto elapsed = msElapsed(challengeStart_);
        if (elapsed > CHALLENGE_TIMEOUT_MS) {
          currentState_ = LivenessState::LIVENESS_FAIL;
          failTimestamp_ = now();
          break;
        }
        bool passed = false;
        switch (activeChallenge_) {
          case ChallengeType::BLINK:
            passed = evaluateBlink(landmarks);
            break;
          case ChallengeType::SMILE:
            passed = (computeMAR(landmarks) > MAR_SMILE_THRESH);
            break;
          case ChallengeType::TURN_LEFT:
            passed = (computeYaw(landmarks) < -YAW_TURN_THRESH);
            break;
          case ChallengeType::TURN_RIGHT:
            passed = (computeYaw(landmarks) >  YAW_TURN_THRESH);
            break;
          default: break;
        }
        if (passed) {
          lastChallengeTime_[activeChallenge_] = now();
          currentState_ = LivenessState::LIVENESS_PASS;
        }
        break;
      }

      case LivenessState::LIVENESS_PASS:
        // Holds for 1 second then resets — allows embedding capture window
        if (msElapsed(passTimestamp_) == 0) passTimestamp_ = now();
        if (msElapsed(passTimestamp_) > 1000) reset();
        break;

      case LivenessState::LIVENESS_FAIL:
      case LivenessState::TEMPORAL_VARIANCE_FAIL:
        // Hold fail state for 2 seconds to display error, then reset
        if (msElapsed(failTimestamp_) > 2000) reset();
        break;
    }

    out.state = currentState_;
    out.challenge = activeChallenge_;
    return out;
  }

  void reset() {
    currentState_     = LivenessState::IDLE;
    activeChallenge_  = ChallengeType::NONE;
    varianceFrameCount_ = 0;
    blinkEyeWasClosed_ = false;
    passTimestamp_    = {};
    failTimestamp_    = {};
  }

private:
  LivenessState currentState_    = LivenessState::IDLE;
  ChallengeType activeChallenge_ = ChallengeType::NONE;
  std::chrono::steady_clock::time_point challengeStart_;
  std::chrono::steady_clock::time_point passTimestamp_;
  std::chrono::steady_clock::time_point failTimestamp_;
  std::map<ChallengeType, std::chrono::steady_clock::time_point> lastChallengeTime_;
  int varianceFrameCount_ = 0;
  bool blinkEyeWasClosed_ = false;
  VarianceAccumulator leftVarAcc_, rightVarAcc_;
  std::mt19937 rng_;

  ChallengeType selectChallenge() {
    // Eligible challenges = all except the one used in the last 30 seconds
    std::vector<ChallengeType> eligible;
    auto candidates = {
      ChallengeType::BLINK,
      ChallengeType::SMILE,
      ChallengeType::TURN_LEFT,
      ChallengeType::TURN_RIGHT
    };
    for (auto c : candidates) {
      auto it = lastChallengeTime_.find(c);
      if (it == lastChallengeTime_.end() ||
          msElapsed(it->second) > CHALLENGE_EXCLUSION_MS) {
        eligible.push_back(c);
      }
    }
    if (eligible.empty()) eligible = candidates; // safety: all eligible
    std::uniform_int_distribution<int> dist(0, (int)eligible.size() - 1);
    return eligible[dist(rng_)];
  }

  bool evaluateBlink(const float* lm) {
    float ear = computeEAR(lm);
    if (!blinkEyeWasClosed_ && ear < EAR_BLINK_CLOSE) {
      blinkEyeWasClosed_ = true;
    }
    return blinkEyeWasClosed_ && ear > EAR_BLINK_OPEN;
  }

  float computeIOD(const float* lm, int imageWidth) {
    // Landmarks 33 (right eye outer) and 263 (left eye outer)
    // lm is 468×3 normalized floats: index = landmark_idx * 3
    float lx = lm[33*3+0], rx = lm[263*3+0];
    return std::abs(lx - rx) * imageWidth;
  }

  int64_t msElapsed(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t).count();
  }

  std::chrono::steady_clock::time_point now() {
    return std::chrono::steady_clock::now();
  }
};
