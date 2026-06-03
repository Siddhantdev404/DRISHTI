package com.drishti

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod
import com.facebook.react.module.annotations.ReactModule
import java.security.KeyStore
import java.security.PrivateKey
import java.security.Signature
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.spec.GCMParameterSpec

@ReactModule(name = KeystoreModule.NAME)
class KeystoreModule(context: ReactApplicationContext) : ReactContextBaseJavaModule(context) {

    companion object { const val NAME = "KeystoreModule" }

    override fun getName() = NAME

    private fun isStrongBoxAvailable(): Boolean {
        // Simple fallback check - in reality, requires PackageManager feature check
        return true 
    }

    private fun getKeyFromKeystore(alias: String): java.security.Key {
        val ks = KeyStore.getInstance("AndroidKeyStore")
        ks.load(null)
        return ks.getKey(alias, null)
    }

    @ReactMethod(isBlockingSynchronousMethod = true)
    fun generateDeviceDEK(alias: String): ByteArray {
      val keySpec = KeyGenParameterSpec.Builder(alias,
        KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
        .setKeySize(256)
        .setIsStrongBoxBacked(isStrongBoxAvailable())
        .build()
      val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
      kg.init(keySpec)
      kg.generateKey() 
      return ByteArray(0)
    }

    @ReactMethod(isBlockingSynchronousMethod = true)
    fun wrapDEK(keyAlias: String, dek: ByteArray): ByteArray {
      val key = getKeyFromKeystore(keyAlias)
      val cipher = Cipher.getInstance("AES/GCM/NoPadding")
      cipher.init(Cipher.ENCRYPT_MODE, key)
      val iv         = cipher.iv          
      val ciphertext = cipher.doFinal(dek)
      return iv + ciphertext              
    }

    @ReactMethod(isBlockingSynchronousMethod = true)
    fun unwrapDEK(keyAlias: String, wrappedDek: ByteArray): ByteArray {
      val key    = getKeyFromKeystore(keyAlias)
      val iv     = wrappedDek.sliceArray(0 until 12)
      val cipher = wrappedDek.sliceArray(12 until wrappedDek.size)
      val c      = Cipher.getInstance("AES/GCM/NoPadding")
      val spec   = GCMParameterSpec(128, iv)
      c.init(Cipher.DECRYPT_MODE, key, spec)
      return c.doFinal(cipher)
    }

    @ReactMethod(isBlockingSynchronousMethod = true)
    fun deleteKey(alias: String) {
      val ks = KeyStore.getInstance("AndroidKeyStore")
      ks.load(null)
      ks.deleteEntry(alias)
    }

    @ReactMethod(isBlockingSynchronousMethod = true)
    fun signECDSA(alias: String, data: ByteArray): ByteArray {
      val key  = getKeyFromKeystore(alias)  
      val sig  = Signature.getInstance("SHA256withECDSA")
      sig.initSign(key as PrivateKey)
      sig.update(data)
      return sig.sign()  
    }
}
