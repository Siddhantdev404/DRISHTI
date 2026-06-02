import { NativeModules } from 'react-native';

// Define the interface mapping the exact C++ functions exposed by FaceAuthHostObject
interface FaceAuthJSI {
  initializeEngine(modelPath: string): boolean;
  processFrame(frameBuffer: ArrayBuffer, width: number, height: number, rotation: number): {
    livenessStatus: number; // Matches your LivenessFSM state values
    hasFace: boolean;
    embedding?: Float32Array; // 512-dimension face embedding vector
  };
  registerFace(userId: string, embedding: Float32Array): boolean;
  verifyFace(embedding: Float32Array): {
    success: boolean;
    confidence: number;
    matchedUserId?: string;
  };
}

// Access the globally injected JSI object
// React Native's New Architecture injects this via your face_auth_plugin initialization
const globalJSI = global as any;

export const FaceAuthEngine: FaceAuthJSI = {
  initializeEngine: (modelPath: string) => {
    if (!globalJSI.FaceAuthHostObject) {
      throw new Error("DRISHTI Native Engine: JSI Host Object not found. Initialization failed.");
    }
    return globalJSI.FaceAuthHostObject.initializeEngine(modelPath);
  },

  processFrame: (frameBuffer: ArrayBuffer, width: number, height: number, rotation: number) => {
    return globalJSI.FaceAuthHostObject.processFrame(frameBuffer, width, height, rotation);
  },

  registerFace: (userId: string, embedding: Float32Array) => {
    return globalJSI.FaceAuthHostObject.registerFace(userId, embedding);
  },

  verifyFace: (embedding: Float32Array) => {
    return globalJSI.FaceAuthHostObject.verifyFace(embedding);
  }
};
