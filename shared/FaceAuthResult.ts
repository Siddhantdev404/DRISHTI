// FaceAuthResult.ts — mirrors FaceAuthResult.h exactly.
// FROZEN contract. Edit only via joint bridge PR.

export const LEDGER_GENESIS_HASH =
  '0000000000000000000000000000000000000000000000000000000000000000';

export type LivenessState =
  | 'IDLE'
  | 'DETECTED'
  | 'VARIANCE_CHECK'
  | 'CHALLENGE_ACTIVE'
  | 'LIVENESS_PASS'
  | 'LIVENESS_FAIL'
  | 'TEMPORAL_VARIANCE_FAIL';

export type ChallengeType = 'NONE' | 'BLINK' | 'SMILE' | 'TURN_LEFT' | 'TURN_RIGHT';

export type FaceAuthErrorCode =
  | ''                     // no error
  | 'CVLockFailed'
  | 'PoolExhausted'
  | 'ModelLoadFail'
  | 'NNAPIUnsupported'
  | 'EnrollQualityFail'
  | 'LSHNotReady';

export interface FaceAuthResult {
  // Primary output
  livenessState:    LivenessState;
  activeChallenge:  ChallengeType;
  matchScore:       number;        // [0.0, 1.0]
  matchedId:        string;        // UUID v4 or "" if no match

  // Enrollment (only valid when livenessState === 'LIVENESS_PASS')
  embeddingBytes:   Uint8Array;    // 512 bytes = 128 × float32
  embeddingReady:   boolean;

  // Debug / HUD
  inferenceMs:   number;
  ear:           number;
  mar:           number;
  yaw:           number;
  pitch:         number;
  tempVariance:  number;
  frameCount:    number;
  nativeFps:     number;
  nativeHeapKB:  number;

  // Error propagation — check this FIRST
  errorCode: FaceAuthErrorCode;
}
