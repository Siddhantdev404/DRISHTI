/**
 * VerifyScreen.tsx
 *
 * Verification screen for DRISHTI.  Runs the same liveness pipeline as
 * FaceAuthScreen, then on LIVENESS_PASS reads matchScore + matchedId from
 * the native C++ LSHIndex result.
 *
 * Decision logic:
 *   matchScore >= MATCH_THRESHOLD AND matchedId non-empty → VERIFIED
 *   otherwise                                             → REJECTED
 *
 * On VERIFIED: writes an attendance record to SQLite and emits
 *   [DRISHTI_MATCH] and [DRISHTI_ATTENDANCE] logs.
 *
 * MATCH_THRESHOLD is a named constant — calibrate after collecting
 * real known-user / unknown-user score distributions.
 */

import React, { useEffect, useState, useRef, useCallback, useMemo } from 'react';
import {
  StyleSheet,
  Text,
  View,
  DeviceEventEmitter,
  Pressable,
} from 'react-native';
import { Camera, useFrameProcessor, VisionCameraProxy } from 'react-native-vision-camera';
import { useCameraStream } from '../hooks/useCameraStream';
import { useFaceAuth } from '../hooks/useFaceAuth';
import { LivenessState } from '../native/FaceAuthEngine';
import { DatabaseService } from '../database/DatabaseService';

// ─── Plugin (module-level singleton) ─────────────────────────────────────────
const faceAuthPlugin = VisionCameraProxy.initFrameProcessorPlugin('processFaceAuthFrame');

// ─── Threshold — calibrate after testing with real known/unknown pairs ────────
//     Start conservatively at 0.70 (same as FaceAuthScreen heuristic).
//     If legitimate users are rejected, lower toward 0.65.
//     If impostors pass, raise toward 0.75.
const MATCH_THRESHOLD = 0.70;

// ─── Helpers ─────────────────────────────────────────────────────────────────

function parseOrientationToRotation(orientation: string | undefined): number {
  'worklet';
  switch (orientation) {
    case 'portrait':              return 90;
    case 'landscape-right':       return 0;
    case 'portrait-upside-down':  return 270;
    case 'landscape-left':        return 180;
    default:                      return 90;
  }
}

function generateId(): string {
  return Date.now().toString(36) + Math.random().toString(36).slice(2).toUpperCase();
}

// ─── Types ────────────────────────────────────────────────────────────────────

type VerifyPhase = 'scanning' | 'verified' | 'rejected';

interface Props {
  onBack: () => void;
}

const RING_SIZE          = 280;
const RING_BORDER        = 4;

// ─── Component ────────────────────────────────────────────────────────────────

export default function VerifyScreen({ onBack }: Props) {
  const [verifyPhase, setVerifyPhase] = useState<VerifyPhase>('scanning');
  const [resultName,  setResultName]  = useState('');
  const [resultScore, setResultScore] = useState(0);
  const [isCameraReady, setIsCameraReady] = useState(false);

  // Prevent double-processing within a single liveness pass
  const processedRef = useRef(false);

  const { hasPermission, device, format, checkPermissionsDirectly } = useCameraStream();
  const {
    livenessState,
    embeddingReady,   // gates on MobileFaceNet+LSH completing (same race as EnrollmentScreen)
    matchScore,
    matchedId,
    startEngine,
    stopEngine,
    resetEngine,
  } = useFaceAuth();

  const cameraAspectRatio = useMemo(() => {
    const w = format?.videoWidth  ?? 4;
    const h = format?.videoHeight ?? 3;
    return Math.min(w, h) / Math.max(w, h);
  }, [format]);

  // ── Frame processor ────────────────────────────────────────────────────────

  const frameProcessor = useFrameProcessor((frame) => {
    'worklet';
    if (faceAuthPlugin == null) return;
    faceAuthPlugin.call(frame, { rotation: parseOrientationToRotation(frame.orientation) });
  }, []);

  // ── Engine lifecycle ────────────────────────────────────────────────────────

  useEffect(() => {
    // Delay camera activation to prevent lifecycle errors on tab switch
    const timer = setTimeout(() => setIsCameraReady(true), 150);
    return () => clearTimeout(timer);
  }, []);

  useEffect(() => {
    if (!device || !hasPermission) return;
    startEngine();
    return () => { stopEngine(); };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [device, hasPermission]);

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

  // ── Core verification logic on LIVENESS_PASS ───────────────────────────────

  useEffect(() => {
    if (livenessState !== LivenessState.LIVENESS_PASS) return;
    // ── Race guard: same as EnrollmentScreen ────────────────────────────────
    // matchScore and matchedId are set in InferencePipeline.cpp at the same
    // time as embeddingReady_ (lines 130–160). Without this guard, the first
    // poll tick can see LIVENESS_PASS with matchScore=0 → false REJECTED.
    if (!embeddingReady) return;
    if (processedRef.current) return;

    processedRef.current = true;

    const isMatch =
      matchScore >= MATCH_THRESHOLD &&
      typeof matchedId === 'string' &&
      matchedId.length > 0;

    console.log('[DRISHTI_MATCH_DEBUG]');
    console.log('candidate_id:', matchedId);
    console.log('score:', matchScore);
    console.log('threshold:', MATCH_THRESHOLD);

    // ── [DRISHTI_MATCH] structured log ──────────────────────────────────────
    console.log('[DRISHTI_MATCH]', JSON.stringify({
      candidate_id:  matchedId || 'none',
      match_score:   Number(matchScore).toFixed(4),
      threshold:     MATCH_THRESHOLD,
      result:        isMatch ? 'VERIFIED' : 'REJECTED',
    }));

    if (isMatch) {
      // Look up display name from DB (matchedId is the stored UUID)
      const user     = DatabaseService.getUserById(matchedId);
      const userName = user?.name ?? `ID:${matchedId.substring(0, 8)}`;

      // ── Write attendance record ────────────────────────────────────────────
      const attendanceId = generateId();
      const saved = DatabaseService.saveAttendance({
        id:                 attendanceId,
        userId:             matchedId,
        userName,
        timestamp:          Date.now(),
        confidence:         matchScore,
        livenessScore:      1.0,
        verificationResult: 'VERIFIED',
      });

      // ── [DRISHTI_ATTENDANCE] log ───────────────────────────────────────────
      console.log('[DRISHTI_ATTENDANCE]', JSON.stringify({
        saved,
        user:  userName,
        score: Number(matchScore).toFixed(3),
      }));

      if (saved) {
        DeviceEventEmitter.emit('attendanceUpdated');
      }

      setResultName(userName);
      setResultScore(matchScore);
      setVerifyPhase('verified');
    } else {
      setVerifyPhase('rejected');
    }

    // Auto-reset after 3 s so the next person can scan
    setTimeout(() => {
      processedRef.current = false;
      setVerifyPhase('scanning');
      resetEngine();
    }, 3000);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [livenessState, embeddingReady]);

  // ── Auto-recover from liveness failure ────────────────────────────────────

  useEffect(() => {
    if (
      livenessState !== LivenessState.LIVENESS_FAIL &&
      livenessState !== LivenessState.TEMPORAL_VARIANCE_FAIL
    ) return;

    const t = setTimeout(() => resetEngine(), 2500);
    return () => clearTimeout(t);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [livenessState]);

  // ─── Render helpers ────────────────────────────────────────────────────────

  function getLivenessPrompt(): string {
    switch (livenessState) {
      case LivenessState.IDLE:                   return 'Align your face in the camera';
      case LivenessState.DETECTED:               return 'Face detected — hold still';
      case LivenessState.VARIANCE_CHECK:         return 'Analyzing liveness…';
      case LivenessState.CHALLENGE_ACTIVE:       return 'Complete the liveness challenge';
      case LivenessState.LIVENESS_PASS:          return 'Matching…';
      case LivenessState.LIVENESS_FAIL:          return '❌  Challenge failed — retrying';
      case LivenessState.TEMPORAL_VARIANCE_FAIL: return '❌  Liveness check failed';
      default:                                    return 'Positioning…';
    }
  }

  // ─── Fallback screens ──────────────────────────────────────────────────────

  if (!hasPermission) {
    return (
      <View style={styles.centered}>
        <Text style={styles.errorText}>Camera permission required</Text>
        <Pressable style={styles.btn} onPress={checkPermissionsDirectly}>
          <Text style={styles.btnText}>Check Permission</Text>
        </Pressable>
      </View>
    );
  }

  if (!device) {
    return null;
  }

  // ─── Main render ──────────────────────────────────────────────────────────

  return (
    <View style={styles.container}>
      {/* Camera layer */}
      <View style={styles.previewStage}>
        {isCameraReady && (
          <Camera
            style={[styles.cameraPreview, { aspectRatio: cameraAspectRatio }]}
            device={device}
            format={format}
            isActive={true}
            resizeMode="contain"
            frameProcessor={frameProcessor}
            pixelFormat="yuv"
            exposure={0.25}
          />
        )}
      </View>

      {/* Overlay layer */}
      <View style={styles.overlayContainer} pointerEvents="none">

        {/* Dynamic Biometric Targeting Ring */}
        <View style={[styles.targetingRing, getRingStyle(livenessState)]}>
          {/* Inner pulse animation indicator for active scanning */}
          {(livenessState === LivenessState.VARIANCE_CHECK ||
            livenessState === LivenessState.CHALLENGE_ACTIVE) && (
            <View style={styles.innerPulse} />
          )}
        </View>

        {/* ── VERIFIED result ─────────────────────────────────────────── */}
        {verifyPhase === 'verified' && (
          <View style={[styles.resultOverlay, styles.verifiedBg]}>
            <Text style={styles.resultIcon}>✅</Text>
            <Text style={styles.verifiedLabel}>VERIFIED</Text>
            <Text style={styles.resultName}>{resultName}</Text>
            <Text style={styles.resultScore}>
              Match: {(resultScore * 100).toFixed(1)}%
            </Text>
            <Text style={styles.attendanceNote}>Attendance recorded</Text>
          </View>
        )}

        {/* ── REJECTED result ─────────────────────────────────────────── */}
        {verifyPhase === 'rejected' && (
          <View style={[styles.resultOverlay, styles.rejectedBg]}>
            <Text style={styles.resultIcon}>❌</Text>
            <Text style={styles.rejectedLabel}>REJECTED</Text>
            <Text style={styles.resultScore}>
              {matchScore > 0
                ? `Score: ${(matchScore * 100).toFixed(1)}% (below ${(MATCH_THRESHOLD * 100).toFixed(0)}% threshold)`
                : 'No matching profile found'}
            </Text>
          </View>
        )}

        {/* ── Scanning prompt ─────────────────────────────────────────── */}
        {verifyPhase === 'scanning' && (
          <View style={styles.promptCard}>
            <Text style={styles.promptText}>{getLivenessPrompt()}</Text>
          </View>
        )}
      </View>
    </View>
  );
}

// ─── Styles ───────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#000' },

  previewStage: {
    flex: 1,
    backgroundColor: '#000',
    justifyContent: 'center',
    alignItems: 'center',
    overflow: 'hidden',
  },
  cameraPreview: { width: '100%', maxHeight: '100%' },

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

  centered: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: '#0A0A0F',
  },
  errorText:   { color: '#FF4444', fontSize: 16, marginBottom: 16 },
  loadingText: { color: '#666',    fontSize: 14, marginTop: 12 },

  btn:     { backgroundColor: '#00FFCC', paddingHorizontal: 24, paddingVertical: 12, borderRadius: 12 },
  btnText: { color: '#000', fontWeight: '700', fontSize: 15 },

  overlayContainer: {
    ...StyleSheet.absoluteFillObject,
    justifyContent: 'center',
    alignItems: 'center',
  },

  // ── Result overlays ────────────────────────────────────────────────────
  resultOverlay: {
    ...StyleSheet.absoluteFillObject,
    justifyContent: 'center',
    alignItems: 'center',
    paddingHorizontal: 32,
  },
  verifiedBg: { backgroundColor: 'rgba(0,20,12,0.88)' },
  rejectedBg: { backgroundColor: 'rgba(20,0,4,0.88)' },

  resultIcon:     { fontSize: 72, marginBottom: 16 },
  verifiedLabel:  { color: '#00FF88', fontSize: 36, fontWeight: '800', letterSpacing: 2 },
  rejectedLabel:  { color: '#FF3344', fontSize: 36, fontWeight: '800', letterSpacing: 2 },
  resultName:     { color: '#FFF',    fontSize: 24, fontWeight: '600', marginTop: 8 },
  resultScore:    { color: '#AAA',    fontSize: 15, marginTop: 6, textAlign: 'center' },
  attendanceNote: { color: '#00FFCC', fontSize: 13, marginTop: 14, letterSpacing: 0.5 },

  // ── Scanning prompt ────────────────────────────────────────────────────
  promptCard: {
    position: 'absolute',
    bottom: 60,
    backgroundColor: 'rgba(0,0,0,0.85)',
    paddingHorizontal: 24,
    paddingVertical: 14,
    borderRadius: 25,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.12)',
    maxWidth: 320,
  },
  promptText: {
    color: '#FFF',
    fontSize: 16,
    fontWeight: '600',
    textAlign: 'center',
    letterSpacing: 0.3,
  },
});
