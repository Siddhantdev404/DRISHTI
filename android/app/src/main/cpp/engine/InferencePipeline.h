#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include "preprocessing/FrameMailbox.h"
#include "tflite/TFLiteEngine.h"
#include "liveness/LivenessFSM.h"
#include <opencv2/opencv.hpp>

namespace drishti {

class InferencePipeline {
public:
    InferencePipeline(std::shared_ptr<FrameMailbox> mailbox, std::shared_ptr<LivenessFSM> fsm, const std::string& modelPath);
    ~InferencePipeline();

    void start();
    void stop();

private:
    void run();

    std::shared_ptr<FrameMailbox> mailbox_;
    std::shared_ptr<LivenessFSM> fsm_;
    std::unique_ptr<TFLiteEngine> tfliteEngine_;
    
    std::atomic<bool> isRunning_{false};
    std::thread workerThread_;
};

} // namespace drishti
