/**
 * FaceAuthScreen.tsx
 *
 * Main biometric face authentication scanning screen for DRISHTI.
 *
 * Architecture flow:
 *   [Camera Sensor] → 60 FPS hardware thread
 *     → [processFaceAuthFrame worklet] → zero-copy Y-plane → C++ FrameMailbox
 *       → [C++ inference thread] → AdaptiveClahe → LivenessFSM → LSHIndex
 *         → [FaceAuthResult] → polled by useFaceAuth at ~20Hz
 *           → [React state] → drives UI overlay (this screen)
 *
 * LivenessFSM State Machine (from LivenessFSM.cpp):
 *   IDLE(0)                    → "Align your face"
 *   DETECTED(1)                → "Face detected, hold still"
 *   VARIANCE_CHECK(2)          → "Analyzing liveness..."
 *   CHALLENGE_ACTIVE(3)        → Dynamic prompt based on activeChallenge
 *   LIVENESS_PASS(4)           → "✅ Verified!"
 *   LIVENESS_FAIL(5)           → "❌ Challenge timed out"
 *   TEMPORAL_VARIANCE_FAIL(6)  → "❌ Liveness check failed"
 *
 * ChallengeType (randomly selected by LivenessFSM::pickChallenge):
 *   BLINK(1)       → "Blink slowly now"
 *   SMILE(2)       → "Please smile slightly"
 *   TURN_LEFT(3)   → "Turn your head left"
 *   TURN_RIGHT(4)  → "Turn your head right"
 */

import React, { useEffect, useCallback } from 'react';
import {
  StyleSheet,
  Text,
  View,
  ActivityIndicator,
  Pressable,
  useWindowDimensions,
} from 'react-native';
import { Camera, useCameraDevice, useCameraFormat, Frame } from 'react-native-vision-camera';
import { useCameraStream } from '../hooks/useCameraStream';
import { useFaceAuth } from '../hooks/useFaceAuth';
import { myFrameProcessor } from '../native/MyFrameProcessor';
import { LivenessState, ChallengeType, FaceAuthResultJS } from '../native/FaceAuthEngine';
import { DatabaseService } from '../database/DatabaseService';

// ─── Constants ───────────────────────────────────────────────────────────────

const RING_SIZE = 280;
const RING_BORDER = 4;

// ─── Component ───────────────────────────────────────────────────────────────

export default function FaceAuthScreen() {
  const {
    hasPermission,
    device,
    format,
    checkPermissionsDirectly,
  } = useCameraStream();

  const {
    // State
    isEngineRunning,
    livenessState,
    activeChallenge,
    matchScore,
    matchedId,
    embeddingReady,
    inferenceMs,
    nativeFps,
    errorCode,

    // Lifecycle
    startEngine,
    stopEngine,
    resetEngine,

    // Profile ops
    getEmbedding,
    registerProfile,
  } = useFaceAuth();

  // ── Boot: Start the C++ inference engine once camera device is available ──

  useEffect(() => {
    if (!device || !hasPermission) return;

    // Start the C++ thread when the camera screen opens
    const started = startEngine();
    if (started) {
      console.log('[DRISHTI] Inference engine started');
    }

    return () => {
      // Stop the thread when leaving the screen
      stopEngine();
    };
  }, [device, hasPermission, startEngine, stopEngine]);

  console.log("[DRISHTI] FaceAuthScreen rendered. global.processVisionFrame:", !!global.processVisionFrame);

  const frameProcessor = useMemo(() => {
    return {
      frameProcessor: myFrameProcessor,
      type: 'frame-processor'
    };
  }, []);

  useEffect(() => {
    console.log('[DEBUG] FrameProcessor asString:', (frameProcessor.frameProcessor as any).asString);
  }, [frameProcessor]);

  // ... [Other effects omitted for brevity, keeping original logic]

  useEffect(() => {
    if (livenessState !== LivenessState.LIVENESS_PASS) return;
    if (!embeddingReady) return;

    const embedding = getEmbedding();
    if (embedding) {
      if (matchedId && matchedId.length > 0 && matchScore > 0.7) {
        console.log(`[DRISHTI] Identity verified: ${matchedId} (score: ${matchScore.toFixed(3)})`);
      } else {
        console.log('[DRISHTI] Liveness passed — embedding ready for registration');
      }
    }
  }, [livenessState, embeddingReady, matchedId, matchScore, getEmbedding]);

  useEffect(() => {
    if (livenessState !== LivenessState.LIVENESS_FAIL && livenessState !== LivenessState.TEMPORAL_VARIANCE_FAIL) return;
    const timer = setTimeout(() => resetEngine(), 2500);
    return () => clearTimeout(timer);
  }, [livenessState, resetEngine]);

  const getChallengePrompt = useCallback((challenge: ChallengeType): string => {
    switch (challenge) {
      case ChallengeType.BLINK: return '👁️  Blink slowly now';
      case ChallengeType.SMILE: return '😊  Please smile slightly';
      case ChallengeType.TURN_LEFT: return '← Turn your head left';
      case ChallengeType.TURN_RIGHT: return 'Turn your head right →';
      default: return '';
    }
  }, []);

  const getLivenessPrompt = useCallback((state: LivenessState, challenge: ChallengeType): string => {
    switch (state) {
      case LivenessState.IDLE: return 'Align your face inside the circle';
      case LivenessState.DETECTED: return 'Face detected — hold still';
      case LivenessState.VARIANCE_CHECK: return 'Analyzing liveness…';
      case LivenessState.CHALLENGE_ACTIVE: return getChallengePrompt(challenge);
      case LivenessState.LIVENESS_PASS: return '✅  Verification Successful!';
      case LivenessState.LIVENESS_FAIL: return errorCode === 'ERR_TIMEOUT' ? '❌  Timed out — please try again' : '❌  Challenge failed — retrying';
      case LivenessState.TEMPORAL_VARIANCE_FAIL: return '❌  Liveness check failed';
      default: return 'Positioning camera…';
    }
  }, [getChallengePrompt, errorCode]);

  const getRingStyle = useCallback((state: LivenessState) => {
    switch (state) {
      case LivenessState.IDLE: return styles.ringIdle;
      case LivenessState.DETECTED:
      case LivenessState.VARIANCE_CHECK: return styles.ringScanning;
      case LivenessState.CHALLENGE_ACTIVE: return styles.ringChallenge;
      case LivenessState.LIVENESS_PASS: return styles.ringSuccess;
      case LivenessState.LIVENESS_FAIL:
      case LivenessState.TEMPORAL_VARIANCE_FAIL: return styles.ringFail;
      default: return styles.ringIdle;
    }
  }, []);

  if (!hasPermission) {
    return (
      <View style={styles.centered}>
        <Text style={styles.iconText}>🔒</Text>
        <Text style={styles.errorTitle}>Camera Access Required</Text>
        <Text style={styles.errorSubtext}>DRISHTI needs camera access for secure biometric authentication.</Text>
        <Pressable style={styles.retryButton} onPress={checkPermissionsDirectly}>
          <Text style={styles.retryButtonText}>Check Again</Text>
        </Pressable>
      </View>
    );
  }

  if (!device) {
    return (
      <View style={styles.centered}>
        <ActivityIndicator size="large" color="#00FFCC" />
        <Text style={styles.loadingText}>Initializing camera sensor…</Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      {/* Active Hardware Camera Stream Layer */}
      <View style={{ flex: 1, backgroundColor: 'black', justifyContent: 'center' }}>
        <Camera
          style={{ flex: 1 }}
          device={device}
          format={format}
          isActive={true}
          resizeMode="contain"
          frameProcessor={frameProcessor}
          pixelFormat="yuv"
        />
      </View>

      {/* Semi-transparent vignette overlay */}
      <View style={styles.overlayContainer} pointerEvents="none">
        {/* Dynamic Biometric Targeting Ring */}
        <View style={[styles.targetingRing, getRingStyle(livenessState)]}>
          {/* Inner pulse animation indicator for active scanning */}
          {(livenessState === LivenessState.VARIANCE_CHECK ||
            livenessState === LivenessState.CHALLENGE_ACTIVE) && (
            <View style={styles.innerPulse} />
          )}
        </View>

        {/* Real-time C++ FSM State Indicator Card */}
        <View style={styles.promptCard}>
          <Text style={styles.promptText}>
            {getLivenessPrompt(livenessState, activeChallenge)}
          </Text>
        </View>

        {/* Engine Telemetry Strip (bottom) */}
        {isEngineRunning && (
          <View style={styles.telemetryStrip}>
            <Text style={styles.telemetryText}>
              {inferenceMs.toFixed(1)}ms
            </Text>
            <View style={styles.telemetryDot} />
            <Text style={styles.telemetryText}>{Number(nativeFps).toFixed(1)} FPS</Text>
            <View style={styles.telemetryDot} />
            <Text style={styles.telemetryText}>
              {livenessState === LivenessState.LIVENESS_PASS && matchedId
                ? `ID: ${matchedId.substring(0, 8)}…`
                : 'Scanning'}
            </Text>
          </View>
        )}
      </View>
    </View>
  );
}

// ─── Styles ──────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#000',
  },

  // ── Fallback States ────────────────────────────────────────────────────
  centered: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: '#0A0A0F',
    paddingHorizontal: 32,
  },
  iconText: {
    fontSize: 48,
    marginBottom: 16,
  },
  errorTitle: {
    color: '#FFFFFF',
    fontSize: 20,
    fontWeight: '700',
    textAlign: 'center',
  },
  errorSubtext: {
    color: '#888',
    fontSize: 14,
    textAlign: 'center',
    marginTop: 8,
    lineHeight: 20,
  },
  retryButton: {
    marginTop: 24,
    backgroundColor: '#1A1A2E',
    paddingHorizontal: 28,
    paddingVertical: 12,
    borderRadius: 24,
    borderWidth: 1,
    borderColor: '#00FFCC',
  },
  retryButtonText: {
    color: '#00FFCC',
    fontSize: 15,
    fontWeight: '600',
  },
  loadingText: {
    color: '#666',
    fontSize: 14,
    marginTop: 16,
  },

  // ── Overlay ────────────────────────────────────────────────────────────
  overlayContainer: {
    ...StyleSheet.absoluteFillObject,
    justifyContent: 'center',
    alignItems: 'center',
  },

  // ── Targeting Ring ─────────────────────────────────────────────────────
  targetingRing: {
    width: RING_SIZE,
    height: RING_SIZE,
    borderRadius: RING_SIZE / 2,
    borderWidth: RING_BORDER,
    backgroundColor: 'transparent',
    marginBottom: 40,
    justifyContent: 'center',
    alignItems: 'center',
  },
  ringIdle: {
    borderColor: 'rgba(255, 255, 255, 0.3)',
    borderStyle: 'dashed' as const,
  },
  ringScanning: {
    borderColor: '#00FFCC',
    borderStyle: 'dashed' as const,
  },
  ringChallenge: {
    borderColor: '#FFD700',
    borderStyle: 'solid' as const,
  },
  ringSuccess: {
    borderColor: '#00FF88',
    borderStyle: 'solid' as const,
  },
  ringFail: {
    borderColor: '#FF3344',
    borderStyle: 'solid' as const,
  },

  // ── Inner Pulse (active scanning indicator) ────────────────────────────
  innerPulse: {
    width: RING_SIZE - 24,
    height: RING_SIZE - 24,
    borderRadius: (RING_SIZE - 24) / 2,
    borderWidth: 1,
    borderColor: 'rgba(0, 255, 204, 0.15)',
    backgroundColor: 'rgba(0, 255, 204, 0.03)',
  },

  // ── Prompt Card ────────────────────────────────────────────────────────
  promptCard: {
    backgroundColor: 'rgba(0, 0, 0, 0.85)',
    paddingHorizontal: 24,
    paddingVertical: 14,
    borderRadius: 25,
    borderWidth: 1,
    borderColor: 'rgba(255, 255, 255, 0.12)',
    maxWidth: 320,
  },
  promptText: {
    color: '#FFFFFF',
    fontSize: 16,
    fontWeight: '600',
    textAlign: 'center',
    letterSpacing: 0.3,
  },

  // ── Telemetry Strip ────────────────────────────────────────────────────
  telemetryStrip: {
    position: 'absolute',
    bottom: 40,
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: 'rgba(0, 0, 0, 0.7)',
    paddingHorizontal: 16,
    paddingVertical: 8,
    borderRadius: 16,
    borderWidth: 1,
    borderColor: 'rgba(255, 255, 255, 0.06)',
  },
  telemetryText: {
    color: 'rgba(255, 255, 255, 0.45)',
    fontSize: 11,
    fontFamily: 'monospace',
    fontWeight: '500',
  },
  telemetryDot: {
    width: 3,
    height: 3,
    borderRadius: 1.5,
    backgroundColor: 'rgba(255, 255, 255, 0.2)',
    marginHorizontal: 8,
  },
});
