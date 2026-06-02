package com.drishti

import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod
import com.facebook.react.module.annotations.ReactModule

/**
 * FaceAuthModule — Native Module responsible for bootstrapping the C++ face
 * authentication engine onto the JSI runtime.
 *
 * Architecture:
 *   Kotlin (this file)
 *     → loads libProject.so (compiled from face_auth_plugin.cpp via CMake)
 *     → calls nativeInstall(jsiRuntimePtr, callInvokerHolder)
 *     → C++ installFaceAuth() registers 8 synchronous global JSI functions:
 *         startFaceAuthEngine, stopFaceAuthEngine, processVisionFrame,
 *         getFaceAuthResult, resetFaceAuthEngine, insertFaceProfile,
 *         removeFaceProfile, getFaceAuthStatus
 *
 * The CallInvoker is forwarded to C++ so the background inference thread can
 * safely schedule callbacks onto the JS thread when needed (e.g., for future
 * event-driven result delivery).
 */
@ReactModule(name = FaceAuthModule.NAME)
class FaceAuthModule(reactContext: ReactApplicationContext) :
    ReactContextBaseJavaModule(reactContext) {

    companion object {
        const val NAME = "FaceAuthModule"

        init {
            // Load the compiled C++ shared library produced by CMake.
            // ReactNative-application.cmake names this ${PROJECT_NAME},
            // which resolves to the app module's project name.
            System.loadLibrary("Project")
        }

        /**
         * JNI entry point → Java_com_drishti_FaceAuthModule_nativeInstall
         *
         * @param jsiRuntimePtr     Raw pointer to facebook::jsi::Runtime
         * @param callInvokerHolder Java-side CallInvokerHolder for thread-safe JS invocations
         */
        @JvmStatic
        external fun nativeInstall(jsiRuntimePtr: Long, callInvokerHolder: Any)

        /**
         * JNI entry point → Java_com_drishti_FaceAuthModule_nativeCleanup
         *
         * Tears down the inference thread, releases the FrameMailbox, CLAHE,
         * LivenessFSM, and LSHIndex, and nullifies the runtime pointer.
         */
        @JvmStatic
        external fun nativeCleanup()
    }

    override fun getName(): String = NAME

    /**
     * Synchronous blocking method callable from JS to trigger JSI binding installation.
     * Called once at app startup — typically from the MainApplication or the first
     * screen that needs face authentication.
     *
     * @return true if bindings were installed successfully, false on failure
     */
    @ReactMethod(isBlockingSynchronousMethod = true)
    fun install(): Boolean {
        return try {
            val catalystInstance = reactApplicationContext.catalystInstance

            // Extract the raw JSI runtime pointer address from the CatalystInstance
            val jsContext = catalystInstance.javaScriptContextHolder.get()

            // Extract the CallInvokerHolder so C++ can schedule callbacks onto the JS thread
            val callInvokerHolder = catalystInstance.jsCallInvokerHolder

            // Forward both handles to the C++ JNI layer, which calls installFaceAuth()
            nativeInstall(jsContext, callInvokerHolder)

            true
        } catch (e: Exception) {
            android.util.Log.e(NAME, "Failed to install JSI bindings", e)
            false
        }
    }

    /**
     * Called by React Native when the CatalystInstance is about to be destroyed.
     * Ensures the C++ inference thread is stopped, all native resources are freed,
     * and no dangling pointers remain to the invalidated JSI runtime.
     */
    override fun onCatalystInstanceDestroy() {
        try {
            nativeCleanup()
        } catch (e: Exception) {
            android.util.Log.e(NAME, "Native cleanup failed", e)
        }
    }
}
