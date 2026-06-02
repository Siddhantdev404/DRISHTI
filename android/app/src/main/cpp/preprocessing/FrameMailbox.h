#pragma once

#include <cstdint>
#include <cstring>
#include <atomic>
#include <new>

struct FrameData {
    uint8_t* yPlane;
    int width;
    int height;
    int stride;
    int64_t timestampNs;
    int rotationDegrees;
    bool valid;
};

class FrameMailbox {
public:
    static constexpr int MAX_Y_PLANE_BYTES = 1920 * 1080;

    FrameMailbox()
        : slotFlag_(0),
          writeWidth_(0),
          writeHeight_(0),
          writeStride_(0),
          writeTimestampNs_(0),
          writeRotation_(0),
          readWidth_(0),
          readHeight_(0),
          readStride_(0),
          readTimestampNs_(0),
          readRotation_(0) {
        writeBuffer_ = new(std::nothrow) uint8_t[MAX_Y_PLANE_BYTES];
        readBuffer_ = new(std::nothrow) uint8_t[MAX_Y_PLANE_BYTES];

        if (writeBuffer_) {
            std::memset(writeBuffer_, 0, MAX_Y_PLANE_BYTES);
        }
        if (readBuffer_) {
            std::memset(readBuffer_, 0, MAX_Y_PLANE_BYTES);
        }
    }

    ~FrameMailbox() {
        delete[] writeBuffer_;
        delete[] readBuffer_;
    }

    FrameMailbox(const FrameMailbox&) = delete;
    FrameMailbox& operator=(const FrameMailbox&) = delete;
    FrameMailbox(FrameMailbox&&) = delete;
    FrameMailbox& operator=(FrameMailbox&&) = delete;

    bool post(const uint8_t* yPlane, int width, int height, int stride,
              int64_t timestampNs, int rotationDegrees) {
        if (!writeBuffer_) {
            return false;
        }

        if (width <= 0 || height <= 0 || stride <= 0) {
            return false;
        }

        int requiredBytes = stride * height;
        if (requiredBytes > MAX_Y_PLANE_BYTES) {
            return false;
        }

        if (!yPlane) {
            return false;
        }

        for (int row = 0; row < height; row++) {
            const uint8_t* srcRow = yPlane + row * stride;
            uint8_t* dstRow = writeBuffer_ + row * stride;
            std::memcpy(dstRow, srcRow, width);
        }

        writeWidth_ = width;
        writeHeight_ = height;
        writeStride_ = stride;
        writeTimestampNs_ = timestampNs;
        writeRotation_ = rotationDegrees;

        std::atomic_thread_fence(std::memory_order_release);

        uint8_t* tempBuffer = readBuffer_;
        int tempWidth = readWidth_;
        int tempHeight = readHeight_;
        int tempStride = readStride_;
        int64_t tempTimestamp = readTimestampNs_;
        int tempRotation = readRotation_;

        readBuffer_ = writeBuffer_;
        readWidth_ = writeWidth_;
        readHeight_ = writeHeight_;
        readStride_ = writeStride_;
        readTimestampNs_ = writeTimestampNs_;
        readRotation_ = writeRotation_;

        writeBuffer_ = tempBuffer;
        writeWidth_ = tempWidth;
        writeHeight_ = tempHeight;
        writeStride_ = tempStride;
        writeTimestampNs_ = tempTimestamp;
        writeRotation_ = tempRotation;

        std::atomic_thread_fence(std::memory_order_release);

        slotFlag_.store(1, std::memory_order_release);

        return true;
    }

    bool fetch(FrameData& outFrame) {
        int expected = 1;
        if (!slotFlag_.compare_exchange_strong(expected, 0,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
            outFrame.valid = false;
            return false;
        }

        std::atomic_thread_fence(std::memory_order_acquire);

        outFrame.yPlane = readBuffer_;
        outFrame.width = readWidth_;
        outFrame.height = readHeight_;
        outFrame.stride = readStride_;
        outFrame.timestampNs = readTimestampNs_;
        outFrame.rotationDegrees = readRotation_;
        outFrame.valid = true;

        return true;
    }

    bool hasFrame() const {
        return slotFlag_.load(std::memory_order_acquire) == 1;
    }

    void drain() {
        slotFlag_.store(0, std::memory_order_release);
    }

private:
    std::atomic<int> slotFlag_;

    uint8_t* writeBuffer_;
    int writeWidth_;
    int writeHeight_;
    int writeStride_;
    int64_t writeTimestampNs_;
    int writeRotation_;

    uint8_t* readBuffer_;
    int readWidth_;
    int readHeight_;
    int readStride_;
    int64_t readTimestampNs_;
    int readRotation_;
};
