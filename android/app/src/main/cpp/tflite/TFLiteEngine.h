#pragma once
#include <string>
#include <vector>
#include <android/log.h>
// No tflite include needed for stub
#include <opencv2/opencv.hpp>

// No LOG_TAG redefine

#include "tensorflow/lite/c/c_api.h"

namespace drishti {

struct BoundingBox {
    float xMin, yMin, xMax, yMax;
    float score;
};

class TFLiteEngine {
public:
    TFLiteEngine() = default;
    ~TFLiteEngine();

    bool loadModel(const std::string& faceMeshPath, const std::string& mobileFaceNetPath);
    
    // Run FaceMesh inference and return landmarks
    bool executeFaceMesh(const cv::Mat& rgbImage, float* outLandmarks, float& outConfidence);

    // Run MobileFaceNet to get embeddings
    bool executeMobileFaceNet(const cv::Mat& alignedFace, std::vector<float>& outEmbedding);

private:
    TfLiteModel* faceMeshModel_ = nullptr;
    TfLiteInterpreter* faceMeshInterpreter_ = nullptr;
    TfLiteInterpreterOptions* faceMeshOptions_ = nullptr;

    TfLiteModel* faceNetModel_ = nullptr;
    TfLiteInterpreter* faceNetInterpreter_ = nullptr;
    TfLiteInterpreterOptions* faceNetOptions_ = nullptr;
};

} // namespace drishti
