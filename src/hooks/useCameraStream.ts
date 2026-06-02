/**
 * useCameraStream.ts
 *
 * Custom React hook that manages the complete Vision Camera hardware lifecycle
 * for DRISHTI biometric face authentication:
 *
 *   1. Requests camera permissions on mount (one-time system prompt)
 *   2. Discovers all available camera devices via Vision Camera JSI layer
 *   3. Isolates the front-facing sensor profile for face capture
 *   4. Exposes reactive state for UI binding (hasPermission, device)
 *   5. Provides an imperative permission refresh function for settings return flow
 *
 * Usage in a component:
 *   const { hasPermission, device, checkPermissionsDirectly } = useCameraStream();
 *
 *   if (!hasPermission) return <PermissionDeniedScreen onRetry={checkPermissionsDirectly} />;
 *   if (!device) return <NoCameraScreen />;
 *
 *   return (
 *     <Camera
 *       device={device}
 *       isActive={true}
 *       frameProcessor={processFaceAuthFrame}
 *       pixelFormat="yuv"
 *     />
 *   );
 */

import { useState, useEffect, useCallback } from 'react';
import {
  Camera,
  useCameraDevices,
  type CameraDevice,
} from 'react-native-vision-camera';

// ─── Hook Return Type ────────────────────────────────────────────────────────

export interface CameraStreamState {
  /**
   * Whether the user has granted camera access.
   * This is initialized from the system permission check on mount
   * and can be refreshed via checkPermissionsDirectly().
   */
  hasPermission: boolean;

  /**
   * The front-facing CameraDevice profile, or undefined if no front camera
   * is available on this device. This is the target sensor for DRISHTI's
   * biometric face authentication capture.
   */
  device: CameraDevice | undefined;

  /**
   * Imperatively re-checks the camera permission status without triggering
   * the system permission dialog. Use this when the user returns from
   * the device Settings app after manually toggling permissions.
   *
   * @returns Promise<boolean> — the updated permission state
   */
  checkPermissionsDirectly: () => Promise<boolean>;
}

// ─── Hook Implementation ─────────────────────────────────────────────────────

export function useCameraStream(): CameraStreamState {
  const [hasPermission, setHasPermission] = useState<boolean>(false);

  // Vision Camera's useCameraDevices() uses JSI to enumerate available
  // hardware camera profiles from the Android CameraManager / iOS AVCaptureDevice.
  const devices = useCameraDevices();

  // DRISHTI exclusively uses the front-facing camera for secure biometric
  // face authentication. Filter the device list to isolate it.
  const frontCamera = devices.find(
    (d: CameraDevice) => d.position === 'front'
  );

  // ── Permission Request on Mount ────────────────────────────────────────

  useEffect(() => {
    let isMounted = true;

    async function requestCameraAccess() {
      try {
        // First check the current permission status before prompting.
        // This avoids showing the system dialog if already granted.
        const currentStatus = await Camera.getCameraPermissionStatus();

        if (currentStatus === 'granted') {
          if (isMounted) setHasPermission(true);
          return;
        }

        // Permission not yet granted — trigger the system permission dialog.
        // On Android, this shows the standard "Allow DRISHTI to access your camera?" prompt.
        // On iOS, this shows the system permission sheet.
        const requestResult = await Camera.requestCameraPermission();

        if (isMounted) {
          setHasPermission(requestResult === 'granted');
        }
      } catch (error) {
        console.error('[DRISHTI] Camera permission request failed:', error);
        if (isMounted) {
          setHasPermission(false);
        }
      }
    }

    requestCameraAccess();

    return () => {
      isMounted = false;
    };
  }, []);

  // ── Imperative Permission Refresh ──────────────────────────────────────

  /**
   * Re-checks the current camera permission status without triggering the
   * system dialog. This is useful in the following flow:
   *
   *   1. User denies camera permission
   *   2. App shows a "Permission Required" screen with "Open Settings" button
   *   3. User navigates to device Settings and enables camera access
   *   4. User returns to the app
   *   5. App calls checkPermissionsDirectly() to detect the change
   *
   * This avoids a full re-mount cycle and provides instant UI reactivity.
   */
  const checkPermissionsDirectly = useCallback(async (): Promise<boolean> => {
    try {
      const status = await Camera.getCameraPermissionStatus();
      const granted = status === 'granted';
      setHasPermission(granted);
      return granted;
    } catch (error) {
      console.error('[DRISHTI] Permission status check failed:', error);
      setHasPermission(false);
      return false;
    }
  }, []);

  return {
    hasPermission,
    device: frontCamera,
    checkPermissionsDirectly,
  };
}
