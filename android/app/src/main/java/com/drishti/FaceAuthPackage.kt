package com.drishti

import com.facebook.react.ReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.uimanager.ViewManager

/**
 * FaceAuthPackage — Registers FaceAuthModule with the React Native module registry.
 *
 * Add this to your MainApplication.kt getPackages() list:
 *   packages.add(FaceAuthPackage())
 */
class FaceAuthPackage : ReactPackage {

    override fun createNativeModules(
        reactContext: ReactApplicationContext
    ): List<NativeModule> {
        val module = FaceAuthModule(reactContext)
        return listOf(module)
    }

    override fun createViewManagers(
        reactContext: ReactApplicationContext
    ): List<ViewManager<*, *>> {
        return emptyList()
    }
}
