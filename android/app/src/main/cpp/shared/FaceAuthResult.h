// FaceAuthResult.h — FROZEN contract. Edit only via joint bridge PR.
// Genesis sentinel for the ledger chain — both C++ and JS must match exactly.
#pragma once
#include <string>
#include <cstdint>

static constexpr const char* LEDGER_GENESIS_HASH =
    "0000000000000000000000000000000000000000000000000000000000000000";
// 64 hex zeros — SHA256("") would differ; use this fixed sentinel
// so genesis detection is explicit, not implicit.

enum class LivenessState : uint8_t {
  IDLE                  = 0,
  DETECTED              = 1,
  VARIANCE_CHECK        = 2,
  CHALLENGE_ACTIVE      = 3,
  LIVENESS_PASS         = 4,
  LIVENESS_FAIL         = 5,
  TEMPORAL_VARIANCE_FAIL = 6,
};

enum class ChallengeType : uint8_t {
  NONE       = 0,
  BLINK      = 1,
  SMILE      = 2,
  TURN_LEFT  = 3,
  TURN_RIGHT = 4,
};

struct FaceAuthResult {
  // ── Primary output fields ──────────────────────────────
  LivenessState livenessState   = LivenessState::IDLE;
  ChallengeType activeChallenge = ChallengeType::NONE;
  float         matchScore      = 0.0f;  // cosine similarity [0.0, 1.0]
  char          matchedId[37]   = {};    // UUID v4 lowercase hyphenated, null-terminated
                                         // empty string = no match / not yet verified

  // ── Enrollment output (only valid when livenessState == LIVENESS_PASS) ──
  uint8_t       embeddingBytes[512] = {}; // 128 × float32, L2-normalized, raw bytes
  bool          embeddingReady      = false;

  // ── Debug / HUD fields ─────────────────────────────────
  float         inferenceMs   = 0.0f;   // total pipeline wall-clock time, ms
  float         ear           = 0.0f;   // Eye Aspect Ratio, left eye
  float         mar           = 0.0f;   // Mouth Aspect Ratio
  float         yaw           = 0.0f;   // head yaw degrees, +right
  float         pitch         = 0.0f;   // head pitch degrees, +up
  float         tempVariance  = 0.0f;   // temporal gradient variance, last frame
  uint32_t      frameCount    = 0;      // monotonically increasing frame counter
  int32_t       nativeFps     = 0;      // frames/sec computed in native pipeline
  uint32_t      nativeHeapKB  = 0;      // native heap allocated, KB

  // ── Error propagation ─────────────────────────────────
  // Empty string = no error. M2 checks this FIRST before reading any other field.
  // M1 NEVER throws a C++ exception across the JSI boundary.
  char errorCode[32] = {};
  // Error codes: "CVLockFailed" | "PoolExhausted" | "ModelLoadFail"
  //              "NNAPIUnsupported" | "EnrollQualityFail" | "LSHNotReady"
};
