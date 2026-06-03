import * as ed from '@noble/ed25519';

export class ErasureVerification {
  /**
   * Securely verifies enterprise-signed erasure commands using noble-ed25519.
   * If verification fails, returns HARDWARE_SECURITY_VIOLATION preventing local DB purge.
   */
  static async verifyErasureCommand(
    commandPayload: Uint8Array,
    signature: Uint8Array,
    adminPublicKey: Uint8Array
  ): Promise<boolean> {
    try {
      const isValid = await ed.verifyAsync(signature, commandPayload, adminPublicKey);
      if (!isValid) {
        throw new Error('HARDWARE_SECURITY_VIOLATION: Erasure signature invalid');
      }
      return true;
    } catch (error) {
      console.error('Erasure verification failed:', error);
      return false;
    }
  }
}
