/* ============================================================================
 * File: T233_Sequence_Builder_220.cpp
 * Summary: Sequence Builder Engine Implementation
 * ========================================================================== */
#include "T233_Sequence_Builder_220.h"



CL_T20_SequenceBuilder::CL_T20_SequenceBuilder() {
    _frames = T20::C10_Sys::SEQUENCE_FRAMES_MAX;
    _dim = T20::C10_DSP::MFCC_COEFFS_DEF * 3U; // [수정] 기본 계수 * 3 (MFCC + Delta + Delta2)로 직접 계산
    _head = 0;
    _is_full = false;
    memset(_data, 0, sizeof(_data));
}

void CL_T20_SequenceBuilder::begin(uint16_t sequence_frames, uint16_t feature_dim) {
    _frames = (sequence_frames > T20::C10_Sys::SEQUENCE_FRAMES_MAX) ? T20::C10_Sys::SEQUENCE_FRAMES_MAX : sequence_frames;
    _dim = feature_dim;
    _head = 0;
    _is_full = false;
    memset(_data, 0, sizeof(_data));
}

void CL_T20_SequenceBuilder::pushVector(const float* p_vector) {
    if (!p_vector || _frames == 0) return;

    // 최신 데이터를 Head 위치에 복사 (링 버퍼 구조)
    memcpy(_data[_head], p_vector, sizeof(float) * _dim);
    
    _head++;
    if (_head >= _frames) {
        _head = 0;
        _is_full = true; // 최초 1바퀴를 돌면 추론 가능한 상태로 플래그 세팅
    }
}

void CL_T20_SequenceBuilder::getSequenceFlat(float* p_out_buffer) const {
    if (!p_out_buffer || !_is_full) return;

    // 링 버퍼 특성상 시계열 순서가 섞여 있으므로, 가장 오래된 데이터부터 순서대로 재조립(Unroll)
    uint16_t oldest_idx = _head; // Head가 가리키는 곳이 덮어써질 차례 = 가장 오래된 데이터
    uint16_t offset = 0;

    for (uint16_t i = 0; i < _frames; i++) {
        uint16_t current_idx = (oldest_idx + i) % _frames;
        memcpy(&p_out_buffer[offset], _data[current_idx], sizeof(float) * _dim);
        offset += _dim;
    }
}

