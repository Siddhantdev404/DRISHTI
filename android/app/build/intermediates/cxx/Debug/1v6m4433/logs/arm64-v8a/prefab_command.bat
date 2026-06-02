@echo off
"C:\\Program Files\\Android\\Android Studio\\jbr\\bin\\java" ^
  --class-path ^
  "C:\\Users\\Siddhant\\.gradle\\caches\\modules-2\\files-2.1\\com.google.prefab\\cli\\2.0.0\\f2702b5ca13df54e3ca92f29d6b403fb6285d8df\\cli-2.0.0-all.jar" ^
  com.google.prefab.cli.AppKt ^
  --build-system ^
  cmake ^
  --platform ^
  android ^
  --abi ^
  arm64-v8a ^
  --os-version ^
  24 ^
  --stl ^
  c++_shared ^
  --ndk-version ^
  26 ^
  --output ^
  "C:\\Users\\Siddhant\\AppData\\Local\\Temp\\agp-prefab-staging14938508068958861403\\staged-cli-output" ^
  "C:\\Users\\Siddhant\\.gradle\\caches\\transforms-3\\5ff98d06ab70c478fe026cf4dbba4a0e\\transformed\\jetified-react-android-0.74.2-debug\\prefab" ^
  "C:\\Users\\Siddhant\\.gradle\\caches\\transforms-3\\6920a9019e6da7870c31430b8bf3be4f\\transformed\\jetified-fbjni-0.6.0\\prefab"
