import { type Frame } from 'react-native-vision-camera';

// The actual return type matches FaceAuthResultJS from native/FaceAuthEngine
export interface FaceAuthResult {
  livenessState: number;
  activeChallenge: number;
  matchScore: number;
  matchedId: string;
  nativeFps: number;
  inferenceMs: number;
  frameCount: number;
  errorCode: string;
  // ... other properties
}

declare global {
  var processVisionFrame: (frame: Frame) => FaceAuthResult;
}

export function useFaceAuthPlugin() {
  return {
    processFrame: (frame: Frame): FaceAuthResult | null => {
      'worklet';
      if (typeof processVisionFrame !== 'undefined') {
        const result = processVisionFrame(frame);
        
        if (result) {
          const w = frame.width;
          const h = frame.height;
          const orient = frame.orientation;
          console.log(
            `[FrameProcessor] WxH: ${w}x${h} | ` +
            `Orientation: ${orient} | ` +
            `FPS: ${result.nativeFps} | ` +
            `Liveness: ${result.livenessState} | ` +
            `MatchScore: ${result.matchScore}`
          );
        }
        
        return result;
      }
      return null;
    }
  };
}
