package com.drishti

import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactMethod
import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.bridge.ReactContextBaseJavaModule

@ReactModule(name = UptimeModule.NAME)
class UptimeModule(context: ReactApplicationContext) :
  ReactContextBaseJavaModule(context) { // Fallback if NativeUptimeModuleSpec is not gen'd

  companion object { const val NAME = "UptimeModule" }

  override fun getName() = NAME

  // elapsedRealtime() = ms since device boot, unaffected by clock changes,
  // continues counting during deep sleep (unlike uptimeMillis())
  @ReactMethod(isBlockingSynchronousMethod = true)
  fun getUptimeMs(): Double =
    android.os.SystemClock.elapsedRealtime().toDouble()
}
