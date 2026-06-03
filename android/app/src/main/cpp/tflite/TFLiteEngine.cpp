#include "TFLiteEngine.h"
#include <thread>

namespace drishti {

TFLiteEngine::~TFLiteEngine() {
    if (interpreter_) TfLiteInterpreterDelete(interpreter_);
    if (options_) TfLiteInterpreterOptionsDelete(options_);
    if (model_) TfLiteModelDelete(model_);
}

bool TFLiteEngine::loadModel(const std::string& modelPath) {
    model_ = TfLiteModelCreateFromFile(modelPath.c_str());
    if (!model_) {
        LOGE("Failed to load model at %s", modelPath.c_str());
        return false;
    }

    options_ = TfLiteInterpreterOptionsCreate();
    
    // Dynamic Thread Budgeting (cores / 4)
    int hardwareThreads = std::thread::hardware_concurrency();
    int tfliteThreads = (hardwareThreads > 3) ? (hardwareThreads / 4) : 1;
    TfLiteInterpreterOptionsSetNumThreads(options_, tfliteThreads);

    interpreter_ = TfLiteInterpreterCreate(model_, options_);
    if (!interpreter_) {
        LOGE("Failed to create interpreter for %s", modelPath.c_str());
        return false;
    }

    if (TfLiteInterpreterAllocateTensors(interpreter_) != kTfLiteOk) {
        LOGE("Failed to allocate tensors for %s", modelPath.c_str());
        return false;
    }

    LOGI("Successfully loaded and allocated TFLite model: %s with %d threads. Status: kTfLiteOk", modelPath.c_str(), tfliteThreads);
    return true;
}

} // namespace drishti
