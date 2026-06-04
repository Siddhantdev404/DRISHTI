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

import { VisionCameraProxy, type Frame } from 'react-native-vision-camera';

const plugin = VisionCameraProxy.initFrameProcessorPlugin('processFaceAuthFrame');

function parseOrientationToRotation(orientation: string): number {
  'worklet';
  switch (orientation) {
    case 'portrait': return 90;
    case 'landscape-right': return 0;
    case 'portrait-upside-down': return 270;
    case 'landscape-left': return 180;
    default: return 90;
  }
}

export function processFaceAuthFrame(frame: Frame): void {
  'worklet';

  if (plugin == null) {
    console.log("[DRISHTI] Failed to load Frame Processor Plugin processFaceAuthFrame!");
    return;
  }

  const rot = parseOrientationToRotation(frame.orientation);
  plugin.call(frame, { rotation: rot });
}
