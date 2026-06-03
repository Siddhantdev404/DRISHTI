cmake_minimum_required(VERSION 3.22.1)
project(face_auth_plugin)

# 1. Include all directories from your DRISHTI spec
include_directories(
    src/main/cpp
    src/main/cpp/engine
    src/main/cpp/preprocessing
    src/main/cpp/liveness
    src/main/cpp/matching
    src/main/cpp/tflite
)

# 2. Add all source files (this matches your deep folder structure)
file(GLOB_RECURSE CPPSOURCES 
    "src/main/cpp/*.cpp"
)

# 3. Create the library
add_library(face_auth_plugin SHARED ${CPPSOURCES})

# 4. Compile options for cross-platform guard and optimizations
target_compile_options(face_auth_plugin PRIVATE -O3 -march=armv8-a+simd -fno-exceptions -fno-rtti)
target_compile_options(face_auth_plugin PRIVATE $<$<NOT:$<STREQUAL:${ANDROID_ABI},arm64-v8a>>:-DDRISHTI_NO_NEON>)

# 5. Define the prebuilt TensorFlow Lite library
add_library(tensorflowlite_c SHARED IMPORTED)
set_target_properties(tensorflowlite_c PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/src/main/jniLibs/${ANDROID_ABI}/libtensorflowlite_c.so)

# 6. Link against React Native's JSI, logging, and TFLite
target_link_libraries(face_auth_plugin
    react_native
    jsi
    log
    tensorflowlite_c
)
