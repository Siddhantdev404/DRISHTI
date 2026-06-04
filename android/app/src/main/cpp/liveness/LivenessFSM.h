// liveness/LivenessFSM.h
#pragma once
#include <chrono>
#include <random>
#include <map>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
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
  static constexpr float  EAR_BLINK_CLOSE  = 0.24f;  // below = eye closed (more sensitive)
  static constexpr float  EAR_BLINK_OPEN   = 0.28f;  // above = eye re-opened (blink complete)
  static constexpr float  MAR_SMILE_THRESH = 0.45f;  // above = smile detected
  static constexpr float  YAW_TURN_THRESH  = 13.0f;  // degrees from neutral = turn
  static constexpr float  VARIANCE_PASS    = 0.000002f; // normalized landmark temporal motion
  static constexpr int    VARIANCE_FRAMES  = 12;     // frames of variance check required
  static constexpr float  DETECT_CONF_MIN  = 0.70f;  // FaceMesh confidence gate
  static constexpr float  TRACK_CONF_MIN   = 0.35f;  // lower gate after lock-on
  static constexpr float  CHALLENGE_CONF_MIN = 0.25f; // ignore weak challenge frames
  static constexpr int    IOD_PIXELS_MIN   = 50;     // inter-ocular distance minimum
  static constexpr int    CHALLENGE_TIMEOUT_MS = 4000;
  static constexpr int    CHALLENGE_EXCLUSION_MS = 30000; // no repeat within 30s

  LivenessFSM() : rng_(std::random_device{}()) {}

  LivenessState getCurrentState() const { return currentState_; }
  ChallengeType getActiveChallenge() const { return activeChallenge_; }
  float getLastEar() const { return lastEar_; }
  float getLastMar() const { return lastMar_; }
  float getLastYaw() const { return lastYaw_; }
  float getLastPitch() const { return lastPitch_; }
  float getLastTempVariance() const { return lastTempVariance_; }

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
            resetVarianceWindow();
          }
        }
        break;

      case LivenessState::DETECTED:
        if (confidenceScore < TRACK_CONF_MIN) {
          currentState_ = LivenessState::IDLE;  // face lost
          break;
        }
        currentState_ = LivenessState::VARIANCE_CHECK;
        [[fallthrough]];

      case LivenessState::VARIANCE_CHECK: {
        if (confidenceScore < TRACK_CONF_MIN) {
          currentState_ = LivenessState::IDLE; break;
        }
        
        pushLandmarkVarianceFrame(landmarks);
        varianceFrameCount_++;

        if (varianceFrameCount_ >= VARIANCE_FRAMES) {
          lastTempVariance_ = computeLandmarkVariance();
          if (lastTempVariance_ >= VARIANCE_PASS) {
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
        if (confidenceScore < CHALLENGE_CONF_MIN) {
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
          passTimestamp_ = now();
          currentState_ = LivenessState::LIVENESS_PASS;
        }
        break;
      }

      case LivenessState::LIVENESS_PASS:
        // Holds for 1 second then resets — allows embedding capture window
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
    resetVarianceWindow();
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
  float lastEar_ = 0.0f;
  float lastMar_ = 0.0f;
  float lastYaw_ = 0.0f;
  float lastPitch_ = 0.0f;
  float lastTempVariance_ = 0.0f;
  static constexpr int TRACKED_LANDMARKS = 8;
  static constexpr std::array<int, TRACKED_LANDMARKS> TRACKED_POINTS = {
    1, 4, 33, 61, 199, 263, 291, 467
  };
  std::array<std::array<float, TRACKED_LANDMARKS * 2>, VARIANCE_FRAMES> landmarkWindow_{};

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

  float distance(const float* lm, int i, int j) {
    float dx = lm[i*3] - lm[j*3];
    float dy = lm[i*3+1] - lm[j*3+1];
    return std::sqrt(dx*dx + dy*dy);
  }

  float computeEAR(const float* lm) {
    float leftV = distance(lm, 159, 145);
    float leftH = distance(lm, 33, 133);
    float rightV = distance(lm, 386, 374);
    float rightH = distance(lm, 362, 263);
    float earLeft = leftH > 0 ? leftV / leftH : 0.0f;
    float earRight = rightH > 0 ? rightV / rightH : 0.0f;
    lastEar_ = (earLeft + earRight) / 2.0f;
    return lastEar_;
  }
  
  float computeMAR(const float* lm) {
    float mouthV = distance(lm, 13, 14);
    float mouthH = distance(lm, 78, 308);
    lastMar_ = mouthH > 0 ? mouthV / mouthH : 0.0f;
    return lastMar_;
  }
  
  float computeYaw(const float* lm) {
    float iod = std::max(std::abs(lm[263*3] - lm[33*3]), 1e-4f);
    float left = lm[4*3] - lm[33*3];
    float right = lm[263*3] - lm[4*3];
    lastYaw_ = std::clamp(((left - right) / iod) * 45.0f, -45.0f, 45.0f);
    return lastYaw_;
  }

  float computePitch(const float* lm) {
    float eyeY = (lm[33*3+1] + lm[263*3+1]) * 0.5f;
    float mouthY = (lm[13*3+1] + lm[14*3+1]) * 0.5f;
    float faceH = std::max(std::abs(mouthY - eyeY), 1e-4f);
    float noseOffset = (lm[4*3+1] - eyeY) / faceH;
    lastPitch_ = std::clamp((noseOffset - 0.52f) * 70.0f, -35.0f, 35.0f);
    return lastPitch_;
  }

  bool evaluateBlink(const float* lm) {
    float ear = computeEAR(lm);
    if (ear < EAR_BLINK_CLOSE) {
      blinkEyeWasClosed_ = true;
    } else if (ear > EAR_BLINK_OPEN && blinkEyeWasClosed_) {
      return true;
    }
    return false;
  }

  float computeIOD(const float* lm, int imageWidth) {
    float lx = lm[33*3+0], rx = lm[263*3+0];
    return std::abs(lx - rx) * imageWidth;
  }

  void resetVarianceWindow() {
    varianceFrameCount_ = 0;
    lastTempVariance_ = 0.0f;
    landmarkWindow_ = {};
  }

  void pushLandmarkVarianceFrame(const float* lm) {
    int index = std::min(varianceFrameCount_, VARIANCE_FRAMES - 1);
    for (int i = 0; i < TRACKED_LANDMARKS; i++) {
      int landmark = TRACKED_POINTS[i];
      landmarkWindow_[index][i * 2] = lm[landmark * 3];
      landmarkWindow_[index][i * 2 + 1] = lm[landmark * 3 + 1];
    }
    computeEAR(lm);
    computeMAR(lm);
    computeYaw(lm);
    computePitch(lm);
  }

  float computeLandmarkVariance() {
    int frames = std::min(varianceFrameCount_, VARIANCE_FRAMES);
    if (frames < 2) return 0.0f;

    float total = 0.0f;
    for (int p = 0; p < TRACKED_LANDMARKS * 2; p++) {
      float mean = 0.0f;
      for (int f = 0; f < frames; f++) mean += landmarkWindow_[f][p];
      mean /= static_cast<float>(frames);

      float variance = 0.0f;
      for (int f = 0; f < frames; f++) {
        float delta = landmarkWindow_[f][p] - mean;
        variance += delta * delta;
      }
      total += variance / static_cast<float>(frames - 1);
    }

    return total / static_cast<float>(TRACKED_LANDMARKS * 2);
  }

  int64_t msElapsed(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t).count();
  }

  std::chrono::steady_clock::time_point now() {
    return std::chrono::steady_clock::now();
  }
};
