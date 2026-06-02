export const LEDGER_GENESIS_HASH: string =
  "0000000000000000000000000000000000000000000000000000000000000000";

export type LivenessState =
  | "IDLE"
  | "DETECTED"
  | "VARIANCE_CHECK"
  | "CHALLENGE_ACTIVE"
  | "LIVENESS_PASS"
  | "LIVENESS_FAIL"
  | "TEMPORAL_VARIANCE_FAIL";

export type ChallengeType =
  | "NONE"
  | "BLINK"
  | "SMILE"
  | "TURN_LEFT"
  | "TURN_RIGHT";

export type FaceAuthErrorCode =
  | ""
  | "ERR_NO_FACE"
  | "ERR_MULTI_FACE"
  | "ERR_LOW_QUALITY"
  | "ERR_LIVENESS_FAIL"
  | "ERR_MATCH_FAIL"
  | "ERR_TIMEOUT"
  | "ERR_MODEL_LOAD"
  | "ERR_INFERENCE"
  | "ERR_CAMERA"
  | "ERR_UNKNOWN";

export interface FaceAuthResult {
  livenessState: LivenessState;
  activeChallenge: ChallengeType;
  matchScore: number;
  matchedId: string;
  embeddingBytes: Uint8Array;
  embeddingReady: boolean;
  inferenceMs: number;
  ear: number;
  mar: number;
  yaw: number;
  pitch: number;
  tempVariance: number;
  frameCount: number;
  nativeFps: number;
  nativeHeapKB: number;
  errorCode: FaceAuthErrorCode;
}
