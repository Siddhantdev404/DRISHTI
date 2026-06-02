package com.drishti

import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod

class FaceAuthModule(reactContext: ReactApplicationContext) :
    ReactContextBaseJavaModule(reactContext) {

    override fun getName(): String = "FaceAuthModule"

    @ReactMethod(isBlockingSynchronousMethod = true)
    fun install(): Boolean {
        return try {
            val catalystInstance = reactApplicationContext.catalystInstance
            val jsContext = catalystInstance.javaScriptContextHolder.get()
            val callInvokerHolder = catalystInstance.jsCallInvokerHolder
            nativeInstall(jsContext, callInvokerHolder)
            true
        } catch (e: Exception) {
            false
        }
    }

    override fun onCatalystInstanceDestroy() {
        nativeCleanup()
    }

    companion object {
        init {
            System.loadLibrary("face_auth_engine")
        }

        @JvmStatic
        external fun nativeInstall(jsiRuntimePtr: Long, callInvokerHolder: Any)

        @JvmStatic
        external fun nativeCleanup()
    }
}
