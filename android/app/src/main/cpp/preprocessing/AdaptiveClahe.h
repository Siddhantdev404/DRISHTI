#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef DRISHTI_USE_NEON
#include <arm_neon.h>
#endif

struct AdaptiveClahe {
    static constexpr int GRID_X = 8;
    static constexpr int GRID_Y = 8;
    static constexpr int HIST_BINS = 256;
    static constexpr int RECALC_INTERVAL = 8;
    static constexpr float CLIP_BASE = 4.0f;
    static constexpr float CLIP_SCALE = 2.5f;
    static constexpr float CLIP_MIN = 1.5f;
    static constexpr float CLIP_MAX = 3.8f;

    float currentClipLimit;
    int frameCounter;

    AdaptiveClahe() : currentClipLimit(CLIP_BASE), frameCounter(0) {}

    void reset() {
        currentClipLimit = CLIP_BASE;
        frameCounter = 0;
    }

    float computeP90(const uint8_t* yPlane, int width, int height, int stride) {
        int histogram[HIST_BINS];
        std::memset(histogram, 0, sizeof(histogram));

        int totalPixels = 0;
        int sampleStep = 4;

        for (int row = 0; row < height; row += sampleStep) {
            const uint8_t* rowPtr = yPlane + row * stride;
            for (int col = 0; col < width; col += sampleStep) {
                histogram[rowPtr[col]]++;
                totalPixels++;
            }
        }

        int target = static_cast<int>(totalPixels * 0.90f);
        int cumulative = 0;

        for (int i = 0; i < HIST_BINS; i++) {
            cumulative += histogram[i];
            if (cumulative >= target) {
                return static_cast<float>(i);
            }
        }

        return 255.0f;
    }

    void updateClipLimit(const uint8_t* yPlane, int width, int height, int stride) {
        frameCounter++;

        if (frameCounter % RECALC_INTERVAL != 1 && frameCounter != 1) {
            return;
        }

        float p90 = computeP90(yPlane, width, height, stride);
        float rawClip = CLIP_BASE - (p90 / 255.0f) * CLIP_SCALE;

        if (rawClip < CLIP_MIN) {
            rawClip = CLIP_MIN;
        }
        if (rawClip > CLIP_MAX) {
            rawClip = CLIP_MAX;
        }

        currentClipLimit = rawClip;
    }

    void buildTileHistogram(const uint8_t* tile, int tileWidth, int tileHeight,
                            int imageStride, int histogram[HIST_BINS]) {
        std::memset(histogram, 0, HIST_BINS * sizeof(int));

#ifdef DRISHTI_USE_NEON
        for (int row = 0; row < tileHeight; row++) {
            const uint8_t* rowPtr = tile + row * imageStride;
            int col = 0;

            for (; col + 16 <= tileWidth; col += 16) {
                uint8x16_t pixels = vld1q_u8(rowPtr + col);

                uint8_t buffer[16];
                vst1q_u8(buffer, pixels);

                histogram[buffer[0]]++;
                histogram[buffer[1]]++;
                histogram[buffer[2]]++;
                histogram[buffer[3]]++;
                histogram[buffer[4]]++;
                histogram[buffer[5]]++;
                histogram[buffer[6]]++;
                histogram[buffer[7]]++;
                histogram[buffer[8]]++;
                histogram[buffer[9]]++;
                histogram[buffer[10]]++;
                histogram[buffer[11]]++;
                histogram[buffer[12]]++;
                histogram[buffer[13]]++;
                histogram[buffer[14]]++;
                histogram[buffer[15]]++;
            }

            for (; col < tileWidth; col++) {
                histogram[rowPtr[col]]++;
            }
        }
#else
        for (int row = 0; row < tileHeight; row++) {
            const uint8_t* rowPtr = tile + row * imageStride;
            for (int col = 0; col < tileWidth; col++) {
                histogram[rowPtr[col]]++;
            }
        }
#endif
    }

    void clipHistogram(int histogram[HIST_BINS], int tilePixelCount) {
        int clipValue = static_cast<int>(
            (currentClipLimit * static_cast<float>(tilePixelCount)) /
            static_cast<float>(HIST_BINS));

        if (clipValue < 1) {
            clipValue = 1;
        }

        int excess = 0;

        for (int i = 0; i < HIST_BINS; i++) {
            if (histogram[i] > clipValue) {
                excess += histogram[i] - clipValue;
                histogram[i] = clipValue;
            }
        }

        int perBin = excess / HIST_BINS;
        int remainder = excess % HIST_BINS;

        for (int i = 0; i < HIST_BINS; i++) {
            histogram[i] += perBin;
        }

        for (int i = 0; i < remainder; i++) {
            histogram[i % HIST_BINS]++;
        }
    }

    void buildLUT(const int histogram[HIST_BINS], int tilePixelCount,
                  uint8_t lut[HIST_BINS]) {
        int cumulative = 0;
        float scale = 255.0f / static_cast<float>(tilePixelCount);

        for (int i = 0; i < HIST_BINS; i++) {
            cumulative += histogram[i];
            float mapped = static_cast<float>(cumulative) * scale;

            if (mapped < 0.0f) {
                mapped = 0.0f;
            }
            if (mapped > 255.0f) {
                mapped = 255.0f;
            }

            lut[i] = static_cast<uint8_t>(mapped);
        }
    }

    void applyLUTToTile(uint8_t* tile, int tileWidth, int tileHeight,
                        int imageStride, const uint8_t lut[HIST_BINS]) {
#ifdef DRISHTI_USE_NEON
        for (int row = 0; row < tileHeight; row++) {
            uint8_t* rowPtr = tile + row * imageStride;
            int col = 0;

            uint8x8x4_t lutTable;
            for (int chunk = 0; chunk < 4; chunk++) {
                for (int lane = 0; lane < 8; lane++) {
                    int idx = chunk * 8 + lane;
                    if (idx < HIST_BINS) {
                        reinterpret_cast<uint8_t*>(&lutTable.val[chunk])[lane] = lut[idx];
                    }
                }
            }

            for (; col + 8 <= tileWidth; col += 8) {
                uint8x8_t pixels = vld1_u8(rowPtr + col);

                uint8_t inBuf[8];
                uint8_t outBuf[8];
                vst1_u8(inBuf, pixels);

                outBuf[0] = lut[inBuf[0]];
                outBuf[1] = lut[inBuf[1]];
                outBuf[2] = lut[inBuf[2]];
                outBuf[3] = lut[inBuf[3]];
                outBuf[4] = lut[inBuf[4]];
                outBuf[5] = lut[inBuf[5]];
                outBuf[6] = lut[inBuf[6]];
                outBuf[7] = lut[inBuf[7]];

                uint8x8_t result = vld1_u8(outBuf);
                vst1_u8(rowPtr + col, result);
            }

            for (; col < tileWidth; col++) {
                rowPtr[col] = lut[rowPtr[col]];
            }
        }
#else
        for (int row = 0; row < tileHeight; row++) {
            uint8_t* rowPtr = tile + row * imageStride;
            for (int col = 0; col < tileWidth; col++) {
                rowPtr[col] = lut[rowPtr[col]];
            }
        }
#endif
    }

    void process(uint8_t* yPlane, int width, int height, int stride) {
        updateClipLimit(yPlane, width, height, stride);

        int tileW = width / GRID_X;
        int tileH = height / GRID_Y;

        if (tileW < 1) {
            tileW = 1;
        }
        if (tileH < 1) {
            tileH = 1;
        }

        for (int gy = 0; gy < GRID_Y; gy++) {
            for (int gx = 0; gx < GRID_X; gx++) {
                int startX = gx * tileW;
                int startY = gy * tileH;

                int currentTileW = tileW;
                int currentTileH = tileH;

                if (gx == GRID_X - 1) {
                    currentTileW = width - startX;
                }
                if (gy == GRID_Y - 1) {
                    currentTileH = height - startY;
                }

                if (currentTileW <= 0 || currentTileH <= 0) {
                    continue;
                }

                int tilePixelCount = currentTileW * currentTileH;
                uint8_t* tilePtr = yPlane + startY * stride + startX;

                int histogram[HIST_BINS];
                buildTileHistogram(tilePtr, currentTileW, currentTileH,
                                   stride, histogram);

                clipHistogram(histogram, tilePixelCount);

                uint8_t lut[HIST_BINS];
                buildLUT(histogram, tilePixelCount, lut);

                applyLUTToTile(tilePtr, currentTileW, currentTileH,
                               stride, lut);
            }
        }
    }
};
