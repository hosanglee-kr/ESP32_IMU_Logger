

/* ============================================================================
 * File: T445_SeqBd_003.hpp
 * Summary: TinyML Inference Sequence Tensor Builder
 * * [AI 메모: 방어 원칙 적용]
 * 1. SIMD 메모리 정렬을 위해 내부는 MAX_DIM_PADDED로 저장하되, 
 * 모델 추론(추출) 시에는 순수 요구 차원(_dim)으로 Tightly Packed 직렬화 수행.
 * 2. 런타임 초기화 병목(CPU Spike) 제거를 위한 memset 없는 O(1) 리셋 적용.
 * ========================================================================== */
#pragma once

#include "T410_Config_003.hpp"
#include <cstddef>
#include <cstring>

class T445_SequenceBuilder {
private:
    static constexpr uint16_t MAX_DIM_PADDED = (SmeaConfig::MFCC_TOTAL_DIM + 3) & ~3;
    
    alignas(16) float v_data[SmeaConfig::Ml::MAX_SEQUENCE_FRAMES][MAX_DIM_PADDED];

    uint16_t v_frames;
    uint16_t v_dim;
    uint16_t v_strideDim; 
    uint16_t v_head;
    bool     v_isFull;

public:
    T445_SequenceBuilder();
    ~T445_SequenceBuilder() = default;

    void init(uint16_t p_sequenceFrames, uint16_t p_featureDim);
    void pushVector(const float* p_vector);
    void reset();

    bool isReady() const { return v_isFull; }

    // 텐서를 1D 플랫 배열로 직렬화 추출 (OOM 힙 파괴 방어 적용)
    void getSequenceFlat(float* p_outBuffer, size_t p_maxOutSize) const;
};

