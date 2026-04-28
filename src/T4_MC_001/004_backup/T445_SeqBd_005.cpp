/* ============================================================================
 * File: T445_SeqBd_005.cpp
 * Summary: Sequence Tensor Builder Implementation
 * ========================================================================== */
#include "T445_SeqBd_005.hpp"

T445_SequenceBuilder::T445_SequenceBuilder() {
    _frames = SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST;
    _dim = SmeaConfig::System::MFCC_TOTAL_DIM_CONST; 
    _strideDim = (_dim + 3) & ~3;
    _head = 0;
    _isFull = false;
}

void T445_SequenceBuilder::init(uint16_t p_sequenceFrames, uint16_t p_featureDim) {
    _frames = p_sequenceFrames;
    if (_frames < 1) _frames = 1;
    if (_frames > SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST) _frames = SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST;

    _dim = p_featureDim;
    if (_dim < 1) _dim = 1;
    if (_dim > SmeaConfig::System::MFCC_TOTAL_DIM_CONST) _dim = SmeaConfig::System::MFCC_TOTAL_DIM_CONST;

    _strideDim = (_dim + 3) & ~3;
    
    _head = 0;
    _isFull = false;
}

void T445_SequenceBuilder::pushVector(const float* p_vector) {
    if (!p_vector || _frames == 0) return;

    memcpy(_data[_head], p_vector, sizeof(float) * _dim);
    
    if (_strideDim > _dim) {
        memset(&_data[_head][_dim], 0, (_strideDim - _dim) * sizeof(float));
    }
    
    _head++;
    if (_head >= _frames) {
        _head = 0;
        _isFull = true; 
    }
}

void T445_SequenceBuilder::getSequenceFlat(float* p_outBuffer, size_t p_maxOutSize) const {
    if (!p_outBuffer || ((uintptr_t)p_outBuffer & 15) != 0) return;

    size_t v_validFrames = _isFull ? _frames : _head;
    if (v_validFrames == 0) return;

    size_t v_requiredBytes = v_validFrames * _dim * sizeof(float);
    
    // [힙 파괴 방어] 교차 검증
    if (p_maxOutSize < v_requiredBytes) return;

    uint16_t v_oldestIdx = _isFull ? _head : 0; 
    size_t v_offset = 0; 

    for (uint16_t i = 0; i < v_validFrames; i++) {
        uint16_t v_currentIdx = v_oldestIdx + i;
        if (v_currentIdx >= _frames) v_currentIdx -= _frames; 
        
        memcpy(p_outBuffer + v_offset, _data[v_currentIdx], sizeof(float) * _dim);
        v_offset += _dim;
    }

    size_t v_totalTensorBytes = _frames * _dim * sizeof(float);
    if (v_validFrames < _frames && p_maxOutSize >= v_totalTensorBytes) {
        memset(p_outBuffer + v_offset, 0, v_totalTensorBytes - v_requiredBytes);
    }
}

void T445_SequenceBuilder::reset() {
    _head = 0;
    _isFull = false;
}
