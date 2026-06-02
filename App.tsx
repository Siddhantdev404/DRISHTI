/**
 * App.tsx — DRISHTI Root Application Container
 *
 * Boot sequence:
 * 1. Initialize SQLite database tables (DatabaseService)
 * 2. Load all stored face embeddings from the offline vector database
 * 3. Insert cached embeddings into the C++ LSHIndex for matching
 * 4. Render the FaceAuthScreen which manages camera + engine lifecycle
 */

import React, { useEffect, useState } from 'react';
import { StatusBar, SafeAreaView, StyleSheet, View, Text, ActivityIndicator, AppRegistry } from 'react-native';

// Force safety fallbacks on the Javascript global execution context before any native imports load
if (global != null) {
  // Disables the Bridgeless Registry hijacking on the active JS thread thread map
  (global as any).__turboModuleProxy = (global as any).__turboModuleProxy || null;
}

import 'react-native-worklets-core'; 
import { DatabaseService } from './src/database/DatabaseService';
import { FaceAuthEngine } from './src/native/FaceAuthEngine';
import FaceAuthScreen from './src/screens/FaceAuthScreen';

type BootPhase = 'initializing' | 'ready' | 'error';

export default function App() {
  const [bootPhase, setBootPhase] = useState<BootPhase>('initializing');
  const [profileCount, setProfileCount] = useState<number>(0);
  const [bootError, setBootError] = useState<string>('');

  useEffect(() => {
    let isMounted = true;

    function boot() {
      try {
        // ── Step 1: Initialize the local SQLite vector database schema ───
        DatabaseService.initDatabase();

        // ── Step 2: Load all stored face embeddings from disk ────────────
        const storedProfiles = DatabaseService.getAllEmbeddings();

        // ── Step 3: Populate the C++ LSHIndex with cached profiles ───────
        let loaded = 0;
        for (const profile of storedProfiles) {
          try {
            FaceAuthEngine.insertProfile(profile.id, profile.embedding);
            loaded++;
          } catch (err) {
            console.warn(
              `[DRISHTI] Failed to load profile ${profile.id} into LSHIndex:`,
              err
            );
          }
        }

        if (isMounted) {
          setProfileCount(loaded);
          setBootPhase('ready');
          console.log(
            `[DRISHTI] Boot complete. ${loaded} profile(s) loaded into LSHIndex.`
          );
        }
      } catch (error) {
        console.error('[DRISHTI] Boot sequence failed:', error);
        if (isMounted) {
          setBootPhase('error');
          setBootError(error instanceof Error ? error.message : String(error));
        }
      }
    }

    boot();
    return () => {
      isMounted = false;
    };
  }, []);

  // ── Boot Screen ─────────────────────────────────────────────────────────

  if (bootPhase === 'initializing') {
    return (
      <SafeAreaView style={styles.bootContainer}>
        <StatusBar barStyle="light-content" backgroundColor="#000" />
        <ActivityIndicator size="large" color="#00FFCC" />
        <Text style={styles.bootText}>Initializing secure storage…</Text>
      </SafeAreaView>
    );
  }

  if (bootPhase === 'error') {
    return (
      <SafeAreaView style={styles.bootContainer}>
        <StatusBar barStyle="light-content" backgroundColor="#000" />
        <Text style={styles.errorIcon}>⚠️</Text>
        <Text style={styles.errorTitle}>Boot Failed</Text>
        <Text style={styles.errorDetail}>{bootError}</Text>
      </SafeAreaView>
    );
  }

  // ── Main Application ──────────────────────────────────────────────────

  return (
    <SafeAreaView style={styles.root}>
      <StatusBar barStyle="light-content" backgroundColor="#000" />
      <FaceAuthScreen />
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: '#000',
  },
  bootContainer: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: '#0A0A0F',
  },
  bootText: {
    color: '#666',
    fontSize: 14,
    marginTop: 16,
  },
  errorIcon: {
    fontSize: 48,
    marginBottom: 12,
  },
  errorTitle: {
    color: '#FF4444',
    fontSize: 20,
    fontWeight: '700',
  },
  errorDetail: {
    color: '#FF8888',
    fontSize: 13,
    marginTop: 8,
    textAlign: 'center',
    paddingHorizontal: 32,
  },
});

// Explicit registration matching your main application config keys seamlessly
AppRegistry.registerComponent('drishti', () => App);
AppRegistry.registerComponent('MainApplication', () => App);
