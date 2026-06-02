cmake_minimum_required(VERSION 3.22.1)
project(Project)
# 1. Include the official React Native configuration script.
# This macro automatically sets up the default project target under the name: ${PROJECT_NAME}
include(${REACT_ANDROID_DIR}/cmake-utils/ReactNative-application.cmake)

# 2. Append your custom face authentication implementation source files 
# directly onto the existing target instead of creating a duplicate one.
target_sources(${PROJECT_NAME} PRIVATE
    face_auth_plugin.cpp
    engine/FaceAuthHostObject.cpp
    liveness/LivenessFSM.cpp
    matching/LSHIndex.cpp
)

# 3. Add your custom project directories to the search map
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/preprocessing"
    "${CMAKE_CURRENT_SOURCE_DIR}/liveness"
    "${CMAKE_CURRENT_SOURCE_DIR}/matching"
    "D:/DRISHTI/shared"
)

if(ANDROID_ABI STREQUAL "arm64-v8a")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -march=armv8-a+simd
        -DDRISHTI_USE_NEON
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -DDRISHTI_NO_NEON
    )
endif()

# 4. Link against your required system libraries and fbjni reference hooks
target_link_libraries(${PROJECT_NAME}
    android
    log
    fbjni::fbjni
)

# 5. Force 16 KB page alignment for Android 15+ kernel compatibility.
# Without this, libProject.so may fail to load on devices with 16 KB page sizes.
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-z,max-page-size=16384")
