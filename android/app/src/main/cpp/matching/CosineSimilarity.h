#pragma once

#include <cstdint>
#include <cmath>

#ifdef DRISHTI_USE_NEON
#include <arm_neon.h>
#endif

struct CosineSimilarity {
    static constexpr int DIMENSIONS = 128;

#ifdef DRISHTI_USE_NEON

    static float dotProduct(const float* a, const float* b) {
        float32x4_t sum0 = vdupq_n_f32(0.0f);
        float32x4_t sum1 = vdupq_n_f32(0.0f);
        float32x4_t sum2 = vdupq_n_f32(0.0f);
        float32x4_t sum3 = vdupq_n_f32(0.0f);
        float32x4_t sum4 = vdupq_n_f32(0.0f);
        float32x4_t sum5 = vdupq_n_f32(0.0f);
        float32x4_t sum6 = vdupq_n_f32(0.0f);
        float32x4_t sum7 = vdupq_n_f32(0.0f);

        for (int i = 0; i < DIMENSIONS; i += 32) {
            float32x4_t a0 = vld1q_f32(a + i);
            float32x4_t b0 = vld1q_f32(b + i);
            sum0 = vmlaq_f32(sum0, a0, b0);

            float32x4_t a1 = vld1q_f32(a + i + 4);
            float32x4_t b1 = vld1q_f32(b + i + 4);
            sum1 = vmlaq_f32(sum1, a1, b1);

            float32x4_t a2 = vld1q_f32(a + i + 8);
            float32x4_t b2 = vld1q_f32(b + i + 8);
            sum2 = vmlaq_f32(sum2, a2, b2);

            float32x4_t a3 = vld1q_f32(a + i + 12);
            float32x4_t b3 = vld1q_f32(b + i + 12);
            sum3 = vmlaq_f32(sum3, a3, b3);

            float32x4_t a4 = vld1q_f32(a + i + 16);
            float32x4_t b4 = vld1q_f32(b + i + 16);
            sum4 = vmlaq_f32(sum4, a4, b4);

            float32x4_t a5 = vld1q_f32(a + i + 20);
            float32x4_t b5 = vld1q_f32(b + i + 20);
            sum5 = vmlaq_f32(sum5, a5, b5);

            float32x4_t a6 = vld1q_f32(a + i + 24);
            float32x4_t b6 = vld1q_f32(b + i + 24);
            sum6 = vmlaq_f32(sum6, a6, b6);

            float32x4_t a7 = vld1q_f32(a + i + 28);
            float32x4_t b7 = vld1q_f32(b + i + 28);
            sum7 = vmlaq_f32(sum7, a7, b7);
        }

        float32x4_t pair01 = vaddq_f32(sum0, sum1);
        float32x4_t pair23 = vaddq_f32(sum2, sum3);
        float32x4_t pair45 = vaddq_f32(sum4, sum5);
        float32x4_t pair67 = vaddq_f32(sum6, sum7);

        float32x4_t quad0123 = vaddq_f32(pair01, pair23);
        float32x4_t quad4567 = vaddq_f32(pair45, pair67);

        float32x4_t total = vaddq_f32(quad0123, quad4567);

        float32x2_t half = vadd_f32(vget_low_f32(total), vget_high_f32(total));
        float32x2_t result = vpadd_f32(half, half);

        return vget_lane_f32(result, 0);
    }

    static float magnitude(const float* v) {
        float dot = dotProduct(v, v);
        return std::sqrt(dot);
    }

    static float compute(const float* a, const float* b) {
        float dot = dotProduct(a, b);
        float magA = magnitude(a);
        float magB = magnitude(b);
        float denom = magA * magB;

        if (denom < 1e-8f) {
            return 0.0f;
        }

        float similarity = dot / denom;

        if (similarity > 1.0f) {
            similarity = 1.0f;
        }
        if (similarity < -1.0f) {
            similarity = -1.0f;
        }

        return similarity;
    }

    static float computeNormalized(const float* a, const float* b) {
        float dot = dotProduct(a, b);

        if (dot > 1.0f) {
            dot = 1.0f;
        }
        if (dot < -1.0f) {
            dot = -1.0f;
        }

        return dot;
    }

#else

    static float dotProduct(const float* a, const float* b) {
        float sum = 0.0f;

        for (int i = 0; i < DIMENSIONS; i += 8) {
            sum += a[i] * b[i];
            sum += a[i + 1] * b[i + 1];
            sum += a[i + 2] * b[i + 2];
            sum += a[i + 3] * b[i + 3];
            sum += a[i + 4] * b[i + 4];
            sum += a[i + 5] * b[i + 5];
            sum += a[i + 6] * b[i + 6];
            sum += a[i + 7] * b[i + 7];
        }

        return sum;
    }

    static float magnitude(const float* v) {
        float dot = dotProduct(v, v);
        return std::sqrt(dot);
    }

    static float compute(const float* a, const float* b) {
        float dot = dotProduct(a, b);
        float magA = magnitude(a);
        float magB = magnitude(b);
        float denom = magA * magB;

        if (denom < 1e-8f) {
            return 0.0f;
        }

        float similarity = dot / denom;

        if (similarity > 1.0f) {
            similarity = 1.0f;
        }
        if (similarity < -1.0f) {
            similarity = -1.0f;
        }

        return similarity;
    }

    static float computeNormalized(const float* a, const float* b) {
        float dot = dotProduct(a, b);

        if (dot > 1.0f) {
            dot = 1.0f;
        }
        if (dot < -1.0f) {
            dot = -1.0f;
        }

        return dot;
    }

#endif

    static float cosineDistance(const float* a, const float* b) {
        return 1.0f - compute(a, b);
    }

    static float cosineDistanceNormalized(const float* a, const float* b) {
        return 1.0f - computeNormalized(a, b);
    }

    static int findBestMatch(const float* query, const float* candidates,
                             const int candidateCount, float& outScore) {
        int bestIndex = -1;
        float bestScore = -2.0f;

        for (int c = 0; c < candidateCount; c++) {
            const float* candidate = candidates + (c * DIMENSIONS);
            float score = computeNormalized(query, candidate);

            if (score > bestScore) {
                bestScore = score;
                bestIndex = c;
            }
        }

        outScore = bestScore;
        return bestIndex;
    }
};
