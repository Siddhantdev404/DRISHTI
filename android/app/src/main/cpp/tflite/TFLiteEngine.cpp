#include "TFLiteEngine.h"
#include <android/log.h>

#undef LOG_TAG
#define LOG_TAG "DrishtiTFLite"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace drishti {

TFLiteEngine::~TFLiteEngine() {
    // Stub
}

bool TFLiteEngine::loadModel(const std::string& modelPath) {
    (void)modelPath;
    LOGI("TFLiteEngine::loadModel called with path: %s", modelPath.c_str());
    // Since we are using OpenCV Haar Cascade for now to prove the pipeline,
    // we don't actually need to load TFLite model in this version.
    return true;
}

std::vector<BoundingBox> TFLiteEngine::detectFaces(const cv::Mat& rgbImage) {
    (void)rgbImage;
    return {};
}

} // namespace drishti
