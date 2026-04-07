/* ============================================================================
 * File: T233_Sequence_Builder_217.h
 * Summary: TinyML 추론을 위한 2D 텐서(Sequence) 조립 엔진
 * * [시퀀스 버퍼링(Sequence Buffering) 구현 검토 요약]
 * 1. 로깅(Storage) 관점:
 * - 단일 프레임 16개와 시퀀스 1개(16x39)는 본질적으로 동일한 데이터임.
 * - 이미 StorageService에 1024B Zero-Copy DMA 버퍼링이 적용되어 있으므로, 
 * 순수 로깅 목적이라면 단일 벡터를 스트리밍하는 것이 구조적 오버헤드가 적음.
 * 2. 웹 차트(Viewer) 관점:
 * - 16프레임을 묶어서 전송하면 네트워크 횟수는 줄지만 차트 렌더링이 뚝뚝 끊어짐.
 * - 1프레임 단위 전송이 시각적으로 부드러운 실시간 파형 모니터링에 적합함.
 * 3. TinyML 서빙 관점 (핵심 도입 사유):
 * - Autoencoder, CNN/RNN 등의 인공지능 모델은 고정된 2D Tensor(예: 16x39)를 입력받음.
 * - 연속적인 시계열 판단(Sliding Window)을 위해서는 가장 최근의 N개 프레임을 
 * 메모리에 상주시키고, 1프레임이 들어올 때마다 가장 오래된 데이터를 밀어내는 
 * '링 버퍼(Ring Buffer)' 구조의 시퀀스 조립기가 필수적임.
 * => [결론] 향후 ESP32 내부에서의 엣지 AI 구동을 위해 DspPipeline 직후에 
 * Sequence Builder 모듈을 부착하되, 웹 설정(runtime config)을 통해 
 * 사용자가 단일 프레임 출력과 시퀀스 출력을 자유롭게 토글할 수 있도록 분리 구현함.
 * ========================================================================== */
#pragma once

#include "T210_Def_Com_217.h"
#include "T214_Def_Rec_217.h"
#include <string.h>

class CL_T20_SequenceBuilder {
public:
    CL_T20_SequenceBuilder();
    ~CL_T20_SequenceBuilder() = default;

    // 시퀀스 프레임 수 및 벡터 차원 설정
    void begin(uint16_t sequence_frames, uint16_t feature_dim);
    
    // 신규 단일 프레임 푸시 (가장 오래된 데이터는 덮어쓰기됨)
    void pushVector(const float* p_vector);
    
    // 버퍼가 꽉 차서 TinyML 추론(또는 묶음 전송)이 가능한 상태인지 확인
    bool isReady() const { return _is_full; }
    
    // 2D 텐서 데이터를 1차원 Flat 배열로 복사 (ML 모델 입력용)
    void getSequenceFlat(float* p_out_buffer) const;
    
    // 현재 설정된 시퀀스 길이 조회
    uint16_t getSequenceFrames() const { return _frames; }
    uint16_t getFeatureDim() const { return _dim; }

private:
    float    _data[T20::C10_Sys::SEQUENCE_FRAMES_MAX][T20::C10_DSP::MFCC_COEFFS_MAX * 3];
    uint16_t _frames;
    uint16_t _dim;
    uint16_t _head;
    bool     _is_full;
};

