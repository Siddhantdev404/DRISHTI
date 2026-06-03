#pragma once
#include <cmath>
#include <cstdint>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

namespace drishti {

inline float cosineSimilarity8(const float* a, const float* b, int dim = 128) {
    float dot = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;

#ifdef __aarch64__
    float32x4_t v_dot = vdupq_n_f32(0.0f);
    float32x4_t v_normA = vdupq_n_f32(0.0f);
    float32x4_t v_normB = vdupq_n_f32(0.0f);

    int i = 0;
    for (; i <= dim - 4; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);

        v_dot = vfmaq_f32(v_dot, va, vb);
        v_normA = vfmaq_f32(v_normA, va, va);
        v_normB = vfmaq_f32(v_normB, vb, vb);
    }

    float temp_dot[4], temp_normA[4], temp_normB[4];
    vst1q_f32(temp_dot, v_dot);
    vst1q_f32(temp_normA, v_normA);
    vst1q_f32(temp_normB, v_normB);

    dot = temp_dot[0] + temp_dot[1] + temp_dot[2] + temp_dot[3];
    normA = temp_normA[0] + temp_normA[1] + temp_normA[2] + temp_normA[3];
    normB = temp_normB[0] + temp_normB[1] + temp_normB[2] + temp_normB[3];

    for (; i < dim; ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
#else
    // Fallback for x86_64 emulator compatibility
    for (int i = 0; i < dim; ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
#endif

    if (normA == 0.0f || normB == 0.0f) return 0.0f;
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

} // namespace drishti
