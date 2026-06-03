import { appendLedgerEvent } from '../storage/ledger';

export enum SyncState {
  ONLINE = 'ONLINE',
  OFFLINE = 'OFFLINE',
  CAPTIVE_PORTAL = 'CAPTIVE_PORTAL'
}

export class SyncService {
  /**
   * Probes connectivitycheck.gstatic.com to detect captive portals 
   * bypassing false positive 200 OKs.
   */
  static async checkConnectivity(): Promise<SyncState> {
    try {
      const response = await fetch('http://connectivitycheck.gstatic.com/generate_204', {
        method: 'HEAD',
        cache: 'no-store'
      });
      if (response.status === 204) {
        return SyncState.ONLINE;
      } else if (response.status === 200) {
        return SyncState.CAPTIVE_PORTAL;
      }
      return SyncState.OFFLINE;
    } catch (e) {
      return SyncState.OFFLINE;
    }
  }
}
