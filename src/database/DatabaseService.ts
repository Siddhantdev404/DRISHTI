import { open } from 'react-native-quick-sqlite';

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
    console.log('DRISHTI Database: Tables initialized successfully.');
  },

  /**
   * Saves a newly registered user and their face embedding array
   */
  registerUser(id: string, name: string, embedding: Float32Array): boolean {
    try {
      // Convert the Float32Array into a raw ArrayBuffer/Uint8Array Blob for binary storage
      const binaryBlob = new Uint8Array(embedding.buffer);

      const result = db.execute(
        'INSERT OR REPLACE INTO users (id, name, face_embedding) VALUES (?, ?, ?);',
        [id, name, binaryBlob]
      );

      return result.rowsAffected > 0;
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
          
          // Reconstruct the Float32Array vector directly out of the SQLite binary blob memory layout
          const blobUint8 = row.face_embedding;
          const float32Vector = new Float32Array(blobUint8.buffer, blobUint8.byteOffset, blobUint8.byteLength / 4);

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
  }
};
