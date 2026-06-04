/**
 * AppNavigator.tsx
 *
 * Bottom-tab router.  App.tsx renders this single component — the entire
 * boot / DB-init / LSH-reload sequence in App.tsx is untouched.
 *
 * Screens (switch-based, so only one Camera is ever active):
 *   🏠 Home    → HomeScreen  (default)
 *   🔍 Scan    → FaceAuthScreen  (existing, unchanged)
 *   👤 Enroll  → EnrollmentScreen
 *   ✅ Verify  → VerifyScreen
 *   📋 History → LedgerScreen
 */

import React, { useState } from 'react';
import { StyleSheet, View, Text, Pressable } from 'react-native';

import HomeScreen        from '../screens/HomeScreen';
import FaceAuthScreen    from '../screens/FaceAuthScreen';
import EnrollmentScreen  from '../screens/EnrollmentScreen';
import VerifyScreen      from '../screens/VerifyScreen';
import LedgerScreen      from '../screens/LedgerScreen';

// ─── Tab definitions ──────────────────────────────────────────────────────────

type Screen = 'home' | 'scan' | 'enroll' | 'verify' | 'history';

const TABS: { id: Screen; label: string; icon: string }[] = [
  { id: 'home',    label: 'Home',    icon: '🏠' },
  { id: 'scan',    label: 'Scan',    icon: '🔍' },
  { id: 'enroll',  label: 'Enroll',  icon: '👤' },
  { id: 'verify',  label: 'Verify',  icon: '✅' },
  { id: 'history', label: 'History', icon: '📋' },
];

// ─── Component ────────────────────────────────────────────────────────────────

export default function AppNavigator() {
  const [active, setActive] = useState<Screen>('home');

  function goBack() { setActive('home'); }

  function renderScreen() {
    switch (active) {
      case 'home':
        return <HomeScreen onNavigate={(s) => setActive(s)} />;
      case 'scan':
        return <FaceAuthScreen />;
      case 'enroll':
        return <EnrollmentScreen onBack={goBack} />;
      case 'verify':
        return <VerifyScreen onBack={goBack} />;
      case 'history':
        return <LedgerScreen onBack={goBack} />;
    }
  }

  return (
    <View style={styles.root}>
      <View style={styles.content}>
        {renderScreen()}
      </View>

      <View style={styles.tabBar}>
        {TABS.map((tab) => {
          const isActive = active === tab.id;
          return (
            <Pressable
              key={tab.id}
              style={[styles.tab, isActive && styles.tabActive]}
              onPress={() => setActive(tab.id)}
            >
              <Text style={styles.tabIcon}>{tab.icon}</Text>
              <Text style={[styles.tabLabel, isActive && styles.tabLabelActive]}>
                {tab.label}
              </Text>
            </Pressable>
          );
        })}
      </View>
    </View>
  );
}

// ─── Styles ───────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  root:    { flex: 1, backgroundColor: '#0A0A0F' },
  content: { flex: 1 },

  tabBar: {
    flexDirection: 'row',
    backgroundColor: '#0D0D15',
    borderTopWidth: 1,
    borderTopColor: 'rgba(255,255,255,0.07)',
  },

  tab: {
    flex: 1,
    alignItems: 'center',
    paddingVertical: 10,
    paddingBottom: 12,
  },
  tabActive: {
    borderTopWidth: 2,
    borderTopColor: '#00FFCC',
  },

  tabIcon:         { fontSize: 18, marginBottom: 2 },
  tabLabel:        { color: '#444', fontSize: 10, fontWeight: '600' },
  tabLabelActive:  { color: '#00FFCC' },
});
