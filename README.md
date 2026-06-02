# Project DRISHTI — Offline Biometric Authentication for NHAI Field Operations

> NHAI Innovation Hackathon 7.0 Submission · Solo Execution Branch · June 2026

A fully offline, cryptographically auditable, DPDPA-compliant facial recognition and liveness detection system for field personnel authentication. Runs entirely on-device with zero network dependency, syncing automatically to AWS once secure connectivity is restored.

---

## Performance Metrics & Evaluation Baseline

| Metric | NHAI Requirement | DRISHTI Production Target | Evaluation Baseline (Snapdragon 665) |
| :--- | :--- | :--- | :--- |
| **End-to-End Latency** | < 1000ms | **P95 < 215ms** | Zero-Copy JSI Interface + NEON SIMD Interleaving |
| **Binary Footprint** | ~20 MB Max | **18.2 MB Total** | FP16 FaceMesh (3.0MB) + INT8 QAT MobileFaceNet (5.2MB) |
| **UI Rendering Rate** | Consistent UI | **58–60 FPS** | Background C++ Thread Offloading via Mailbox |
| **Memory Architecture** | Leak-Free Runtime | **< 42 MB Static Heap** | Real-time CVPixelBuffer Heap Pruning |
| **Demographic Accuracy** | Ambient Diversity | **> 95% Accuracy** | 8-Frame Ambient-Adaptive CLAHE Filtering |
| **Anti-Spoof Rejection** | Presentation Security | **< 2s Passive Reject** | Temporal Gradient Microsaccade Tracking |

---

## System Architecture

```
Camera Frame Ingestion (YUV Buffer)
│
▼ (Zero-Copy JSI Allocation Wall)
[C++ Native Inference Engine]
│
├── AdaptiveCLAHE       (Luminance-Normalized Contrast Filter) [<1.5ms]
├── FaceMesh v2         (468 landmarks, FP16 XNNPACK Architecture) [~95ms]
├── LivenessFSM         (6-State Micro-Saccade Variance Engine) [<2ms]
└── MobileFaceNet INT8  (128-D Quantized Embedding Generation) [~80ms]
│
▼ (Deterministic LSH Index Layer) [<5ms]
┌──────────────────────────────────────────────────────────────────────────────────────┐
│                             React Native Application Shell                           │
│  ├── Shopify Skia Overlay Ring  (Direct UI Thread Shared Value Interleaved Driver)   │
│  ├── SQLCipher Local Ledger     (Cryptographic Tamper-Evident Chronological Chain)   │
│  └── NDJSON Sync Processing     (Pre-signed Multipart Conditional S3 Cloud Gateway) │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Local Development & Environment Setup (Windows Host)

### Prerequisites

- **Runtime Environments:** Node.js 20+, JDK 17
- **Android SDK Pipeline:** Android Studio Hedgehog+, NDK r26b, CMake 3.22.1
- **Physical Hardware:** ARM64-v8a compatible Android 8.0+ device with USB Debugging enabled

### Step-by-Step System Assembly

```bash
# 1. Install Node ecosystem structures
npm install

# 2. Extract and link pre-trained network weights
node scripts/fetch_models.js

# 3. Clean and verify local project Gradle wrappers
cd android && gradlew clean && cd ..

# 4. Compile and deploy size-optimized release package to device
npx react-native run-android --variant=release
```

---

## Operational Security Primitives

**Hardware-Backed Cryptographic Isolation:**
Biometric embeddings are encrypted via AES-256-GCM using Data Encryption Keys (DEK) wrapped directly inside Android StrongBox. Under DPDPA 2023 guidelines, a Right-to-Erasure execution completely purges the key material from the hardware security layer in O(1) time, rendering stored database arrays permanently unrecoverable.

**Tamper-Evident Clock Chain:**
Offline attendance ledger events are appended using sequential hash linking bound to the device hardware clock (`elapsedRealtime()`). Manipulating the system wall clock exposes anomalies during AWS server ingestion by creating an inversion between chronological time and monotonic hardware counters.

---

## Directory Structure

```
DRISHTI/
├── .github/workflows/          # CI/CD pipelines (Android build + JS lint)
├── _tests_/                    # Test suites + mock frame headers
├── android/                    # Native Android + C++ inference engine
│   └── app/src/main/
│       ├── cpp/                # JNI bridge, liveness FSM, LSH matcher
│       └── java/com/drishti/   # Kotlin modules (Keystore, Uptime, Package)
├── models/                     # TFLite weights + quantization scripts
├── shared/                     # Cross-layer contracts (C++ headers + TS types)
├── src/
│   ├── components/             # Skia UI overlays (Ring, HUD, SyncBar)
│   ├── crypto/                 # Enrollment, erasure, enterprise key ops
│   ├── hooks/                  # React hooks for plugin + liveness state
│   ├── native/                 # JS wrappers for native Kotlin modules
│   ├── screens/                # App screens (Consent, Enroll, Verify, Ledger, Admin)
│   └── storage/                # SQLCipher DB, ledger logic, schema migrations
├── LICENSES.md
├── package.json
└── README.md
```

---

## Compliance & Regulatory Alignment

| Standard | Mechanism |
| :--- | :--- |
| **DPDPA 2023** | StrongBox DEK erasure, consent screen with explicit opt-in |
| **ISO/IEC 30107-3** | Passive liveness via microsaccade temporal variance |
| **NHAI Audit Trail** | Hash-chained offline ledger with monotonic clock binding |
| **Data Minimisation** | 128-D embeddings only — no raw biometric images persisted |

---

## License

See `LICENSES.md` for third-party attributions including TensorFlow Lite, SQLCipher, and Shopify Skia.
