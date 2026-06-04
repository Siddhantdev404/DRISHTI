/**
 * LedgerScreen.tsx
 *
 * Attendance history screen.  Reads all attendance records from SQLite
 * (via DatabaseService.getAttendanceHistory) and displays them sorted
 * newest first.  No native dependencies — pure business logic layer.
 */

import React, { useEffect, useState, useCallback } from 'react';
import {
  StyleSheet,
  Text,
  View,
  FlatList,
  Pressable,
  ActivityIndicator,
  DeviceEventEmitter,
  Alert,
} from 'react-native';
import { DatabaseService } from '../database/DatabaseService';

// ─── Types ────────────────────────────────────────────────────────────────────

interface AttendanceRecord {
  id: string;
  userId: string;
  userName: string;
  timestamp: number;
  confidence: number;
  verificationResult: string;
}

interface Props {
  onBack: () => void;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

function formatTimestamp(ts: number): string {
  const d = new Date(ts);
  return d.toLocaleString('en-IN', {
    day:    '2-digit',
    month:  'short',
    year:   'numeric',
    hour:   '2-digit',
    minute: '2-digit',
    hour12: true,
  });
}

// ─── Component ────────────────────────────────────────────────────────────────

export default function LedgerScreen({ onBack }: Props) {
  const [records, setRecords] = useState<AttendanceRecord[]>([]);
  const [loading, setLoading] = useState(true);

  const loadRecords = useCallback(() => {
    setLoading(true);
    try {
      const data = DatabaseService.getAttendanceHistory();
      setRecords(data);
    } catch (e) {
      console.error('[DRISHTI] Failed to load attendance history:', e);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    loadRecords();
  }, [loadRecords]);

  useEffect(() => {
    const sub = DeviceEventEmitter.addListener('attendanceUpdated', loadRecords);
    return () => sub.remove();
  }, [loadRecords]);

  const handleClearHistory = useCallback(() => {
    Alert.alert(
      'Clear History',
      'Clear all attendance records?',
      [
        { text: 'Cancel', style: 'cancel' },
        { 
          text: 'Delete', 
          style: 'destructive',
          onPress: () => {
            const success = DatabaseService.clearAttendanceHistory();
            if (success) {
              loadRecords();
              DeviceEventEmitter.emit('attendanceUpdated'); // notify HomeScreen dashboard
            }
          }
        }
      ]
    );
  }, [loadRecords]);

  // ─── Row renderer ─────────────────────────────────────────────────────────

  const renderItem = ({ item }: { item: AttendanceRecord }) => {
    const isVerified = item.verificationResult === 'VERIFIED';
    return (
      <View style={styles.row}>
        <View style={styles.rowLeft}>
          <Text style={styles.userName} numberOfLines={1}>
            {item.userName}
          </Text>
          <Text style={styles.timestamp}>
            {formatTimestamp(item.timestamp)}
          </Text>
        </View>

        <View style={styles.rowRight}>
          <Text style={styles.confidence}>
            {(item.confidence * 100).toFixed(1)}%
          </Text>
          <View style={[styles.badge, isVerified ? styles.badgeVerified : styles.badgeRejected]}>
            <Text style={[styles.badgeText, isVerified ? styles.badgeTextVerified : styles.badgeTextRejected]}>
              {item.verificationResult}
            </Text>
          </View>
        </View>
      </View>
    );
  };

  // ─── Render ───────────────────────────────────────────────────────────────

  return (
    <View style={styles.container}>
      {/* Header */}
      <View style={styles.header}>
        <Pressable onPress={onBack} style={styles.backBtn} hitSlop={12}>
          <Text style={styles.backText}>← Back</Text>
        </Pressable>
        <Text style={styles.title}>Attendance History</Text>
        <View style={styles.headerRight}>
          <Pressable onPress={loadRecords} style={styles.refreshBtn} hitSlop={12}>
            <Text style={styles.refreshText}>↻</Text>
          </Pressable>
          <Pressable onPress={handleClearHistory} style={styles.clearBtn} hitSlop={12}>
            <Text style={styles.clearText}>🗑</Text>
          </Pressable>
        </View>
      </View>

      {/* Body */}
      {loading ? (
        <View style={styles.centered}>
          <ActivityIndicator size="large" color="#00FFCC" />
        </View>
      ) : records.length === 0 ? (
        <View style={styles.centered}>
          <Text style={styles.emptyIcon}>📋</Text>
          <Text style={styles.emptyTitle}>No records yet</Text>
          <Text style={styles.emptySubtext}>
            Complete a verification on the Verify tab to see records here.
          </Text>
        </View>
      ) : (
        <>
          <Text style={styles.countLabel}>
            {records.length} record{records.length !== 1 ? 's' : ''} — newest first
          </Text>
          <FlatList
            data={records}
            keyExtractor={(item) => item.id}
            renderItem={renderItem}
            contentContainerStyle={styles.list}
            ItemSeparatorComponent={() => <View style={styles.separator} />}
          />
        </>
      )}
    </View>
  );
}

// ─── Styles ───────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#0A0A0F' },

  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 20,
    paddingTop: 16,
    paddingBottom: 12,
    borderBottomWidth: 1,
    borderBottomColor: 'rgba(255,255,255,0.06)',
  },
  backBtn:     { paddingRight: 12, paddingVertical: 8 },
  backText:    { color: '#00FFCC', fontSize: 15, fontWeight: '600' },
  title:       { color: '#FFF',    fontSize: 18, fontWeight: '700', flex: 1, textAlign: 'center' },
  headerRight: { flexDirection: 'row', alignItems: 'center' },
  refreshBtn:  { paddingHorizontal: 12, paddingVertical: 8 },
  refreshText: { color: '#00FFCC', fontSize: 22, fontWeight: '400' },
  clearBtn:    { paddingLeft: 12, paddingVertical: 8 },
  clearText:   { color: '#FF3344', fontSize: 20, fontWeight: '400' },

  countLabel: {
    color: '#444',
    fontSize: 12,
    fontWeight: '600',
    paddingHorizontal: 20,
    paddingTop: 12,
    paddingBottom: 4,
  },

  centered: { flex: 1, justifyContent: 'center', alignItems: 'center', padding: 40 },
  emptyIcon:    { fontSize: 48, marginBottom: 12 },
  emptyTitle:   { color: '#FFF', fontSize: 16, fontWeight: '600', marginBottom: 8 },
  emptySubtext: { color: '#555', fontSize: 13, textAlign: 'center', lineHeight: 18 },

  list: { padding: 16 },

  row: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    backgroundColor: '#12121A',
    padding: 16,
    borderRadius: 12,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.05)',
  },
  rowLeft:  { flex: 1, marginRight: 12 },
  rowRight: { alignItems: 'flex-end' },

  userName:   { color: '#FFF', fontSize: 16, fontWeight: '600' },
  timestamp:  { color: '#555', fontSize: 12, marginTop: 3 },
  confidence: { color: '#00FFCC', fontSize: 13, fontWeight: '600', marginBottom: 6 },

  badge: {
    paddingHorizontal: 9,
    paddingVertical: 3,
    borderRadius: 6,
  },
  badgeVerified:    { backgroundColor: 'rgba(0,255,136,0.12)' },
  badgeRejected:    { backgroundColor: 'rgba(255,51,68,0.12)' },
  badgeText:        { fontSize: 10, fontWeight: '700', letterSpacing: 0.6 },
  badgeTextVerified: { color: '#00FF88' },
  badgeTextRejected: { color: '#FF3344' },

  separator: { height: 8 },
});
