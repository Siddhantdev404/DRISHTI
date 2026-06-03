#pragma once
#include <atomic>
#include <mutex>
#include <cstdint>

namespace drishti {

struct FrameData {
    uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int64_t timestamp = 0;
};

class FrameMailbox {
public:
    void pushFrame(const FrameData& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        latestFrame_ = frame;
        // ARM memory fence: ensures inference thread sees fully written frame data
        hasNewFrame_.store(true, std::memory_order_release);
    }

    bool popFrame(FrameData& outFrame) {
        if (!hasNewFrame_.load(std::memory_order_acquire)) {
            return false; // Prevent consumption of stale frames
        }
        std::lock_guard<std::mutex> lock(mutex_);
        outFrame = latestFrame_;
        hasNewFrame_.store(false, std::memory_order_release);
        return true;
    }

private:
    std::mutex mutex_;
    FrameData latestFrame_;
    std::atomic<bool> hasNewFrame_{false};
};

} // namespace drishti
