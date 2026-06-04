/**
 * EnrollmentScreen.tsx
 *
 * Captures SAMPLE_TARGET (5) liveness-verified face embeddings, averages them,
 * L2-normalises, persists to SQLite, and inserts into the live C++ LSHIndex.
 *
 * ── Root-Cause Fix ───────────────────────────────────────────────────────────
 * Previous version used  useEffect([livenessState, embeddingReady]).
 * After capturing sample 1, resetEngine() is called.  If the C++ FSM cycles
 * IDLE → LIVENESS_PASS before the NEXT JS poll (50 ms window), the React
 * state never changes — deps stay LIVENESS_PASS/true — the effect NEVER
 * re-fires — samples 2-5 are permanently missed.
 *
 * Fix: Use a direct setInterval calling FaceAuthEngine.getResult() every 50 ms.
 * This is imperative — it fires on every tick regardless of whether React state
 * values changed — so the race is impossible.
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * DO NOT modify this file's native C++ calls or FaceAuthEngine.ts.
 */

import React, {
  useEffect, useState, useMemo, useRef, useCallback,
} from 'react';
import {
  StyleSheet, Text, View, TextInput, Pressable, ActivityIndicator,
} from 'react-native';
import { Camera, useFrameProcessor, VisionCameraProxy } from 'react-native-vision-camera';
import { useCameraStream } from '../hooks/useCameraStream';
import {
  FaceAuthEngine,
  LivenessState,
  ChallengeType,
} from '../native/FaceAuthEngine';
import { DatabaseService } from '../database/DatabaseService';

// ─── Plugin (module-level singleton) ─────────────────────────────────────────
const faceAuthPlugin = VisionCameraProxy.initFrameProcessorPlugin('processFaceAuthFrame');

// ─── Constants ────────────────────────────────────────────────────────────────
const SAMPLE_TARGET      = 5;    // number of liveness passes to average
const POLL_MS            = 50;   // match useFaceAuth polling rate
const POST_SAMPLE_DELAY  = 1200; // ms to pause after each capture (UI feedback)
const RING_SIZE          = 280;
const RING_BORDER        = 4;

// ─── Helpers ─────────────────────────────────────────────────────────────────

function parseOrientationToRotation(orientation: string | undefined): number {
  'worklet';
  switch (orientation) {
    case 'portrait':             return 90;
    case 'landscape-right':      return 0;
    case 'portrait-upside-down': return 270;
    case 'landscape-left':       return 180;
    default:                     return 90;
  }
}

function l2Normalize(vec: Float32Array): Float32Array {
  let sumSq = 0;
  for (let i = 0; i < vec.length; i++) sumSq += vec[i] * vec[i];
  const norm = Math.sqrt(sumSq);
  if (norm < 1e-8) return new Float32Array(vec);
  const out = new Float32Array(vec.length);
  for (let i = 0; i < vec.length; i++) out[i] = vec[i] / norm;
  return out;
}

function averageEmbeddings(samples: Float32Array[]): Float32Array {
  const len = samples[0].length;
  const avg = new Float32Array(len);
  for (const s of samples) {
    for (let i = 0; i < len; i++) avg[i] += s[i];
  }
  for (let i = 0; i < len; i++) avg[i] /= samples.length;
  return avg;
}

function generateId(): string {
  return Date.now().toString(36) + Math.random().toString(36).slice(2).toUpperCase();
}

function getLivenessPrompt(state: LivenessState, challenge: ChallengeType): string {
  switch (state) {
    case LivenessState.IDLE:             return 'Align your face in the camera';
    case LivenessState.DETECTED:         return 'Face detected — hold still';
    case LivenessState.VARIANCE_CHECK:   return 'Analysing liveness…';
    case LivenessState.CHALLENGE_ACTIVE:
      switch (challenge) {
        case ChallengeType.BLINK:      return '👁  Blink slowly';
        case ChallengeType.SMILE:      return '😊  Smile slightly';
        case ChallengeType.TURN_LEFT:  return '←  Turn your head left';
        case ChallengeType.TURN_RIGHT: return 'Turn your head right  →';
        default:                        return 'Follow the on-screen guide';
      }
    case LivenessState.LIVENESS_PASS:          return '✅  Sample captured!';
    case LivenessState.LIVENESS_FAIL:          return '❌  Challenge failed — try again';
    case LivenessState.TEMPORAL_VARIANCE_FAIL: return '❌  Liveness failed — retry';
    default:                                    return 'Positioning…';
  }
}

// ─── Types ────────────────────────────────────────────────────────────────────

type EnrollPhase = 'form' | 'capturing' | 'saving' | 'done' | 'error';

interface Props { onBack: () => void; }

// ─── Component ────────────────────────────────────────────────────────────────

export default function EnrollmentScreen({ onBack }: Props) {
  const [name,        setName]        = useState('');
  const [phase,       setPhase]       = useState<EnrollPhase>('form');
  const [sampleCount, setSampleCount] = useState(0);
  const [statusMsg,   setStatusMsg]   = useState('');
  const [livenessUI,  setLivenessUI]  = useState<LivenessState>(LivenessState.IDLE);
  const [challengeUI, setChallengeUI] = useState<ChallengeType>(ChallengeType.NONE);
  const [flashMsg,    setFlashMsg]    = useState(''); // shown for POST_SAMPLE_DELAY
  const [isCameraReady, setIsCameraReady] = useState(false);

  // Refs — accessible from setInterval closures without stale-closure issues
  const phaseRef    = useRef<EnrollPhase>('form');
  const nameRef     = useRef('');
  const samplesRef  = useRef<Float32Array[]>([]);
  const captured    = useRef(false);   // guards double-capture in one pass
  const isSaving    = useRef(false);
  const loopRef     = useRef<ReturnType<typeof setInterval> | null>(null);

  useEffect(() => { phaseRef.current = phase; }, [phase]);
  useEffect(() => { nameRef.current  = name;  }, [name]);

  // ── Camera ────────────────────────────────────────────────────────────────
  const { hasPermission, device, format, checkPermissionsDirectly } = useCameraStream();

  const cameraAspectRatio = useMemo(() => {
    const w = format?.videoWidth  ?? 4;
    const h = format?.videoHeight ?? 3;
    return Math.min(w, h) / Math.max(w, h);
  }, [format]);

  const frameProcessor = useFrameProcessor((frame) => {
    'worklet';
    if (faceAuthPlugin == null) return;
    faceAuthPlugin.call(frame, { rotation: parseOrientationToRotation(frame.orientation) });
  }, []);

  // Delay camera activation to prevent lifecycle errors on tab switch
  useEffect(() => {
    const timer = setTimeout(() => setIsCameraReady(true), 150);
    return () => clearTimeout(timer);
  }, []);

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

  // ── Save all 5 samples ────────────────────────────────────────────────────

  const saveEnrollment = useCallback((samples: Float32Array[]) => {
    if (isSaving.current) return;
    isSaving.current = true;
    setPhase('saving');
    phaseRef.current = 'saving';

    // ── Average ──────────────────────────────────────────────────────────
    console.log('[DRISHTI_ENROLL] Averaging embeddings');
    const averaged = averageEmbeddings(samples);

    // ── Normalize ────────────────────────────────────────────────────────
    console.log('[DRISHTI_ENROLL] Normalizing embedding');
    const normalized = l2Normalize(averaged);

    const userId   = generateId();
    const userName = nameRef.current.trim();

    // ── SQLite save ──────────────────────────────────────────────────────
    console.log('[DRISHTI_ENROLL] Saving user: "' + userName + '"');
    let saved = false;
    try {
      saved = DatabaseService.registerUser(userId, userName, normalized);
    } catch (e) {
      console.error('[DRISHTI_ENROLL] registerUser threw:', e);
    }

    if (!saved) {
      console.error('[DRISHTI_ENROLL] SQLite save FAILED');
      setPhase('error');
      setStatusMsg('Database write failed — check Logcat.');
      isSaving.current = false;
      return;
    }

    console.log('[DRISHTI_ENROLL] User saved successfully');

    // ── Verify SQLite write ───────────────────────────────────────────────
    const allEmbeddings = DatabaseService.getAllEmbeddings();
    console.log('[DRISHTI_DB] User count: ' + allEmbeddings.length);
    console.log('[DRISHTI_DB] User inserted: id=' + userId + ' name="' + userName + '"');

    // ── Insert into live LSH (so this session can verify immediately) ─────
    console.log('[DRISHTI_ENROLL] Inserting into LSH: id=' + userId);
    try {
      const inserted = FaceAuthEngine.insertProfile(userId, normalized);
      if (inserted) {
        console.log('[DRISHTI_ENROLL] Inserted into LSH ✓');
      } else {
        console.warn('[DRISHTI_ENROLL] insertProfile returned false');
      }
    } catch (e) {
      console.error('[DRISHTI_ENROLL] LSH insertion threw:', e);
    }

    // LSH size = DB count (C++ getFaceAuthStatus().profileCount is hardcoded 0)
    console.log('[DRISHTI_LSH] Profiles loaded: ' + allEmbeddings.length);

    setPhase('done');
  }, []);

  // ── Main capture loop (direct native polling) ─────────────────────────────

  useEffect(() => {
    if (phase !== 'capturing') return;
    if (!device || !hasPermission) {
      setStatusMsg('Camera not ready — check permissions.');
      return;
    }

    // Reset capture state on (re-)start
    samplesRef.current = [];
    captured.current   = false;
    isSaving.current   = false;
    setSampleCount(0);
    setFlashMsg('');
    setStatusMsg('Align your face and complete the liveness challenge');
    setLivenessUI(LivenessState.IDLE);
    setChallengeUI(ChallengeType.NONE);

    try { FaceAuthEngine.startEngine(); }
    catch (e) {
      console.error('[DRISHTI_ENROLL] startEngine failed:', e);
      setPhase('error');
      setStatusMsg('Engine start failed: ' + String(e));
      return;
    }

    // ── Direct imperative polling ──────────────────────────────────────────
    // We call FaceAuthEngine.getResult() every 50 ms directly.
    // This is NOT subject to the React state-dep unchanged race:
    //   "After reset, C++ cycles IDLE→PASS before next JS poll
    //    → React state stays at PASS/true → useEffect deps never change
    //    → effect never re-fires → samples 2-5 silently missed."
    // The interval fires unconditionally every tick and inspects native state.

    loopRef.current = setInterval(() => {
      if (phaseRef.current !== 'capturing') return;
      if (captured.current) return; // waiting for reset after this pass

      let result;
      try { result = FaceAuthEngine.getResult(); }
      catch { return; }

      // Live UI update (non-blocking)
      setLivenessUI(result.livenessState);
      setChallengeUI(result.activeChallenge);

      // Wait for liveness PASS + embedding ready (dual-condition, no dep race)
      if (result.livenessState !== LivenessState.LIVENESS_PASS) return;
      if (!result.embeddingReady) return;

      // Lock this pass immediately
      captured.current = true;

      // Get embedding from native engine
      const emb = FaceAuthEngine.getEmbeddingVector();
      if (!emb || emb.byteLength !== 512) {
        console.warn(
          '[DRISHTI_ENROLL] Embedding invalid (byteLength=' + (emb?.byteLength ?? 0) + '), retrying'
        );
        captured.current = false;
        try { FaceAuthEngine.resetEngine(); } catch {}
        return;
      }

      // Clone immediately — JSI ArrayBuffer is a view into C++ memory
      const cloned     = new Float32Array(emb);
      const newSamples = [...samplesRef.current, cloned];
      samplesRef.current = newSamples;
      const count        = newSamples.length;

      setSampleCount(count);
      console.log('[DRISHTI_ENROLL] Sample ' + count + '/' + SAMPLE_TARGET);

      if (count < SAMPLE_TARGET) {
        // Show flash, pause, reset engine for next pass
        setFlashMsg('Sample ' + count + '/' + SAMPLE_TARGET + ' captured ✓');
        setStatusMsg('Hold steady for sample ' + (count + 1) + '/' + SAMPLE_TARGET);

        setTimeout(() => {
          if (phaseRef.current !== 'capturing') return;
          captured.current = false;
          setFlashMsg('');
          setStatusMsg('Align face — sample ' + (count + 1) + '/' + SAMPLE_TARGET);
          try { FaceAuthEngine.resetEngine(); } catch {}
        }, POST_SAMPLE_DELAY);

      } else {
        // All SAMPLE_TARGET samples done — save
        setFlashMsg('All ' + SAMPLE_TARGET + '/5 samples captured ✓');
        setStatusMsg('Saving profile…');
        const snapshot = [...newSamples];
        setTimeout(() => saveEnrollment(snapshot), 300);
      }
    }, POLL_MS);

    return () => {
      if (loopRef.current !== null) {
        clearInterval(loopRef.current);
        loopRef.current = null;
      }
      try { FaceAuthEngine.stopEngine(); } catch {}
    };
  // saveEnrollment is stable (useCallback([])); device/hasPermission are stable
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [phase, device, hasPermission, saveEnrollment]);

  // ─── Render: Form ─────────────────────────────────────────────────────────

  if (phase === 'form') {
    return (
      <View style={styles.container}>
        <View style={styles.header}>
          <Pressable onPress={onBack} style={styles.backBtn} hitSlop={12}>
            <Text style={styles.backText}>← Back</Text>
          </Pressable>
          <Text style={styles.title}>Enroll New User</Text>
          <View style={{ width: 60 }} />
        </View>

        <View style={styles.formBody}>
          <View style={styles.iconBadge}>
            <Text style={styles.iconBadgeText}>👤</Text>
          </View>
          <Text style={styles.formHeading}>Register Your Face</Text>
          <Text style={styles.formSub}>
            We'll capture {SAMPLE_TARGET} independent liveness challenges, average
            the embeddings, and save a stable L2-normalised template.
          </Text>

          <Text style={styles.label}>FULL NAME</Text>
          <TextInput
            style={styles.input}
            value={name}
            onChangeText={setName}
            placeholder="Enter name…"
            placeholderTextColor="#333"
            autoCapitalize="words"
            returnKeyType="done"
            maxLength={64}
          />

          <Pressable
            style={[styles.primaryBtn, !name.trim() && styles.btnDisabled]}
            onPress={() => { if (name.trim()) setPhase('capturing'); }}
            disabled={!name.trim()}
          >
            <Text style={styles.primaryBtnText}>Start Enrollment →</Text>
          </Pressable>
        </View>
      </View>
    );
  }

  // ─── Render: Saving ───────────────────────────────────────────────────────

  if (phase === 'saving') {
    return (
      <View style={[styles.container, styles.centered]}>
        <ActivityIndicator size="large" color="#00FFCC" />
        <Text style={[styles.resultTitle, { marginTop: 24 }]}>Saving Profile…</Text>
        <Text style={styles.resultSub}>Averaging · Normalising · Persisting</Text>
      </View>
    );
  }

  // ─── Render: Done ─────────────────────────────────────────────────────────

  if (phase === 'done') {
    return (
      <View style={[styles.container, styles.centered]}>
        <View style={styles.resultCard}>
          <Text style={styles.resultIcon}>✅</Text>
          <Text style={styles.resultTitle}>Enrolled!</Text>
          <Text style={styles.resultName}>{name.trim()}</Text>
          <Text style={styles.resultSub}>
            {SAMPLE_TARGET} liveness samples · averaged · L2-normalised{'\n'}
            Saved to SQLite + LSH
          </Text>
          <Pressable style={styles.primaryBtn} onPress={onBack}>
            <Text style={styles.primaryBtnText}>Done</Text>
          </Pressable>
        </View>
      </View>
    );
  }

  // ─── Render: Error ────────────────────────────────────────────────────────

  if (phase === 'error') {
    return (
      <View style={[styles.container, styles.centered]}>
        <View style={styles.resultCard}>
          <Text style={styles.resultIcon}>❌</Text>
          <Text style={styles.resultTitle}>Enrollment Failed</Text>
          <Text style={styles.resultSub}>{statusMsg}</Text>
          <Pressable style={styles.primaryBtn} onPress={() => {
            samplesRef.current = [];
            captured.current   = false;
            isSaving.current   = false;
            setSampleCount(0);
            setPhase('form');
          }}>
            <Text style={styles.primaryBtnText}>Try Again</Text>
          </Pressable>
        </View>
      </View>
    );
  }

  // ─── Render: Camera (capturing) ───────────────────────────────────────────

  if (!hasPermission) {
    return (
      <View style={[styles.container, styles.centered]}>
        <Text style={styles.errorText}>Camera permission required</Text>
        <Pressable style={styles.primaryBtn} onPress={checkPermissionsDirectly}>
          <Text style={styles.primaryBtnText}>Grant Permission</Text>
        </Pressable>
      </View>
    );
  }

  if (!device) {
    return (
      <View style={[styles.container, styles.centered]}>
        <ActivityIndicator size="large" color="#00FFCC" />
        <Text style={styles.loadingText}>Initialising camera…</Text>
      </View>
    );
  }

  const livenessPrompt = getLivenessPrompt(livenessUI, challengeUI);

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
        <View style={[styles.targetingRing, getRingStyle(livenessUI)]}>
          {/* Inner pulse animation indicator for active scanning */}
          {(livenessUI === LivenessState.VARIANCE_CHECK ||
            livenessUI === LivenessState.CHALLENGE_ACTIVE) && (
            <View style={styles.innerPulse} />
          )}
        </View>

        {/* User name bar */}
        <View style={styles.nameBar}>
          <Text style={styles.nameBarText} numberOfLines={1}>
            Enrolling: {name.trim()}
          </Text>
        </View>

        {/* Sample progress dots */}
        <View style={styles.progressRow}>
          {Array.from({ length: SAMPLE_TARGET }).map((_, i) => (
            <View
              key={i}
              style={[styles.dot, i < sampleCount ? styles.dotFilled : styles.dotEmpty]}
            />
          ))}
        </View>

        <Text style={styles.countLabel}>
          {sampleCount} / {SAMPLE_TARGET} samples
        </Text>

        {/* Flash confirmation or liveness prompt */}
        {flashMsg ? (
          <View style={[styles.promptCard, styles.promptFlash]}>
            <Text style={[styles.promptText, styles.promptFlashText]}>{flashMsg}</Text>
          </View>
        ) : (
          <View style={styles.promptCard}>
            <Text style={styles.promptText}>{livenessPrompt}</Text>
          </View>
        )}

        {/* Sub-status */}
        {statusMsg ? (
          <Text style={styles.statusSub} numberOfLines={2}>{statusMsg}</Text>
        ) : null}
      </View>
    </View>
  );
}

// ─── Styles ───────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  container:     { flex: 1, backgroundColor: '#0A0A0F' },
  centered:      { justifyContent: 'center', alignItems: 'center', padding: 32 },

  // ── Header ──────────────────────────────────────────────────────────────
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 20,
    paddingTop: 16,
    paddingBottom: 12,
    borderBottomWidth: 1,
    borderBottomColor: 'rgba(255,255,255,0.06)',
  },
  backBtn:  { paddingRight: 12, paddingVertical: 8 },
  backText: { color: '#00FFCC', fontSize: 15, fontWeight: '600' },
  title:    { color: '#FFF', fontSize: 18, fontWeight: '700', flex: 1, textAlign: 'center' },

  // ── Form ────────────────────────────────────────────────────────────────
  formBody: { flex: 1, paddingHorizontal: 28, paddingTop: 32 },
  iconBadge: {
    width: 72, height: 72, borderRadius: 36,
    backgroundColor: 'rgba(0,255,204,0.08)',
    borderWidth: 1, borderColor: 'rgba(0,255,204,0.25)',
    alignItems: 'center', justifyContent: 'center',
    marginBottom: 20,
  },
  iconBadgeText: { fontSize: 34 },
  formHeading: { color: '#FFF', fontSize: 22, fontWeight: '700', marginBottom: 8 },
  formSub:     { color: '#555', fontSize: 13, lineHeight: 19, marginBottom: 32 },

  label: { color: '#00FFCC', fontSize: 11, fontWeight: '700', letterSpacing: 1.5, marginBottom: 8 },
  input: {
    backgroundColor: '#12121A',
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.1)',
    borderRadius: 14,
    paddingHorizontal: 18,
    paddingVertical: 14,
    color: '#FFF',
    fontSize: 16,
    marginBottom: 28,
  },

  // ── Buttons ─────────────────────────────────────────────────────────────
  primaryBtn: {
    backgroundColor: '#00FFCC',
    paddingVertical: 16,
    borderRadius: 16,
    alignItems: 'center',
    marginTop: 8,
  },
  btnDisabled: { opacity: 0.35 },
  primaryBtnText: { color: '#000', fontWeight: '800', fontSize: 16, letterSpacing: 0.3 },

  // ── Result screens ───────────────────────────────────────────────────────
  resultCard: { alignItems: 'center', padding: 32 },
  resultIcon:  { fontSize: 72, marginBottom: 16 },
  resultTitle: { color: '#FFF', fontSize: 26, fontWeight: '800', marginBottom: 8, textAlign: 'center' },
  resultName:  { color: '#00FFCC', fontSize: 20, fontWeight: '600', marginBottom: 12 },
  resultSub:   { color: '#555', fontSize: 13, lineHeight: 20, textAlign: 'center', marginBottom: 28 },
  errorText:   { color: '#FF4444', fontSize: 16, marginBottom: 16, textAlign: 'center' },
  loadingText: { color: '#555', fontSize: 14, marginTop: 14 },

  // ── Camera overlay ───────────────────────────────────────────────────────
  previewStage: {
    flex: 1, backgroundColor: '#000',
    justifyContent: 'center', alignItems: 'center', overflow: 'hidden',
  },
  cameraPreview: { width: '100%', maxHeight: '100%' },

  overlayContainer: {
    ...StyleSheet.absoluteFillObject,
    alignItems: 'center',
    justifyContent: 'center',
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

  nameBar: {
    marginTop: 16,
    backgroundColor: 'rgba(0,0,0,0.75)',
    paddingHorizontal: 18,
    paddingVertical: 8,
    borderRadius: 20,
    borderWidth: 1,
    borderColor: 'rgba(0,255,204,0.2)',
    maxWidth: '80%',
  },
  nameBarText: { color: '#00FFCC', fontSize: 13, fontWeight: '600' },

  progressRow: {
    flexDirection: 'row',
    marginTop: 20,
    gap: 12,
  },
  dot: { width: 14, height: 14, borderRadius: 7 },
  dotEmpty:  { backgroundColor: 'rgba(255,255,255,0.15)' },
  dotFilled: { backgroundColor: '#00FFCC' },

  countLabel: { color: '#AAA', fontSize: 12, marginTop: 8, fontWeight: '600' },

  promptCard: {
    position: 'absolute',
    bottom: 70,
    backgroundColor: 'rgba(0,0,0,0.85)',
    paddingHorizontal: 24,
    paddingVertical: 14,
    borderRadius: 25,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.12)',
    maxWidth: 320,
  },
  promptFlash: {
    backgroundColor: 'rgba(0,30,20,0.92)',
    borderColor: 'rgba(0,255,204,0.4)',
  },
  promptText:      { color: '#FFF', fontSize: 16, fontWeight: '600', textAlign: 'center' },
  promptFlashText: { color: '#00FFCC' },

  statusSub: {
    position: 'absolute',
    bottom: 28,
    color: '#666',
    fontSize: 12,
    textAlign: 'center',
    maxWidth: 280,
  },
});
