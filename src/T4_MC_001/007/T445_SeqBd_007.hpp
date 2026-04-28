/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. SIMD 메모리 정렬을 위해 내부는 MAX_DIM_PADDED로 저장하되, 
 * 모델 추론(추출) 시에는 순수 요구 차원(_dim)으로 Tightly Packed 직렬화 수행.
 * 2. 런타임 초기화 병목(CPU Spike) 제거를 위한 memset 없는 O(1) 리셋 적용.
 * 3. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * 4. [동적 할당 방어]: 텐서 버퍼는 런타임 설정(_DEF)을 배제하고 오직 
 * 최대 정적 상수(_CONST)만 사용하여 OOM을 원천 차단한다.
 * ============================================================================
 * File: T445_SeqBd_007.hpp
 * Summary: TinyML Inference Sequence Tensor Builder
 * ========================================================================== */
#pragma once

#include "T410_Config_007.hpp"
#include <cstddef>
#include <cstring>

class T445_SequenceBuilder {
private:
    static constexpr uint16_t MAX_DIM_PADDED = (SmeaConfig::System::MFCC_TOTAL_DIM_CONST + 3) & ~3;
    
    alignas(16) float _data[SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST][MAX_DIM_PADDED];

    uint16_t _frames;
    uint16_t _dim;
    uint16_t _strideDim; 
    uint16_t _head;
    bool     _isFull;

public:
    T445_SequenceBuilder();
    ~T445_SequenceBuilder() = default;

    void init(uint16_t p_sequenceFrames, uint16_t p_featureDim);
    void pushVector(const float* p_vector);
    void reset();

    bool isReady() const { return _isFull; }

    // 텐서를 1D 플랫 배열로 직렬화 추출 (OOM 힙 파괴 방어 적용)
    void getSequenceFlat(float* p_outBuffer, size_t p_maxOutSize) const;
};

