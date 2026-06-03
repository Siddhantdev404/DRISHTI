cmake_minimum_required(VERSION 3.22.1)

get_filename_component(REACT_ANDROID_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../node_modules/react-native/ReactAndroid" ABSOLUTE)
get_filename_component(PROJECT_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build" ABSOLUTE)
project(Project)

include(${REACT_ANDROID_DIR}/cmake-utils/ReactNative-application.cmake)

# OpenCV Configuration
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/OpenCV-android-sdk/sdk/native/jni")
find_package(OpenCV REQUIRED)

# TensorFlow Lite Configuration (Temporarily disabled)
# set(TFLITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/tflite")
# set(TENSORFLOW_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/tensorflow")
# add_library(tensorflowlite_c SHARED IMPORTED)
# set_target_properties(tensorflowlite_c PROPERTIES IMPORTED_LOCATION
#     ${TFLITE_DIR}/jni/${ANDROID_ABI}/libtensorflowlite_jni.so)

target_sources(${PROJECT_NAME} PRIVATE
    face_auth_plugin.cpp
    engine/InferencePipeline.cpp
    tflite/TFLiteEngine.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/preprocessing"
    "${CMAKE_CURRENT_SOURCE_DIR}/liveness"
    "${CMAKE_CURRENT_SOURCE_DIR}/matching"
    "${CMAKE_CURRENT_SOURCE_DIR}/tflite"
    "D:/DRISHTI/shared"
    "${TENSORFLOW_SRC_DIR}" # For tensorflow/lite/c/c_api.h
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

target_link_libraries(${PROJECT_NAME}
    android
    log
    fbjni::fbjni
    ${OpenCV_LIBS}
)

target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-z,max-page-size=16384")
