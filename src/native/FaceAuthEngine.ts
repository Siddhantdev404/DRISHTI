/**
 * FaceAuthEngine.ts
 * 
 * Strict TypeScript JSI interface layer that wraps the global synchronous
 * functions injected by the C++ face_auth_plugin.cpp via installFaceAuth().
 * 
 * The native layer registers these directly onto the JS global object:
 *   startFaceAuthEngine, stopFaceAuthEngine, processVisionFrame,
 *   getFaceAuthResult, resetFaceAuthEngine, insertFaceProfile,
 *   removeFaceProfile, getFaceAuthStatus
 * 
 * All calls are synchronous JSI — NO async bridge, NO JSON serialization.
 */

// ─── Type Definitions ────────────────────────────────────────────────────────

import { NativeModules } from 'react-native';

const { FaceAuthModule } = NativeModules;

if (FaceAuthModule && typeof FaceAuthModule.install === 'function') {
  const installed = FaceAuthModule.install();
  console.log('[DRISHTI] FaceAuthModule.install() returned:', installed);
} else {
  console.error('[DRISHTI] FaceAuthModule not found in NativeModules! Cannot install JSI bindings.');
}

export enum LivenessState {
  IDLE = 0,
  DETECTED = 1,
  VARIANCE_CHECK = 2,
  CHALLENGE_ACTIVE = 3,
  LIVENESS_PASS = 4,
  LIVENESS_FAIL = 5,
  TEMPORAL_VARIANCE_FAIL = 6,
}

export enum ChallengeType {
  NONE = 0,
  BLINK = 1,
  SMILE = 2,
  TURN_LEFT = 3,
  TURN_RIGHT = 4,
}

/**
 * Mirrors the FaceAuthResult struct from shared/FaceAuthResult.h.
 * This is what getFaceAuthResult() returns over JSI.
 */
export interface FaceAuthResultJS {
  livenessState: LivenessState;
  activeChallenge: ChallengeType;
  matchScore: number;
  matchedId: string;
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
  errorCode: string;
  /** Raw 512-byte (128-float) embedding vector, only present when embeddingReady === true */
  embeddingBytes?: ArrayBuffer;
}

export interface FaceAuthStatusJS {
  engineRunning: boolean;
  indexReady: boolean;
  profileCount: number;
  mailboxHasFrame: boolean;
}

// ─── Global JSI declarations ─────────────────────────────────────────────────

declare global {
  /** Starts the background inference thread. Returns true on success. */
  function startFaceAuthEngine(): boolean;
  /** Stops the inference thread and joins. Returns true on success. */
  function stopFaceAuthEngine(): boolean;
  /**
   * Posts a raw Y-plane ArrayBuffer into the FrameMailbox for the inference thread.
   * @param frameBuffer - Y-plane pixel data as ArrayBuffer
   * @param width       - Frame width in pixels
   * @param height      - Frame height in pixels
   * @param stride      - Row stride in bytes
   * @returns true if the mailbox accepted the frame
   */
  function processVisionFrame(
    frameBuffer: ArrayBuffer,
    width: number,
    height: number,
    stride: number
  ): boolean;
  /** Reads the latest FaceAuthResult from the inference thread (lock-free). */
  function getFaceAuthResult(): FaceAuthResultJS;
  /** Resets the LivenessFSM, CLAHE, and mailbox. */
  function resetFaceAuthEngine(): boolean;
  /**
   * Inserts a face profile embedding into the LSHIndex for matching.
   * @param profileId  - Unique identifier (UUID) for the user
   * @param embedding  - 512-byte ArrayBuffer containing 128 float32 values
   */
  function insertFaceProfile(profileId: string, embedding: ArrayBuffer): boolean;
  /** Removes a profile from the LSHIndex by ID. */
  function removeFaceProfile(profileId: string): boolean;
  /** Returns engine and index health status. */
  function getFaceAuthStatus(): FaceAuthStatusJS;
}

// ─── Validation Helpers ──────────────────────────────────────────────────────

const EMBEDDING_BYTE_LENGTH = 512; // 128 floats × 4 bytes

function assertJSIAvailable(): void {
  if (typeof global.startFaceAuthEngine !== 'function') {
    throw new Error(
      '[DRISHTI] FaceAuth JSI bindings are not installed. ' +
      'Ensure FaceAuthModule.install() was called and libface_auth_engine.so loaded.'
    );
  }
}

// ─── Public API ──────────────────────────────────────────────────────────────

export const FaceAuthEngine = {
  // ── Lifecycle ────────────────────────────────────────────────────────────

  /**
   * Boots the background inference thread (CLAHE + LivenessFSM pipeline).
   * Safe to call multiple times — returns false if already running.
   */
  startEngine(): boolean {
    assertJSIAvailable();
    return global.startFaceAuthEngine();
  },

  /**
   * Gracefully stops the inference thread, joins, and drains the mailbox.
   */
  stopEngine(): boolean {
    assertJSIAvailable();
    return global.stopFaceAuthEngine();
  },

  /**
   * Resets the LivenessFSM state machine, CLAHE histogram, and frame mailbox
   * without tearing down the inference thread.
   */
  resetEngine(): boolean {
    assertJSIAvailable();
    return global.resetFaceAuthEngine();
  },

  // ── Frame Processing ─────────────────────────────────────────────────────

  /**
   * Posts a single Y-plane video frame into the FrameMailbox.
   * The inference thread picks it up asynchronously at ~60 FPS.
   * 
   * @param yPlaneBuffer - Raw Y-plane luminance data
   * @param width        - Frame width in pixels
   * @param height       - Frame height in pixels
   * @param stride       - Row stride in bytes (may differ from width due to padding)
   * @returns true if the mailbox accepted the frame
   */
  postFrame(
    yPlaneBuffer: ArrayBuffer,
    width: number,
    height: number,
    stride: number
  ): boolean {
    assertJSIAvailable();
    return global.processVisionFrame(yPlaneBuffer, width, height, stride);
  },

  /**
   * Reads the latest inference result snapshot from the C++ engine.
   * This is a synchronous read of the most recent FaceAuthResult struct.
   */
  getResult(): FaceAuthResultJS {
    assertJSIAvailable();
    return global.getFaceAuthResult();
  },

  /**
   * Convenience: reads result and extracts the embedding as a Float32Array
   * if a face embedding is ready. Returns null otherwise.
   */
  getEmbeddingVector(): Float32Array | null {
    const result = this.getResult();
    if (!result.embeddingReady || !result.embeddingBytes) {
      return null;
    }
    // Zero-copy view over the JSI ArrayBuffer — 128 × float32 = 512 bytes
    return new Float32Array(result.embeddingBytes);
  },

  // ── Profile Management (LSHIndex) ────────────────────────────────────────

  /**
   * Inserts a user's face embedding into the native LSHIndex for matching.
   * The embedding must be a Float32Array of exactly 128 dimensions (512 bytes).
   * 
   * @param profileId - Unique user identifier (UUID string)
   * @param embedding - 128-dimensional float vector
   */
  insertProfile(profileId: string, embedding: Float32Array): boolean {
    assertJSIAvailable();

    if (embedding.byteLength !== EMBEDDING_BYTE_LENGTH) {
      throw new Error(
        `[DRISHTI] Embedding must be exactly ${EMBEDDING_BYTE_LENGTH} bytes ` +
        `(128 floats). Received ${embedding.byteLength} bytes.`
      );
    }

    // Pass the underlying ArrayBuffer directly — zero-copy to C++ memcpy
    return global.insertFaceProfile(profileId, embedding.buffer);
  },

  /**
   * Removes a user profile from the native LSHIndex.
   */
  removeProfile(profileId: string): boolean {
    assertJSIAvailable();
    return global.removeFaceProfile(profileId);
  },

  // ── Status ───────────────────────────────────────────────────────────────

  /**
   * Returns engine health and index status for diagnostics.
   */
  getStatus(): FaceAuthStatusJS {
    assertJSIAvailable();
    return global.getFaceAuthStatus();
  },
};
