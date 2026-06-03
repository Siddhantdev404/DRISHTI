// hooks/useFaceAuthPlugin.ts
import { useEffect, useRef } from 'react';
import { VisionCameraProxy, type Frame } from 'react-native-vision-camera';
import type { FaceAuthResult } from '../../shared/FaceAuthResult';

// The plugin is registered by M1's JSIInstaller.mm / FaceAuthPackage.kt
// under the name 'drishti_face_auth'. This name must match M1's
// VisionCameraProxy.initFrameProcessorPlugin('drishti_face_auth') call.
const plugin = VisionCameraProxy.initFrameProcessorPlugin('drishti_face_auth', {});

export function useFaceAuthPlugin() {
  return {
    processFrame: (frame: Frame): FaceAuthResult => {
      'worklet';
      // Runs on VisionCamera's CameraQueue thread — NOT the JS thread.
      // Direct C++ call via JSI. Zero serialization. Zero bridge hop.
      const raw = plugin!.call(frame) as unknown as FaceAuthResult;
      return raw;
    }
  };
}
