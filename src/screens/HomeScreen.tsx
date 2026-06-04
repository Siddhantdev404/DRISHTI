/**
 * HomeScreen.tsx — DRISHTI Command Centre
 *
 * Central dashboard showing system status, enrolled profile count, attendance
 * count, and navigation buttons to all four feature screens.
 *
 * Reads from DatabaseService on every mount (fresh because AppNavigator uses
 * a switch statement — the component always re-mounts when the tab is selected).
 */

import React, { useEffect, useState, useCallback } from 'react';
import {
  StyleSheet, Text, View, Pressable, ScrollView, DeviceEventEmitter, Alert
} from 'react-native';
import { DatabaseService } from '../database/DatabaseService';

interface Props {
  onNavigate: (screen: 'scan' | 'enroll' | 'verify' | 'history') => void;
}

export default function HomeScreen({ onNavigate }: Props) {
  const [profileCount,    setProfileCount]    = useState(0);
  const [attendanceCount, setAttendanceCount] = useState(0);
  const [dbReady,         setDbReady]         = useState(true);

  const loadData = useCallback(() => {
    try {
      setProfileCount(DatabaseService.getUserCount());
      setAttendanceCount(DatabaseService.getAttendanceCount());
      setDbReady(true);
    } catch (e) {
      console.error('[DRISHTI_HOME] DB read failed:', e);
      setDbReady(false);
    }
  }, []);

  useEffect(() => {
    loadData();
  }, [loadData]);

  useEffect(() => {
    const sub = DeviceEventEmitter.addListener('attendanceUpdated', loadData);
    return () => sub.remove();
  }, [loadData]);

  const isReady = dbReady;

  const handleClearAll = useCallback(() => {
    Alert.alert(
      "Reset DRISHTI",
      "This will delete all enrolled users, attendance history, and LSH profiles. Continue?",
      [
        { text: "Cancel", style: "cancel" },
        { 
          text: "Delete", 
          style: "destructive",
          onPress: () => {
            DatabaseService.resetAllData();
            loadData();
            DeviceEventEmitter.emit('attendanceUpdated'); // notify other screens if they are mounted
          }
        }
      ]
    );
  }, [loadData]);

  return (
    <ScrollView style={styles.scroll} contentContainerStyle={styles.scrollContent}>

      {/* ── Branding ─────────────────────────────────────────────────────── */}
      <View style={styles.brandRow}>
        <View style={styles.logoRing}>
          <Text style={styles.logoChar}>D</Text>
        </View>
        <View>
          <Text style={styles.brandName}>DRISHTI</Text>
          <Text style={styles.brandSub}>Biometric Attendance</Text>
        </View>
      </View>

      {/* ── Status pills ─────────────────────────────────────────────────── */}
      <View style={styles.pillRow}>
        <View style={[styles.pill, styles.pillGreen]}>
          <View style={styles.pillDot} />
          <Text style={styles.pillText}>AI Engine</Text>
        </View>
        <View style={[styles.pill, styles.pillGreen]}>
          <View style={styles.pillDot} />
          <Text style={styles.pillText}>Camera</Text>
        </View>
        <View style={[styles.pill, isReady ? styles.pillGreen : styles.pillRed]}>
          <View style={[styles.pillDot, !isReady && styles.pillDotRed]} />
          <Text style={styles.pillText}>Database</Text>
        </View>
      </View>

      {/* ── Stat cards ───────────────────────────────────────────────────── */}
      <View style={styles.statsRow}>
        <View style={styles.statCard}>
          <Text style={styles.statNumber}>{profileCount}</Text>
          <Text style={styles.statLabel}>Enrolled{'\n'}Profiles</Text>
        </View>
        <View style={styles.statCard}>
          <Text style={styles.statNumber}>{attendanceCount}</Text>
          <Text style={styles.statLabel}>Attendance{'\n'}Records</Text>
        </View>
      </View>

      {/* ── Action buttons ───────────────────────────────────────────────── */}
      <Text style={styles.sectionTitle}>ACTIONS</Text>

      <Pressable style={styles.actionBtn} onPress={() => onNavigate('enroll')}>
        <View style={[styles.actionIcon, { backgroundColor: 'rgba(0,255,204,0.08)' }]}>
          <Text style={styles.actionEmoji}>👤</Text>
        </View>
        <View style={styles.actionText}>
          <Text style={styles.actionLabel}>Enroll User</Text>
          <Text style={styles.actionSub}>Register a new face profile</Text>
        </View>
        <Text style={styles.actionChevron}>›</Text>
      </Pressable>

      <Pressable style={styles.actionBtn} onPress={() => onNavigate('verify')}>
        <View style={[styles.actionIcon, { backgroundColor: 'rgba(0,136,255,0.08)' }]}>
          <Text style={styles.actionEmoji}>✅</Text>
        </View>
        <View style={styles.actionText}>
          <Text style={styles.actionLabel}>Verify Identity</Text>
          <Text style={styles.actionSub}>Authenticate against enrolled profiles</Text>
        </View>
        <Text style={styles.actionChevron}>›</Text>
      </Pressable>

      <Pressable style={styles.actionBtn} onPress={() => onNavigate('scan')}>
        <View style={[styles.actionIcon, { backgroundColor: 'rgba(255,170,0,0.08)' }]}>
          <Text style={styles.actionEmoji}>🔍</Text>
        </View>
        <View style={styles.actionText}>
          <Text style={styles.actionLabel}>AI Diagnostics</Text>
          <Text style={styles.actionSub}>Live FaceMesh · liveness · FPS</Text>
        </View>
        <Text style={styles.actionChevron}>›</Text>
      </Pressable>

      <Pressable style={styles.actionBtn} onPress={() => onNavigate('history')}>
        <View style={[styles.actionIcon, { backgroundColor: 'rgba(180,0,255,0.08)' }]}>
          <Text style={styles.actionEmoji}>📋</Text>
        </View>
        <View style={styles.actionText}>
          <Text style={styles.actionLabel}>Attendance History</Text>
          <Text style={styles.actionSub}>View all verification records</Text>
        </View>
        <Text style={styles.actionChevron}>›</Text>
      </Pressable>

      <Text style={[styles.sectionTitle, { marginTop: 24, color: '#FF3366' }]}>ADMIN CONTROLS</Text>
      
      <Pressable style={styles.actionBtn} onPress={handleClearAll}>
        <View style={[styles.actionIcon, { backgroundColor: 'rgba(255,51,102,0.08)' }]}>
          <Text style={styles.actionEmoji}>🗑</Text>
        </View>
        <View style={styles.actionText}>
          <Text style={[styles.actionLabel, { color: '#FF3366' }]}>Clear All Users</Text>
          <Text style={styles.actionSub}>Delete all data and reset LSH index</Text>
        </View>
        <Text style={styles.actionChevron}>›</Text>
      </Pressable>

      {/* ── Guidance banner ──────────────────────────────────────────────── */}
      {profileCount === 0 && (
        <View style={styles.guidanceBanner}>
          <Text style={styles.guidanceIcon}>💡</Text>
          <Text style={styles.guidanceText}>
            No profiles enrolled yet.{'\n'}
            Tap <Text style={{ color: '#00FFCC', fontWeight: '700' }}>Enroll User</Text> to register the first face.
          </Text>
        </View>
      )}

    </ScrollView>
  );
}

// ─── Styles ───────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  scroll:        { flex: 1, backgroundColor: '#0A0A0F' },
  scrollContent: { paddingHorizontal: 20, paddingTop: 20, paddingBottom: 40 },

  // ── Branding ─────────────────────────────────────────────────────────────
  brandRow: { flexDirection: 'row', alignItems: 'center', marginBottom: 24, gap: 16 },
  logoRing: {
    width: 52, height: 52, borderRadius: 26,
    backgroundColor: 'rgba(0,255,204,0.1)',
    borderWidth: 2, borderColor: '#00FFCC',
    alignItems: 'center', justifyContent: 'center',
  },
  logoChar:   { color: '#00FFCC', fontSize: 24, fontWeight: '900' },
  brandName:  { color: '#FFF', fontSize: 22, fontWeight: '900', letterSpacing: 2 },
  brandSub:   { color: '#444', fontSize: 11, fontWeight: '600', letterSpacing: 0.5 },

  // ── Status pills ─────────────────────────────────────────────────────────
  pillRow: { flexDirection: 'row', gap: 10, marginBottom: 28, flexWrap: 'wrap' },
  pill: {
    flexDirection: 'row', alignItems: 'center', gap: 6,
    paddingHorizontal: 12, paddingVertical: 6,
    borderRadius: 20, borderWidth: 1,
  },
  pillGreen: { backgroundColor: 'rgba(0,255,136,0.06)', borderColor: 'rgba(0,255,136,0.2)' },
  pillRed:   { backgroundColor: 'rgba(255,51,68,0.06)',  borderColor: 'rgba(255,51,68,0.2)' },
  pillDot:    { width: 6, height: 6, borderRadius: 3, backgroundColor: '#00FF88' },
  pillDotRed: { backgroundColor: '#FF3344' },
  pillText:   { color: '#AAA', fontSize: 12, fontWeight: '600' },

  // ── Stat cards ───────────────────────────────────────────────────────────
  statsRow: { flexDirection: 'row', gap: 14, marginBottom: 32 },
  statCard: {
    flex: 1, backgroundColor: '#12121A',
    borderRadius: 18, padding: 20,
    borderWidth: 1, borderColor: 'rgba(255,255,255,0.06)',
    alignItems: 'center',
  },
  statNumber: { color: '#00FFCC', fontSize: 40, fontWeight: '800' },
  statLabel:  { color: '#444', fontSize: 12, textAlign: 'center', marginTop: 4, lineHeight: 17 },

  // ── Section title ─────────────────────────────────────────────────────────
  sectionTitle: {
    color: '#333', fontSize: 11, fontWeight: '700', letterSpacing: 1.5, marginBottom: 12,
  },

  // ── Action buttons ────────────────────────────────────────────────────────
  actionBtn: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: '#12121A',
    borderRadius: 16,
    padding: 16,
    marginBottom: 10,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.05)',
  },
  actionIcon: { width: 46, height: 46, borderRadius: 12, alignItems: 'center', justifyContent: 'center', marginRight: 14 },
  actionEmoji:   { fontSize: 22 },
  actionText:    { flex: 1 },
  actionLabel:   { color: '#FFF', fontSize: 15, fontWeight: '700' },
  actionSub:     { color: '#444', fontSize: 12, marginTop: 2 },
  actionChevron: { color: '#333', fontSize: 22, fontWeight: '300' },

  // ── Guidance banner ───────────────────────────────────────────────────────
  guidanceBanner: {
    flexDirection: 'row',
    alignItems: 'flex-start',
    gap: 14,
    backgroundColor: 'rgba(0,255,204,0.04)',
    borderRadius: 14,
    borderWidth: 1,
    borderColor: 'rgba(0,255,204,0.15)',
    padding: 16,
    marginTop: 20,
  },
  guidanceIcon: { fontSize: 20 },
  guidanceText: { color: '#666', fontSize: 13, lineHeight: 20, flex: 1 },
});
