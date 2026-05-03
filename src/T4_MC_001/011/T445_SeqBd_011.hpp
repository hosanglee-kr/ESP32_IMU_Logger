/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. SIMD 메모리 정렬을 위해 내부는 MAX_DIM_PADDED로 저장하되,
 * 모델 추론(추출) 시에는 순수 요구 차원(_dim)으로 Tightly Packed 직렬화 수행.
 * 2. 런타임 초기화 병목(CPU Spike) 제거를 위한 memset 없는 O(1) 리셋 적용.
 * 3. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * 4. [방어/교정] Internal SRAM 고갈 방어: 20KB에 달하는 2D 시퀀스 텐서 배열을 
 * 정적으로 선언하지 않고, init() 시점에 MALLOC_CAP_SPIRAM으로 PSRAM에 1D로 동적 할당한다.
 * 5. [동적 할당 방어]: 텐서 버퍼는 런타임 설정(_DEF)을 배제하고 오직
 * 최대 정적 상수(_CONST)만 사용하여 OOM을 원천 차단한다.
 * ============================================================================
 * File: T445_SeqBd_011.hpp
 * Summary: TinyML Inference Sequence Tensor Builder (PSRAM Optimized)
 * ========================================================================== */
#pragma once

#include "T410_Def_011.hpp"
#include <cstddef>
#include <cstring>
#include <cstdint>

class T445_SequenceBuilder {
private:
    // SIMD 16바이트 정렬을 위한 1프레임당 최대 패딩 차원 수 (예: 39 -> 40)
    static constexpr uint16_t MAX_DIM_PADDED = (SmeaConfig::System::MFCC_TOTAL_DIM_CONST + 3) & ~3;

    // [메모리 교정] 20KB 정적 배열을 PSRAM 1D 포인터로 승격하여 Internal SRAM 보호
    float* _dataFlat;

    uint16_t _frames;       // 런타임에 설정된 시퀀스 프레임 수
    uint16_t _dim;          // 런타임에 설정된 순수 피처 차원 수 (예: 39)
    uint16_t _strideDim;    // SIMD 정렬을 위해 패딩이 포함된 스트라이드 차원 수 (예: 40)
    uint16_t _head;         // 링버퍼(환형 큐)의 최신 삽입 인덱스
    bool     _isFull;       // 텐서가 목표 프레임만큼 꽉 찼는지 여부

public:
    T445_SequenceBuilder();
    ~T445_SequenceBuilder();

    // 동적 할당(PSRAM) 및 텐서 규격 초기화
    bool init(uint16_t p_sequenceFrames, uint16_t p_featureDim);
    
    // 신규 특징량 1프레임(1D Vector)을 시퀀스 텐서에 삽입 (SIMD 패딩 자동 처리)
    void pushVector(const float* p_vector);
    
    // 시퀀스 텐서 내부를 비우는 O(1) 링버퍼 리셋
    void reset();

    // 추론 가능한 상태(텐서가 모두 찼는지) 확인
    bool isReady() const { return _isFull; }

    // 텐서를 TFLite Micro 입력 규격에 맞게 1D 플랫 배열로 직렬화 추출 (OOM 힙 파괴 방어 적용)
    void getSequenceFlat(float* p_outBuffer, size_t p_maxOutSize) const;
};
