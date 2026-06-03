#pragma once
#include <string>
#include <android/log.h>
#include <tensorflow/lite/c/c_api.h>

#define LOG_TAG "DrishtiTFLite"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace drishti {

class TFLiteEngine {
public:
    TFLiteEngine() = default;
    ~TFLiteEngine();

    bool loadModel(const std::string& modelPath);

private:
    TfLiteModel* model_ = nullptr;
    TfLiteInterpreterOptions* options_ = nullptr;
    TfLiteInterpreter* interpreter_ = nullptr;
};

} // namespace drishti
