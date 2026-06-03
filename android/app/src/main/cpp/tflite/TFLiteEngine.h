#pragma once
#include <string>
#include <vector>
#include <android/log.h>
// No tflite include needed for stub
#include <opencv2/opencv.hpp>

// No LOG_TAG redefine

namespace drishti {

struct BoundingBox {
    float xMin, yMin, xMax, yMax;
    float score;
};

class TFLiteEngine {
public:
    TFLiteEngine() = default;
    ~TFLiteEngine();

    bool loadModel(const std::string& modelPath);
    
    // Run BlazeFace detection and return the best bounding box
    std::vector<BoundingBox> detectFaces(const cv::Mat& rgbImage);

    // Stub
};

} // namespace drishti
