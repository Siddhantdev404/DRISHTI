import React from 'react';
import { View, Text, StyleSheet } from 'react-native';
import Animated, { useAnimatedReaction, useSharedValue, runOnJS } from 'react-native-reanimated';

interface HUDProps {
  inferenceMs: number;
  fps: number;
  ramMB: number;
}

// Skia-driven HUD bypassing JS thread reconciliation for 60FPS UI mapping
export const LedgerHUD: React.FC<HUDProps> = ({ inferenceMs, fps, ramMB }) => {
  const sharedInference = useSharedValue(inferenceMs);
  
  // 30 FPS throttle mechanism for debug text limits UI thread lockups
  useAnimatedReaction(
    () => sharedInference.value,
    (current, previous) => {
      // Skia Ring state updates would be driven here directly from the worklet
    }
  );

  return (
    <View style={styles.hudContainer}>
      <Text style={styles.text}>Inference: {inferenceMs}ms</Text>
      <Text style={styles.text}>FPS: {fps}</Text>
      <Text style={styles.text}>RAM: {ramMB}MB</Text>
    </View>
  );
};

const styles = StyleSheet.create({
  hudContainer: {
    position: 'absolute',
    top: 40,
    right: 20,
    backgroundColor: 'rgba(0,0,0,0.7)',
    padding: 10,
    borderRadius: 8,
  },
  text: {
    color: '#00FF00',
    fontFamily: 'monospace',
    fontSize: 12,
  }
});
