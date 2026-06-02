/**
 * useFaceAuth.ts
 *
 * Reusable React hook that orchestrates the full lifecycle of the DRISHTI
 * face authentication engine:
 *   1. Engine startup/shutdown (C++ inference thread)
 *   2. Reactive polling of native inference results
 *   3. Profile registration and verification via LSHIndex
 *   4. UI-reactive state properties for component consumption
 *
 * This hook links the camera frame callback, JSI engine layer, and
 * component UI reactivity through the FaceAuthEngine wrapper.
 */

import { useState, useEffect, useCallback, useRef } from 'react';
import {
  FaceAuthEngine,
  LivenessState,
  ChallengeType,
  type FaceAuthResultJS,
  type FaceAuthStatusJS,
} from '../native/FaceAuthEngine';

// ─── Hook State Types ────────────────────────────────────────────────────────

export interface FaceAuthState {
  /** Whether the C++ inference thread is currently running */
  isEngineRunning: boolean;
  /** Whether result polling is active */
  isProcessing: boolean;
  /** Current liveness FSM state from the C++ engine */
  livenessState: LivenessState;
  /** Active challenge type being issued to the user */
  activeChallenge: ChallengeType;
  /** Latest cosine similarity match score from LSHIndex */
  matchScore: number;
  /** UUID of the matched profile, empty string if none */
  matchedId: string;
  /** Whether a face embedding vector is available to read */
  embeddingReady: boolean;
  /** Inference latency in milliseconds for the latest frame */
  inferenceMs: number;
  /** Eye Aspect Ratio — used for blink detection */
  ear: number;
  /** Mouth Aspect Ratio — used for smile detection */
  mar: number;
  /** Head yaw angle in degrees */
  yaw: number;
  /** Head pitch angle in degrees */
  pitch: number;
  /** Native-side processing FPS counter */
  nativeFps: number;
  /** Last error code from the engine, empty if none */
  errorCode: string;
}

const INITIAL_STATE: FaceAuthState = {
  isEngineRunning: false,
  isProcessing: false,
  livenessState: LivenessState.IDLE,
  activeChallenge: ChallengeType.NONE,
  matchScore: 0,
  matchedId: '',
  embeddingReady: false,
  inferenceMs: 0,
  ear: 0,
  mar: 0,
  yaw: 0,
  pitch: 0,
  nativeFps: 0,
  errorCode: '',
};

/** How often (ms) we poll getFaceAuthResult() from the native side */
const POLL_INTERVAL_MS = 50; // ~20 reads/sec — lightweight since it's synchronous JSI

// ─── Hook Implementation ─────────────────────────────────────────────────────

export function useFaceAuth() {
  const [state, setState] = useState<FaceAuthState>(INITIAL_STATE);
  const pollingRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const mountedRef = useRef<boolean>(true);

  // Track mount state to prevent setState after unmount
  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
    };
  }, []);

  // ── Result Polling ─────────────────────────────────────────────────────

  const startPolling = useCallback(() => {
    if (pollingRef.current !== null) return;

    pollingRef.current = setInterval(() => {
      if (!mountedRef.current) return;

      try {
        const result: FaceAuthResultJS = FaceAuthEngine.getResult();

        setState((prev) => ({
          ...prev,
          livenessState: result.livenessState,
          activeChallenge: result.activeChallenge,
          matchScore: result.matchScore,
          matchedId: result.matchedId,
          embeddingReady: result.embeddingReady,
          inferenceMs: result.inferenceMs,
          ear: result.ear,
          mar: result.mar,
          yaw: result.yaw,
          pitch: result.pitch,
          nativeFps: result.nativeFps,
          errorCode: result.errorCode,
        }));
      } catch (error) {
        console.error('[DRISHTI] Result polling error:', error);
      }
    }, POLL_INTERVAL_MS);
  }, []);

  const stopPolling = useCallback(() => {
    if (pollingRef.current !== null) {
      clearInterval(pollingRef.current);
      pollingRef.current = null;
    }
  }, []);

  // Clean up polling on unmount
  useEffect(() => {
    return () => {
      stopPolling();
    };
  }, [stopPolling]);

  // ── Engine Lifecycle ───────────────────────────────────────────────────

  /**
   * Starts the C++ inference thread and begins polling for results.
   * Call this after the camera is ready and permissions are granted.
   */
  const startEngine = useCallback((): boolean => {
    try {
      const success = FaceAuthEngine.startEngine();

      if (success && mountedRef.current) {
        setState((prev) => ({
          ...prev,
          isEngineRunning: true,
          isProcessing: true,
        }));
        startPolling();
      }

      return success;
    } catch (error) {
      console.error('[DRISHTI] Failed to start engine:', error);
      return false;
    }
  }, [startPolling]);

  /**
   * Stops the C++ inference thread, joins, and ceases polling.
   */
  const stopEngine = useCallback((): boolean => {
    try {
      stopPolling();
      const success = FaceAuthEngine.stopEngine();

      if (mountedRef.current) {
        setState((prev) => ({
          ...prev,
          isEngineRunning: false,
          isProcessing: false,
        }));
      }

      return success;
    } catch (error) {
      console.error('[DRISHTI] Failed to stop engine:', error);
      return false;
    }
  }, [stopPolling]);

  /**
   * Resets the LivenessFSM + CLAHE + mailbox without stopping the thread.
   * Useful for retrying a liveness challenge after failure.
   */
  const resetEngine = useCallback((): boolean => {
    try {
      const success = FaceAuthEngine.resetEngine();

      if (success && mountedRef.current) {
        setState((prev) => ({
          ...prev,
          livenessState: LivenessState.IDLE,
          activeChallenge: ChallengeType.NONE,
          matchScore: 0,
          matchedId: '',
          embeddingReady: false,
          errorCode: '',
        }));
      }

      return success;
    } catch (error) {
      console.error('[DRISHTI] Failed to reset engine:', error);
      return false;
    }
  }, []);

  // ── Embedding & Profile Management ─────────────────────────────────────

  /**
   * Extracts the latest face embedding vector as a Float32Array.
   * Returns null if no embedding is currently available.
   */
  const getEmbedding = useCallback((): Float32Array | null => {
    try {
      return FaceAuthEngine.getEmbeddingVector();
    } catch (error) {
      console.error('[DRISHTI] Failed to get embedding:', error);
      return null;
    }
  }, []);

  /**
   * Inserts a face profile into the native C++ LSHIndex for future matching.
   * 
   * @param profileId - Unique user UUID
   * @param embedding - 128-dimensional Float32Array (512 bytes)
   */
  const registerProfile = useCallback(
    (profileId: string, embedding: Float32Array): boolean => {
      try {
        return FaceAuthEngine.insertProfile(profileId, embedding);
      } catch (error) {
        console.error('[DRISHTI] Failed to register profile:', error);
        return false;
      }
    },
    []
  );

  /**
   * Removes a face profile from the native LSHIndex.
   */
  const removeProfile = useCallback((profileId: string): boolean => {
    try {
      return FaceAuthEngine.removeProfile(profileId);
    } catch (error) {
      console.error('[DRISHTI] Failed to remove profile:', error);
      return false;
    }
  }, []);

  /**
   * Reads engine diagnostic status (running, index ready, profile count).
   */
  const getStatus = useCallback((): FaceAuthStatusJS | null => {
    try {
      return FaceAuthEngine.getStatus();
    } catch (error) {
      console.error('[DRISHTI] Failed to get status:', error);
      return null;
    }
  }, []);

  // ── Cleanup on unmount ─────────────────────────────────────────────────

  useEffect(() => {
    return () => {
      // Stop polling and engine if component unmounts while active
      if (pollingRef.current !== null) {
        clearInterval(pollingRef.current);
        pollingRef.current = null;
      }

      try {
        const status = FaceAuthEngine.getStatus();
        if (status?.engineRunning) {
          FaceAuthEngine.stopEngine();
        }
      } catch {
        // Engine may not be initialized — safe to ignore
      }
    };
  }, []);

  return {
    // State
    ...state,

    // Engine lifecycle
    startEngine,
    stopEngine,
    resetEngine,

    // Embedding / profile operations
    getEmbedding,
    registerProfile,
    removeProfile,
    getStatus,
  };
}
