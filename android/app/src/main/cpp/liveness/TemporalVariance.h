#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <array>

struct VarianceAccumulator {
    static constexpr int WINDOW_SIZE = 10;
    static constexpr int MAX_FRAMES = 10;
    static constexpr int PIXEL_COUNT = WINDOW_SIZE * WINDOW_SIZE;

    std::array<std::array<float, PIXEL_COUNT>, MAX_FRAMES> frameBuffers;
    int frameIndex;
    bool filled;

    VarianceAccumulator() : frameBuffers(), frameIndex(0), filled(false) {
        for (int f = 0; f < MAX_FRAMES; f++) {
            for (int p = 0; p < PIXEL_COUNT; p++) {
                frameBuffers[f][p] = 0.0f;
            }
        }
    }

    void reset() {
        frameIndex = 0;
        filled = false;
        for (int f = 0; f < MAX_FRAMES; f++) {
            for (int p = 0; p < PIXEL_COUNT; p++) {
                frameBuffers[f][p] = 0.0f;
            }
        }
    }

    void pushFrame(const uint8_t* grayPixels, int imageWidth, int imageHeight,
                   int cropX, int cropY) {
        int safeX = cropX;
        int safeY = cropY;

        if (safeX < 0) {
            safeX = 0;
        }
        if (safeY < 0) {
            safeY = 0;
        }
        if (safeX + WINDOW_SIZE > imageWidth) {
            safeX = imageWidth - WINDOW_SIZE;
        }
        if (safeY + WINDOW_SIZE > imageHeight) {
            safeY = imageHeight - WINDOW_SIZE;
        }
        if (safeX < 0) {
            safeX = 0;
        }
        if (safeY < 0) {
            safeY = 0;
        }

        for (int row = 0; row < WINDOW_SIZE; row++) {
            for (int col = 0; col < WINDOW_SIZE; col++) {
                int srcRow = safeY + row;
                int srcCol = safeX + col;
                int srcIdx = srcRow * imageWidth + srcCol;
                int dstIdx = row * WINDOW_SIZE + col;

                if (srcRow < imageHeight && srcCol < imageWidth) {
                    frameBuffers[frameIndex][dstIdx] =
                        static_cast<float>(grayPixels[srcIdx]) / 255.0f;
                } else {
                    frameBuffers[frameIndex][dstIdx] = 0.0f;
                }
            }
        }

        frameIndex++;
        if (frameIndex >= MAX_FRAMES) {
            frameIndex = 0;
            filled = true;
        }
    }

    float computeVariance() const {
        int totalFrames = filled ? MAX_FRAMES : frameIndex;

        if (totalFrames < 2) {
            return 1.0f;
        }

        float totalVariance = 0.0f;

        for (int p = 0; p < PIXEL_COUNT; p++) {
            float sum = 0.0f;
            for (int f = 0; f < totalFrames; f++) {
                sum += frameBuffers[f][p];
            }
            float mean = sum / static_cast<float>(totalFrames);

            float sqDiffSum = 0.0f;
            for (int f = 0; f < totalFrames; f++) {
                float diff = frameBuffers[f][p] - mean;
                sqDiffSum += diff * diff;
            }
            float pixelVariance = sqDiffSum / static_cast<float>(totalFrames - 1);

            totalVariance += pixelVariance;
        }

        float avgVariance = totalVariance / static_cast<float>(PIXEL_COUNT);

        float stdDev = std::sqrt(avgVariance);

        return stdDev;
    }
};
