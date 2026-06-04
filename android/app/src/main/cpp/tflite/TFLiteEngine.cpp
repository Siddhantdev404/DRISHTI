#include "TFLiteEngine.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <vector>

#undef LOG_TAG
#define LOG_TAG "DrishtiTFLite"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace drishti {

TFLiteEngine::~TFLiteEngine() {
    if (faceMeshInterpreter_) TfLiteInterpreterDelete(faceMeshInterpreter_);
    if (faceMeshModel_) TfLiteModelDelete(faceMeshModel_);
    if (faceMeshOptions_) TfLiteInterpreterOptionsDelete(faceMeshOptions_);

    if (faceNetInterpreter_) TfLiteInterpreterDelete(faceNetInterpreter_);
    if (faceNetModel_) TfLiteModelDelete(faceNetModel_);
    if (faceNetOptions_) TfLiteInterpreterOptionsDelete(faceNetOptions_);
}

bool TFLiteEngine::loadModel(const std::string& faceMeshPath, const std::string& mobileFaceNetPath) {
    LOGI("Loading FaceMesh model from: %s", faceMeshPath.c_str());
    faceMeshModel_ = TfLiteModelCreateFromFile(faceMeshPath.c_str());
    if (!faceMeshModel_) {
        LOGE("Failed to create FaceMesh model");
        return false;
    }

    faceMeshOptions_ = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(faceMeshOptions_, 4);
    faceMeshInterpreter_ = TfLiteInterpreterCreate(faceMeshModel_, faceMeshOptions_);
    if (!faceMeshInterpreter_) {
        LOGE("Failed to create FaceMesh interpreter");
        return false;
    }
    if (TfLiteInterpreterAllocateTensors(faceMeshInterpreter_) != kTfLiteOk) {
        LOGE("Failed to allocate FaceMesh tensors");
        return false;
    }

    LOGI("Loading MobileFaceNet model from: %s", mobileFaceNetPath.c_str());
    faceNetModel_ = TfLiteModelCreateFromFile(mobileFaceNetPath.c_str());
    if (!faceNetModel_) {
        LOGE("Failed to create MobileFaceNet model");
        return false;
    }

    faceNetOptions_ = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(faceNetOptions_, 2);
    faceNetInterpreter_ = TfLiteInterpreterCreate(faceNetModel_, faceNetOptions_);
    if (!faceNetInterpreter_) {
        LOGE("Failed to create MobileFaceNet interpreter");
        return false;
    }
    if (TfLiteInterpreterAllocateTensors(faceNetInterpreter_) != kTfLiteOk) {
        LOGE("Failed to allocate MobileFaceNet tensors");
        return false;
    }

    return true;
}

bool TFLiteEngine::executeFaceMesh(const cv::Mat& rgbImage, float* outLandmarks, float& outConfidence) {
    if (!faceMeshInterpreter_) return false;

    TfLiteTensor* inputTensor = TfLiteInterpreterGetInputTensor(faceMeshInterpreter_, 0);
    int inputHeight = TfLiteTensorDim(inputTensor, 1);
    int inputWidth = TfLiteTensorDim(inputTensor, 2);
    int inputChannels = TfLiteTensorDim(inputTensor, 3);
    (void)inputChannels;

    cv::Mat resized;
    cv::resize(rgbImage, resized, cv::Size(inputWidth, inputHeight));
    
    // Convert to float32 or int8 depending on the model. 
    // Usually FaceMesh v2 is float32 [-1, 1] or [0, 1]. Let's assume float32 [0, 1] or it's uint8.
    // We will just copy the data in. If the model requires float32, we do the conversion.
    if (TfLiteTensorType(inputTensor) == kTfLiteFloat32) {
        resized.convertTo(resized, CV_32FC3, 1.0f / 127.5f, -1.0f);
        if (TfLiteTensorCopyFromBuffer(inputTensor, resized.data, resized.total() * resized.elemSize()) != kTfLiteOk) {
            LOGE("FaceMesh input copy failed");
            return false;
        }
    } else {
        if (TfLiteTensorCopyFromBuffer(inputTensor, resized.data, resized.total() * resized.elemSize()) != kTfLiteOk) {
            LOGE("FaceMesh input copy failed");
            return false;
        }
    }

    if (TfLiteInterpreterInvoke(faceMeshInterpreter_) != kTfLiteOk) {
        LOGE("FaceMesh inference failed");
        return false;
    }

    const TfLiteTensor* landmarksTensor = nullptr;
    const TfLiteTensor* scoreTensor = nullptr;
    const int32_t outputCount = TfLiteInterpreterGetOutputTensorCount(faceMeshInterpreter_);
    size_t largestOutputBytes = 0;
    for (int32_t i = 0; i < outputCount; i++) {
        const TfLiteTensor* tensor = TfLiteInterpreterGetOutputTensor(faceMeshInterpreter_, i);
        if (!tensor) continue;
        const size_t tensorBytes = TfLiteTensorByteSize(tensor);
        if (tensorBytes > largestOutputBytes) {
            largestOutputBytes = tensorBytes;
            landmarksTensor = tensor;
        }
    }
    for (int32_t i = 0; i < outputCount; i++) {
        const TfLiteTensor* tensor = TfLiteInterpreterGetOutputTensor(faceMeshInterpreter_, i);
        if (!tensor || tensor == landmarksTensor) continue;
        if (TfLiteTensorByteSize(tensor) <= 16) {
            scoreTensor = tensor;
            break;
        }
    }
    
    if (!landmarksTensor) return false;
    
    // Copy landmarks
    if (outLandmarks) {
        size_t bytesToCopy = std::min(TfLiteTensorByteSize(landmarksTensor), static_cast<size_t>(468 * 3 * sizeof(float)));
        if (TfLiteTensorCopyToBuffer(landmarksTensor, outLandmarks, bytesToCopy) != kTfLiteOk) {
            LOGE("FaceMesh landmark copy failed");
            return false;
        }
    }
    
    if (scoreTensor && TfLiteTensorType(scoreTensor) == kTfLiteFloat32) {
        float score = 0;
        TfLiteTensorCopyToBuffer(scoreTensor, &score, sizeof(float));
        outConfidence = score;
        if (outConfidence < 0.0f || outConfidence > 1.0f) {
            outConfidence = 1.0f / (1.0f + std::exp(-outConfidence));
        }
    } else {
        outConfidence = 1.0f;
    }

    return true;
}

bool TFLiteEngine::executeMobileFaceNet(const cv::Mat& alignedFace, std::vector<float>& outEmbedding) {
    if (!faceNetInterpreter_) return false;

    TfLiteTensor* inputTensor = TfLiteInterpreterGetInputTensor(faceNetInterpreter_, 0);
    if (!inputTensor) return false;

    int inputHeight = TfLiteTensorDim(inputTensor, 1);
    int inputWidth = TfLiteTensorDim(inputTensor, 2);
    int inputChannels = TfLiteTensorDim(inputTensor, 3);
    if (inputHeight <= 0 || inputWidth <= 0 || inputChannels <= 0) {
        LOGE("MobileFaceNet input tensor has invalid shape");
        return false;
    }

    cv::Mat resized;
    cv::resize(alignedFace, resized, cv::Size(inputWidth, inputHeight));
    if (resized.channels() != inputChannels) {
        if (inputChannels == 1) {
            cv::cvtColor(resized, resized, cv::COLOR_RGB2GRAY);
        } else if (inputChannels == 3 && resized.channels() == 1) {
            cv::cvtColor(resized, resized, cv::COLOR_GRAY2RGB);
        }
    }

    const TfLiteType inputType = TfLiteTensorType(inputTensor);
    if (inputType == kTfLiteFloat32) {
        cv::Mat floatInput;
        resized.convertTo(floatInput, CV_32FC(inputChannels), 1.0f / 127.5f, -1.0f);
        if (TfLiteTensorCopyFromBuffer(inputTensor, floatInput.data, TfLiteTensorByteSize(inputTensor)) != kTfLiteOk) {
            LOGE("MobileFaceNet float input copy failed");
            return false;
        }
    } else if (inputType == kTfLiteUInt8 || inputType == kTfLiteInt8) {
        if (TfLiteTensorCopyFromBuffer(inputTensor, resized.data, TfLiteTensorByteSize(inputTensor)) != kTfLiteOk) {
            LOGE("MobileFaceNet quantized input copy failed");
            return false;
        }
    } else {
        LOGE("Unsupported MobileFaceNet input tensor type: %d", inputType);
        return false;
    }

    if (TfLiteInterpreterInvoke(faceNetInterpreter_) != kTfLiteOk) {
        LOGE("MobileFaceNet inference failed");
        return false;
    }

    const TfLiteTensor* outputTensor = TfLiteInterpreterGetOutputTensor(faceNetInterpreter_, 0);
    if (!outputTensor) return false;

    const size_t outputBytes = TfLiteTensorByteSize(outputTensor);
    const TfLiteType outputType = TfLiteTensorType(outputTensor);
    outEmbedding.assign(128, 0.0f);

    if (outputType == kTfLiteFloat32) {
        std::vector<float> fullOutput(outputBytes / sizeof(float));
        if (TfLiteTensorCopyToBuffer(outputTensor, fullOutput.data(), outputBytes) != kTfLiteOk) {
            LOGE("MobileFaceNet float output copy failed");
            return false;
        }
        const size_t floats = std::min<size_t>(128, fullOutput.size());
        std::copy(fullOutput.begin(), fullOutput.begin() + floats, outEmbedding.begin());
    } else if (outputType == kTfLiteUInt8 || outputType == kTfLiteInt8) {
        std::vector<uint8_t> quantized(outputBytes);
        if (TfLiteTensorCopyToBuffer(outputTensor, quantized.data(), outputBytes) != kTfLiteOk) {
            LOGE("MobileFaceNet quantized output copy failed");
            return false;
        }
        TfLiteQuantizationParams params = TfLiteTensorQuantizationParams(outputTensor);
        const size_t dims = std::min<size_t>(128, outputBytes);
        for (size_t i = 0; i < dims; i++) {
            int32_t raw = outputType == kTfLiteInt8
                ? static_cast<int32_t>(reinterpret_cast<int8_t*>(quantized.data())[i])
                : static_cast<int32_t>(quantized[i]);
            outEmbedding[i] = (raw - params.zero_point) * params.scale;
        }
    } else {
        LOGE("Unsupported MobileFaceNet output tensor type: %d", outputType);
        return false;
    }

    float norm = 0.0f;
    for (float value : outEmbedding) norm += value * value;
    norm = std::sqrt(norm);
    if (norm <= 1e-6f) {
        LOGE("MobileFaceNet produced near-zero embedding");
        return false;
    }
    for (float& value : outEmbedding) value /= norm;

    LOGI("[DRISHTI_RUNTIME] MobileFaceNet inference produced normalized embedding");
    return true;
}

} // namespace drishti
