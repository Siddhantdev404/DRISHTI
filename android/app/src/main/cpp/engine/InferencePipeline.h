#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include <array>
#include <mutex>
#include "preprocessing/FrameMailbox.h"
#include "tflite/TFLiteEngine.h"
#include "liveness/LivenessFSM.h"
#include "matching/LSHIndex.h"
#include "matching/CosineSimilarity.h"
#include <opencv2/opencv.hpp>

namespace drishti {

class InferencePipeline {
public:
    InferencePipeline(std::shared_ptr<FrameMailbox> mailbox, std::shared_ptr<LivenessFSM> fsm, std::shared_ptr<LSHIndex> lshIndex, const std::string& faceMeshPath, const std::string& mobileFaceNetPath);
    ~InferencePipeline();

    void start();
    void stop();

    std::string getLastMatchedId() const {
        std::lock_guard<std::mutex> lock(matchMutex_);
        return matchedId_;
    }
    float getLastMatchScore() const {
        std::lock_guard<std::mutex> lock(matchMutex_);
        return matchScore_;
    }
    uint32_t getFrameCount() const { return frameCount_.load(); }
    int32_t getNativeFps() const { return nativeFps_.load(); }
    float getLastInferenceMs() const { return lastInferenceMs_.load(); }
    float getLastConfidence() const { return lastConfidence_.load(); }
    bool isEmbeddingReady() const { return embeddingReady_.load(); }
    bool copyLastEmbedding(std::array<float, 128>& outEmbedding) const;

private:
    void run();

    std::shared_ptr<FrameMailbox> mailbox_;
    std::shared_ptr<LivenessFSM> fsm_;
    std::shared_ptr<LSHIndex> lshIndex_;
    std::unique_ptr<TFLiteEngine> tfliteEngine_;
    
    std::atomic<bool> isRunning_{false};
    std::thread workerThread_;
    std::atomic<uint32_t> frameCount_{0};
    std::atomic<int32_t> nativeFps_{0};
    std::atomic<float> lastInferenceMs_{0.0f};
    std::atomic<float> lastConfidence_{0.0f};
    std::atomic<bool> embeddingReady_{false};
    bool embeddingCapturedForPass_ = false;
    mutable std::mutex embeddingMutex_;
    std::array<float, 128> lastEmbedding_{};

    mutable std::mutex matchMutex_;
    std::string matchedId_ = "";
    float matchScore_ = 0.0f;
};

} // namespace drishti
