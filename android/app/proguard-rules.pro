# ─── React Native ──────────────────────────────────────────────────────────────
-keep,allowobfuscation class com.facebook.react.** { *; }
-keep class com.facebook.hermes.** { *; }
-keep class com.facebook.jni.** { *; }

# Keep JSI & TurboModules (critical for native C++ <-> Java bridge)
-keep class com.facebook.react.turbomodule.** { *; }
-keep class com.facebook.react.bridge.** { *; }
-keepclassmembers class com.facebook.react.bridge.CatalystInstanceImpl {
    *;
}
-keepclassmembers class com.facebook.react.bridge.JavaScriptContextHolder {
    *;
}

# ─── react-native-worklets-core ────────────────────────────────────────────────
# Without this, R8 strips the Worklets native module and Frame Processors fail
-keep class com.margelo.worklets.** { *; }

# ─── react-native-vision-camera ───────────────────────────────────────────────
-keep class com.mrousavy.camera.** { *; }

# ─── DRISHTI native modules ───────────────────────────────────────────────────
-keep class com.drishti.FaceAuthModule { *; }
-keep class com.drishti.FaceAuthPackage { *; }

# ─── JNI / Native methods ─────────────────────────────────────────────────────
# Keep all classes with native methods (JNI entry points)
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

# ─── Keep annotations used by React Native ─────────────────────────────────────
-keepattributes *Annotation*
-keepattributes Signature
-keepattributes InnerClasses
-keepattributes EnclosingMethod

# ─── Suppress common React Native warnings ────────────────────────────────────
-dontwarn com.facebook.react.**
-dontwarn com.facebook.hermes.**
-dontwarn com.facebook.jni.**
