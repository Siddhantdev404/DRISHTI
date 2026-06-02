import { useState, useCallback } from 'react';
import { FaceAuthEngine } from '../native/FaceAuthEngine';

export const useFaceAuth = () => {
  const [isInitialized, setIsInitialized] = useState(false);
  const [livenessState, setLivenessState] = useState<number>(0);
  const [isProcessing, setIsProcessing] = useState(false);

  const initEngine = useCallback(async (modelPath: string) => {
    try {
      const success = FaceAuthEngine.initializeEngine(modelPath);
      setIsInitialized(success);
      return success;
    } catch (error) {
      console.error("Failed to boot DRISHTI core engine:", error);
      setIsInitialized(false);
      return false;
    }
  }, []);

  const handleCameraFrame = useCallback((frameArrayBuffer: ArrayBuffer, width: number, height: number, rotation: number) => {
    if (!isInitialized || isProcessing) return;

    setIsProcessing(true);
    try {
      const result = FaceAuthEngine.processFrame(frameArrayBuffer, width, height, rotation);
      
      // Update liveness FSM feedback state tracking metrics
      setLivenessState(result.livenessStatus);
      
      return result;
    } catch (e) {
      console.error("Frame processing execution pass failed:", e);
    } finally {
      setIsProcessing(false);
    }
  }, [isInitialized, isProcessing]);

  return {
    initEngine,
    handleCameraFrame,
    isInitialized,
    livenessState
  };
};
