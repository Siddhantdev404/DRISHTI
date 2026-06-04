#include "InferencePipeline.h"
#include <android/log.h>
#include <chrono>
#include <algorithm>

#undef LOG_TAG
#define LOG_TAG "DrishtiInference"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace drishti {

InferencePipeline::InferencePipeline(std::shared_ptr<FrameMailbox> mailbox, std::shared_ptr<LivenessFSM> fsm, std::shared_ptr<LSHIndex> lshIndex, const std::string& faceMeshPath, const std::string& mobileFaceNetPath)
    : mailbox_(mailbox), fsm_(fsm), lshIndex_(lshIndex) {
    tfliteEngine_ = std::make_unique<TFLiteEngine>();
    
    if (!tfliteEngine_->loadModel(faceMeshPath, mobileFaceNetPath)) {
        LOGE("Failed to load TFLite models");
    } else {
        LOGI("Successfully loaded TFLite models");
    }
}

InferencePipeline::~InferencePipeline() {
    stop();
}

void InferencePipeline::start() {
    if (isRunning_) return;
    isRunning_ = true;
    workerThread_ = std::thread(&InferencePipeline::run, this);
    LOGI("InferencePipeline started");
}

void InferencePipeline::stop() {
    if (!isRunning_) return;
    isRunning_ = false;
    mailbox_->post(nullptr, 0, 0, 0, 0, 0); 
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    LOGI("InferencePipeline stopped");
}

bool InferencePipeline::copyLastEmbedding(std::array<float, 128>& outEmbedding) const {
    if (!embeddingReady_.load()) return false;
    std::lock_guard<std::mutex> lock(embeddingMutex_);
    outEmbedding = lastEmbedding_;
    return true;
}

void InferencePipeline::run() {
    auto fpsWindowStart = std::chrono::steady_clock::now();
    uint32_t fpsWindowFrames = 0;

    while (isRunning_) {
        FrameData frameData;
        if (!mailbox_->fetch(frameData)) {
            // No frame available, yield and continue
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (!isRunning_ || !frameData.yPlane) continue;
        const auto frameStart = std::chrono::steady_clock::now();

        // Convert Y-plane (grayscale) directly to cv::Mat
        cv::Mat grayMat(frameData.height, frameData.stride, CV_8UC1, frameData.yPlane);
        cv::Mat visibleGray = grayMat(cv::Rect(0, 0, frameData.width, frameData.height));
        cv::Mat rgbMat;
        cv::cvtColor(visibleGray, rgbMat, cv::COLOR_GRAY2RGB);

        // Apply rotation if needed
        if (frameData.rotationDegrees != 0) {
            if (frameData.rotationDegrees == 90) {
                cv::rotate(rgbMat, rgbMat, cv::ROTATE_90_CLOCKWISE);
            } else if (frameData.rotationDegrees == 180) {
                cv::rotate(rgbMat, rgbMat, cv::ROTATE_180);
            } else if (frameData.rotationDegrees == 270) {
                cv::rotate(rgbMat, rgbMat, cv::ROTATE_90_COUNTERCLOCKWISE);
            }
        }

        float landmarks[468 * 3] = {0};
        float confidence = 0.0f;
        
        bool success = tfliteEngine_->executeFaceMesh(rgbMat, landmarks, confidence);
        lastConfidence_.store(confidence);
        const uint32_t currentFrame = frameCount_.fetch_add(1) + 1;
        fpsWindowFrames++;

        if (!success) {
            embeddingReady_.store(false);
            if (currentFrame % 30 == 0) {
                LOGE("[DRISHTI_RUNTIME] FaceMesh inference failed at frame=%u", currentFrame);
            }
            continue;
        }

        if (currentFrame % 30 == 0) {
            LOGI("[DRISHTI_RUNTIME] FaceMesh frame=%u confidence=%.3f size=%dx%d rotation=%d",
                 currentFrame, confidence, rgbMat.cols, rgbMat.rows, frameData.rotationDegrees);
        }

        if (fsm_) {
            // FSM needs to know the correct width after rotation
            int correctWidth = (frameData.rotationDegrees == 90 || frameData.rotationDegrees == 270) ? frameData.height : frameData.width;
            auto fsmOut = fsm_->update(landmarks, confidence, correctWidth);

            if (currentFrame % 30 == 0) {
                LOGI("[DRISHTI_RUNTIME] Liveness state=%d challenge=%d ear=%.3f mar=%.3f yaw=%.1f pitch=%.1f tempVar=%.8f",
                     static_cast<int>(fsmOut.state),
                     static_cast<int>(fsmOut.challenge),
                     fsm_->getLastEar(),
                     fsm_->getLastMar(),
                     fsm_->getLastYaw(),
                     fsm_->getLastPitch(),
                     fsm_->getLastTempVariance());
            }

            if (fsmOut.state == LivenessState::LIVENESS_PASS && !embeddingCapturedForPass_) {
                std::vector<float> embedding;
                // Get aligned face from landmarks
                // Placeholder for aligned face: we just use rgbMat for now
                if (tfliteEngine_->executeMobileFaceNet(rgbMat, embedding)) {
                    if (embedding.size() == 128) {
                        {
                            std::lock_guard<std::mutex> lock(embeddingMutex_);
                            std::copy(embedding.begin(), embedding.end(), lastEmbedding_.begin());
                        }
                        embeddingReady_.store(true);
                        embeddingCapturedForPass_ = true;
                        LOGI("[DRISHTI_RUNTIME] MobileFaceNet embedding ready frame=%u", currentFrame);
                    }
                    if (lshIndex_) {
                        auto candidates = lshIndex_->query(embedding.data());
                        float bestScore = -1.0f;
                        std::string bestId = "";
                        for (const auto& c : candidates) {
                            float score = CosineSimilarity::computeNormalized(embedding.data(), c.embedding);
                            if (score > bestScore) {
                                bestScore = score;
                                bestId = c.personnelId;
                            }
                        }
                        if (bestScore > 0.65f) {
                            {
                                std::lock_guard<std::mutex> lock(matchMutex_);
                                matchedId_ = bestId;
                                matchScore_ = bestScore;
                            }
                            LOGI("[DRISHTI_RUNTIME] Verification matched id=%s score=%.3f", bestId.c_str(), bestScore);
                        } else {
                            {
                                std::lock_guard<std::mutex> lock(matchMutex_);
                                matchedId_ = "UNKNOWN";
                                matchScore_ = bestScore;
                            }
                            LOGI("[DRISHTI_RUNTIME] Verification rejected bestScore=%.3f", bestScore);
                        }
                    }
                }
            } else if (fsmOut.state == LivenessState::IDLE || fsmOut.state == LivenessState::DETECTED) {
                {
                    std::lock_guard<std::mutex> lock(matchMutex_);
                    matchedId_ = "";
                    matchScore_ = 0.0f;
                }
                embeddingCapturedForPass_ = false;
                embeddingReady_.store(false);
            }
        }
        const auto frameEnd = std::chrono::steady_clock::now();
        const auto inferenceMs = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
        lastInferenceMs_.store(inferenceMs);

        const auto fpsElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - fpsWindowStart).count();
        if (fpsElapsed >= 1000) {
            nativeFps_.store(static_cast<int32_t>((fpsWindowFrames * 1000) / std::max<int64_t>(fpsElapsed, 1)));
            fpsWindowFrames = 0;
            fpsWindowStart = frameEnd;
            LOGI("[DRISHTI_RUNTIME] Native FPS=%d inferenceMs=%.2f", nativeFps_.load(), inferenceMs);
        }
        // mailbox manages its own memory, no need to delete anything
    }
}

} // namespace drishti
