import { processFaceAuthFrame } from './FrameProcessorPlugin';

export function myFrameProcessor(frame) {
  'worklet';
  const internal = frame;
  internal.incrementRefCount();
  if (global.processVisionFrame) {
    const result = global.processVisionFrame(frame);
    if (result) {
      console.log("[FrameProcessor] FPS: " + result.nativeFps);
    }
  }
  internal.decrementRefCount();
}
