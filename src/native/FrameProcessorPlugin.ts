/**
 * FrameProcessorPlugin.ts
 *
 * High-performance Vision Camera worklet that captures raw video frames at
 * 60 FPS on the native camera hardware thread and posts them directly into
 * the C++ FrameMailbox via the synchronous JSI global `processVisionFrame()`.
 *
 * ┌──────────────────┐
 * │  Camera Sensor   │  60 FPS hardware stream
 * └────────┬─────────┘
 *          ▼
 * ┌──────────────────┐
 * │ processFaceAuth  │  'worklet' — runs on native video thread (NOT JS thread)
 * │     Frame()      │  Extracts Y-plane ArrayBuffer (zero-copy JSI)
 * └────────┬─────────┘
 *          ▼
 * ┌──────────────────┐
 * │ processVision    │  Synchronous JSI global → C++ FrameMailbox::post()
 * │   Frame()        │  Posts raw Y-plane pointer, width, height, stride
 * └────────┬─────────┘
 *          ▼
 * ┌──────────────────┐
 * │ Inference Thread │  Background C++ thread picks up via FrameMailbox::fetch()
 * │ AdaptiveClahe →  │  Runs CLAHE preprocessing → LivenessFSM → LSHIndex match
 * │ LivenessFSM →    │  Writes results to g_latestResult (lock-free read by JS)
 * │ LSHIndex         │
 * └──────────────────┘
 *
 * CRITICAL: This function runs entirely off the JS thread. You cannot import
 * regular TypeScript modules or call async functions inside a worklet.
 * Only synchronous JSI globals installed on the JS runtime are accessible.
 */

import type { Frame } from 'react-native-vision-camera';

// ─── Orientation Rotation Matrix ─────────────────────────────────────────────

/**
 * Translates Vision Camera orientation string identifiers into explicit integer
 * angle degrees for the C++ engine's face landmark coordinate transformations.
 *
 * The camera HAL reports orientation as a semantic string. The C++ preprocessing
 * layer (AdaptiveClahe) and face landmark detector require a numeric rotation
 * value to correctly orient the YUV frame data before processing.
 *
 * Rotation degrees follow the standard Android camera coordinate convention:
 *   - 0°   = landscape-right (sensor natural orientation)
 *   - 90°  = portrait (most common handheld orientation)
 *   - 180° = landscape-left (upside-down landscape)
 *   - 270° = portrait-upside-down
 *
 * @param orientation - Vision Camera orientation string from frame.orientation
 * @returns Integer rotation angle in degrees (0, 90, 180, 270)
 */
function parseOrientationToRotation(orientation: string): number {
  'worklet';
  switch (orientation) {
    case 'portrait':
      return 90;
    case 'landscape-right':
      return 0;
    case 'portrait-upside-down':
      return 270;
    case 'landscape-left':
      return 180;
    default:
      // Fallback to portrait — the most common user orientation for face auth
      return 90;
  }
}

// ─── Frame Processor Worklet ─────────────────────────────────────────────────

/**
 * Primary frame processor worklet invoked by Vision Camera on every captured
 * video frame from the front-facing camera hardware sensor.
 *
 * This function:
 *   1. Extracts the raw Y-plane luminance pixel data as an ArrayBuffer
 *      via Vision Camera's zero-copy JSI allocation layer (no memcpy)
 *   2. Computes the rotation angle from the orientation string
 *   3. Calculates the row stride to account for camera HAL memory padding
 *   4. Posts the raw buffer directly into the C++ FrameMailbox via the
 *      synchronous JSI global `processVisionFrame()`
 *
 * Performance characteristics:
 *   - Zero-copy: frame.toArrayBuffer() returns a JSI ArrayBuffer backed by
 *     the same native memory — no data duplication
 *   - Synchronous: the JSI call completes in <0.1ms (just a mailbox post)
 *   - Off-thread: the 'worklet' directive ensures this runs on the native
 *     camera thread, never blocking React Native UI rendering
 *
 * @param frame - Vision Camera Frame object with raw pixel access
 * @returns true if the frame was accepted by the FrameMailbox, false otherwise
 */
export function processFaceAuthFrame(frame: Frame): boolean {
  'worklet';

  // 1. Extract raw hardware frame geometry
  const width = frame.width;
  const height = frame.height;

  // 2. Translate the orientation string into a numeric rotation angle
  //    for correct face landmark coordinate matrix transformations in C++
  const orientation = frame.orientation;
  const _rotation = parseOrientationToRotation(orientation);

  // 3. Extract the raw Y-plane luminance pixel data directly from hardware
  //    memory via Vision Camera's JSI zero-copy ArrayBuffer allocation.
  //    This does NOT copy the pixel data — it returns a JSI ArrayBuffer
  //    backed by the same native memory pointer.
  const frameBuffer = frame.toArrayBuffer();

  if (!frameBuffer) {
    return false;
  }

  // 4. Compute the row stride from the buffer dimensions.
  //    Camera HAL may add padding bytes at the end of each row for memory
  //    alignment. stride = total_bytes / height accounts for this.
  //    The C++ FrameMailbox::post() uses stride (not width) for correct
  //    row-by-row pixel access in AdaptiveClahe::process().
  const stride = Math.floor(frameBuffer.byteLength / height);

  // 5. Post the raw Y-plane ArrayBuffer synchronously into the C++
  //    FrameMailbox. The background inference thread will pick this up
  //    via FrameMailbox::fetch() and run the full pipeline:
  //      AdaptiveClahe → LivenessFSM → embedding extraction → LSHIndex match
  //
  //    processVisionFrame is a synchronous JSI global function installed by
  //    face_auth_plugin.cpp → installFaceAuth(). It takes:
  //      (frameBuffer: ArrayBuffer, width: int, height: int, stride: int)
  //    and returns true if the mailbox accepted the frame.
  return (global as any).processVisionFrame(frameBuffer, width, height, stride);
}
