// storage/ledger.ts
import { NativeModules } from 'react-native';
import { LEDGER_GENESIS_HASH } from '../../shared/FaceAuthResult';

// Assuming global/local implementations for these utilities based on the environment
declare const uuid: () => string;
declare const sha256: (data: Uint8Array) => Uint8Array;
declare const bytesToHex: (bytes: Uint8Array) => string;

const { UptimeModule } = NativeModules;

export interface DB {
  execute(query: string, params?: any[]): any[];
}

let currentSessionAnchorHash: string | null = null;

export async function createBootSessionAnchor(db: DB): Promise<string> {
  const wallTs   = Date.now();                          // ms since Unix epoch
  const uptimeMs = await UptimeModule.getUptimeMs();   // ms since device boot (monotonic)

  // Canonical input string — pipe-delimited, fields in fixed order
  // format: "DRISHTI_ANCHOR|{wallTs}|{uptimeMs}"
  const anchorInput = `DRISHTI_ANCHOR|${wallTs}|${uptimeMs}`;
  const sessionHash = bytesToHex(sha256(new TextEncoder().encode(anchorInput)));

  // Store the anchor — this row is immutable once written
  const anchorId = uuid();
  db.execute(
    `INSERT INTO boot_session_anchors
       (id, wall_ts, uptime_ms, session_hash, created_at)
     VALUES (?, ?, ?, ?, ?)`,
    [anchorId, wallTs, uptimeMs, sessionHash, wallTs]
  );

  // Cache in module scope — used by every subsequent appendLedgerEvent()
  currentSessionAnchorHash = sessionHash;
  return sessionHash;
}

export async function appendLedgerEvent(
  db: DB,
  payload: {
    personnel_id: string;
    action: 'ENROLL' | 'VERIFY' | 'VERIFY_FAIL' | 'SPOOF_ATTEMPT';
    match_score: number;
    device_id: string;
    session_id: string;   // current boot session anchor ID
  }
): Promise<string> {

  // 1. Fetch previous hash and counter atomically
  const prevRows = db.execute(
    `SELECT current_hash, event_counter
     FROM attendance_ledger
     ORDER BY event_counter DESC LIMIT 1`
  );
  const prevHash     = prevRows[0]?.current_hash ?? LEDGER_GENESIS_HASH;
  const eventCounter = (prevRows[0]?.event_counter ?? -1) + 1;

  // 2. Both clock sources — each independently detectable if tampered
  const wallTs   = Date.now();
  const uptimeMs = await UptimeModule.getUptimeMs();

  // 3. Canonical JSON payload — sorted keys, no whitespace, ASCII-safe
  //    Key sort order MUST be alphabetical — both client and server must agree
  const sortedKeys = Object.keys(payload).sort();
  const canonicalPayload = JSON.stringify(
    Object.fromEntries(sortedKeys.map(k => [k, (payload as any)[k]]))
  );

  // 4. Session anchor hash — fetched from current session (created at cold-start)
  //    If anchor creation failed for any reason, abort the write — do not write
  //    an unanchored ledger event.
  if (!currentSessionAnchorHash) {
    throw new Error('SessionAnchorMissing: cannot write unanchored ledger event');
  }

  // 5. Hash input — pipe-delimited, field order is FIXED and must never change
  //    H[n] = SHA256( H[n-1] | canonicalPayload | wallTs | uptimeMs | eventCounter | anchorHash )
  const hashInput = [
    prevHash,
    canonicalPayload,
    String(wallTs),
    String(uptimeMs),
    String(eventCounter),
    currentSessionAnchorHash
  ].join('|');

  const currentHash = bytesToHex(sha256(new TextEncoder().encode(hashInput)));

  // 6. Atomic INSERT — if this fails, the event_counter gap will be detected
  //    by verifyChain() as a broken sequence, not a hash mismatch.
  db.execute(
    `INSERT INTO attendance_ledger
       (id, personnel_id, action, match_score, device_id, session_id,
        payload_json, prev_hash, current_hash,
        wall_ts, uptime_ms, event_counter, session_anchor_hash, synced)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)`,
    [
      uuid(), payload.personnel_id, payload.action, payload.match_score,
      payload.device_id, payload.session_id,
      canonicalPayload, prevHash, currentHash,
      wallTs, uptimeMs, eventCounter, currentSessionAnchorHash
    ]
  );

  return currentHash;
}

export function verifyChain(db: DB): {
  valid: boolean;
  brokenAt?: number;   // event_counter of the first invalid row
  gapAt?: number;      // event_counter of the first sequence gap
  totalRows: number;
} {
  const rows = db.execute(
    `SELECT event_counter, prev_hash, current_hash,
            payload_json, wall_ts, uptime_ms,
            session_anchor_hash
     FROM attendance_ledger
     ORDER BY event_counter ASC`
  );

  if (rows.length === 0) return { valid: true, totalRows: 0 };

  // Genesis check: first row's prev_hash must equal the GENESIS sentinel
  if (rows[0].prev_hash !== LEDGER_GENESIS_HASH) {
    return { valid: false, brokenAt: rows[0].event_counter, totalRows: rows.length };
  }

  // Sequence gap detection: event counters must be consecutive with no holes
  for (let i = 1; i < rows.length; i++) {
    if (rows[i].event_counter !== rows[i-1].event_counter + 1) {
      return { valid: false, gapAt: rows[i].event_counter, totalRows: rows.length };
    }
  }

  // Hash chain recomputation
  let expectedPrevHash = LEDGER_GENESIS_HASH;
  for (const row of rows) {
    const recomputedInput = [
      expectedPrevHash,
      row.payload_json,
      String(row.wall_ts),
      String(row.uptime_ms),
      String(row.event_counter),
      row.session_anchor_hash
    ].join('|');

    const expected = bytesToHex(sha256(new TextEncoder().encode(recomputedInput)));

    if (expected !== row.current_hash) {
      return { valid: false, brokenAt: row.event_counter, totalRows: rows.length };
    }
    expectedPrevHash = row.current_hash;
  }

  return { valid: true, totalRows: rows.length };
}
