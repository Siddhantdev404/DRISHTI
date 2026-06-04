import { open } from 'react-native-quick-sqlite';
import { FaceAuthEngine } from '../native/FaceAuthEngine';

// Open a persistent database file on the device
const db = open({ name: 'drishti_secure.db' });

export const DatabaseService = {
  /**
   * Initializes the local database tables
   */
  initDatabase(): void {
    db.execute('PRAGMA journal_mode=WAL;');
    db.execute('PRAGMA wal_autocheckpoint=0;');
    console.log('DRISHTI Database: PRAGMA journal_mode=WAL and wal_autocheckpoint=0 set.');

    // Create the users table to store IDs, names, and face template blobs
    const result = db.execute(
      `CREATE TABLE IF NOT EXISTS users (
        id TEXT PRIMARY KEY NOT NULL,
        name TEXT NOT NULL,
        face_embedding BLOB NOT NULL,
        created_at DATETIME DEFAULT CURRENT_TIMESTAMP
      );`
    );
    
    db.execute(`CREATE INDEX IF NOT EXISTS idx_user_id ON users(id);`);

    // Create the attendance table for verified/rejected match records
    db.execute(
      `CREATE TABLE IF NOT EXISTS attendance (
        id TEXT PRIMARY KEY NOT NULL,
        user_id TEXT NOT NULL,
        user_name TEXT NOT NULL,
        timestamp INTEGER NOT NULL,
        confidence REAL NOT NULL,
        liveness_score REAL DEFAULT 0,
        verification_result TEXT NOT NULL
      );`
    );
    db.execute(`CREATE INDEX IF NOT EXISTS idx_attendance_ts ON attendance(timestamp DESC);`);
    console.log('DRISHTI Database: Tables initialized successfully.');
  },

  /**
   * Saves a newly registered user and their face embedding array
   */
  registerUser(id: string, name: string, embedding: Float32Array): boolean {
    try {
      console.log('[DRISHTI_DB] embedding typeof', typeof embedding);
      console.log('[DRISHTI_DB] embedding constructor', embedding?.constructor?.name);
      console.log('[DRISHTI_DB] embedding length', embedding?.length);
      console.log('[DRISHTI_DB] embedding sample', embedding?.slice?.(0, 5));
      console.log('[DRISHTI_DB] embedding value', embedding);

      // Convert Float32Array to JSON string for SQLite storage
      const jsonString = JSON.stringify(Array.from(embedding));
      
      console.log('[DRISHTI_DB] Serialized Length:', jsonString.length);

      const result = db.execute(
        'INSERT OR REPLACE INTO users (id, name, face_embedding) VALUES (?, ?, ?);',
        [id, name, jsonString]
      );

      const success = result.rowsAffected > 0;
      if (success) {
        console.log('[DRISHTI_DB] User inserted successfully');
        console.log('[DRISHTI_DB] User count:', this.getUserCount());
      }
      return success;
    } catch (error) {
      console.error('Database registration failed:', error);
      return false;
    }
  },

  /**
   * Fetches all registered users to populate your C++ LSHIndex cache at boot time
   */
  getAllEmbeddings(): Array<{ id: string; name: string; embedding: Float32Array }> {
    try {
      const result = db.execute('SELECT id, name, face_embedding FROM users;');
      const usersList: Array<{ id: string; name: string; embedding: Float32Array }> = [];

      if (result.rows && result.rows.length > 0) {
        for (let i = 0; i < result.rows.length; i++) {
          const row = result.rows.item(i);
          
          let float32Vector: Float32Array;
          try {
            // Parse JSON string back to Float32Array
            const parsedArray = JSON.parse(row.face_embedding);
            float32Vector = new Float32Array(parsedArray);
          } catch (e) {
            console.error('[DRISHTI_DB] Failed to parse face_embedding for user:', row.id, e);
            continue;
          }

          usersList.push({
            id: row.id,
            name: row.name,
            embedding: float32Vector
          });
        }
      }
      return usersList;
    } catch (error) {
      console.error('Failed to query embeddings from database:', error);
      return [];
    }
  },

  /**
   * Deletes a user identity template from local database
   */
  deleteUser(id: string): boolean {
    const result = db.execute('DELETE FROM users WHERE id = ?;', [id]);
    return result.rowsAffected > 0;
  },

  /**
   * Returns a single user by ID — used for name lookup during verification
   */
  getUserById(id: string): { id: string; name: string } | null {
    try {
      const result = db.execute('SELECT id, name FROM users WHERE id = ?;', [id]);
      if (result.rows && result.rows.length > 0) {
        const row = result.rows.item(0);
        return { id: row.id, name: row.name };
      }
      return null;
    } catch (error) {
      console.error('[DRISHTI] getUserById failed:', error);
      return null;
    }
  },

  /**
   * Writes a verification event (VERIFIED or REJECTED) to the attendance ledger
   */
  saveAttendance(record: {
    id: string;
    userId: string;
    userName: string;
    timestamp: number;
    confidence: number;
    livenessScore: number;
    verificationResult: string;
  }): boolean {
    try {
      const result = db.execute(
        `INSERT OR REPLACE INTO attendance
          (id, user_id, user_name, timestamp, confidence, liveness_score, verification_result)
         VALUES (?, ?, ?, ?, ?, ?, ?);`,
        [
          record.id,
          record.userId,
          record.userName,
          record.timestamp,
          record.confidence,
          record.livenessScore,
          record.verificationResult,
        ]
      );
      return result.rowsAffected > 0;
    } catch (error) {
      console.error('[DRISHTI] saveAttendance failed:', error);
      return false;
    }
  },

  /**
   * Returns all attendance records sorted newest first
   */
  getAttendanceHistory(): Array<{
    id: string;
    userId: string;
    userName: string;
    timestamp: number;
    confidence: number;
    verificationResult: string;
  }> {
    try {
      const result = db.execute(
        'SELECT id, user_id, user_name, timestamp, confidence, verification_result FROM attendance ORDER BY timestamp DESC;'
      );
      const records: Array<{
        id: string; userId: string; userName: string;
        timestamp: number; confidence: number; verificationResult: string;
      }> = [];

      if (result.rows && result.rows.length > 0) {
        for (let i = 0; i < result.rows.length; i++) {
          const row = result.rows.item(i);
          records.push({
            id: row.id,
            userId: row.user_id,
            userName: row.user_name,
            timestamp: row.timestamp,
            confidence: row.confidence,
            verificationResult: row.verification_result,
          });
        }
      }
      return records;
    } catch (error) {
      console.error('[DRISHTI] getAttendanceHistory failed:', error);
      return [];
    }
  },

  /**
   * Clears all attendance records from the database
   */
  clearAttendanceHistory(): boolean {
    try {
      db.execute('DELETE FROM attendance;');
      return true;
    } catch (error) {
      console.error('[DRISHTI] clearAttendanceHistory failed:', error);
      return false;
    }
  },

  /**
   * Returns the total number of enrolled users (fast COUNT query, no blobs loaded)
   */
  getUserCount(): number {
    try {
      const result = db.execute('SELECT COUNT(*) as cnt FROM users;');
      if (result.rows && result.rows.length > 0) {
        return result.rows.item(0).cnt ?? 0;
      }
      return 0;
    } catch { return 0; }
  },

  /**
   * Returns the total number of attendance records
   */
  getAttendanceCount(): number {
    try {
      const result = db.execute('SELECT COUNT(*) as cnt FROM attendance;');
      if (result.rows && result.rows.length > 0) {
        return result.rows.item(0).cnt ?? 0;
      }
      return 0;
    } catch { return 0; }
  },

  /**
   * Temporary developer-only reset function
   */
  resetAllData(): boolean {
    try {
      db.execute('DELETE FROM users;');
      console.log('[DRISHTI_ADMIN] Users cleared');

      db.execute('DELETE FROM attendance;');
      console.log('[DRISHTI_ADMIN] Attendance cleared');

      // Clear the native state
      FaceAuthEngine.resetEngine();

      // Reload LSH (effectively clearing it since db is empty)
      const allUsers = DatabaseService.getAllEmbeddings();
      allUsers.forEach(u => {
        if (u.embedding) {
          FaceAuthEngine.insertProfile(u.id, u.embedding);
        }
      });
      console.log('[DRISHTI_ADMIN] LSH cleared');

      console.log('[DRISHTI_ADMIN] System reset complete');
      return true;
    } catch (e) {
      console.error('[DRISHTI_DB] resetAllData failed:', e);
      return false;
    }
  },
};

