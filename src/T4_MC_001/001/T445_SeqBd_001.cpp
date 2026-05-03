/* ============================================================================
 * File: T445_SeqBd_001.cpp
 * Summary: Sequence Tensor Builder Implementation
 * ========================================================================== */
#include "T445_SeqBd_001.hpp"

T445_SequenceBuilder::T445_SequenceBuilder() {
    v_frames = SmeaConfig::Ml::MAX_SEQUENCE_FRAMES;
    v_dim = SmeaConfig::MFCC_TOTAL_DIM; 
    v_strideDim = (v_dim + 3) & ~3;
    v_head = 0;
    v_isFull = false;
}

void T445_SequenceBuilder::init(uint16_t p_sequenceFrames, uint16_t p_featureDim) {
    v_frames = p_sequenceFrames;
    if (v_frames < 1) v_frames = 1;
    if (v_frames > SmeaConfig::Ml::MAX_SEQUENCE_FRAMES) v_frames = SmeaConfig::Ml::MAX_SEQUENCE_FRAMES;

    v_dim = p_featureDim;
    if (v_dim < 1) v_dim = 1;
    if (v_dim > SmeaConfig::MFCC_TOTAL_DIM) v_dim = SmeaConfig::MFCC_TOTAL_DIM;

    v_strideDim = (v_dim + 3) & ~3;
    
    v_head = 0;
    v_isFull = false;
}

void T445_SequenceBuilder::pushVector(const float* p_vector) {
    if (!p_vector || v_frames == 0) return;

    memcpy(v_data[v_head], p_vector, sizeof(float) * v_dim);
    
    if (v_strideDim > v_dim) {
        memset(&v_data[v_head][v_dim], 0, (v_strideDim - v_dim) * sizeof(float));
    }
    
    v_head++;
    if (v_head >= v_frames) {
        v_head = 0;
        v_isFull = true; 
    }
}

void T445_SequenceBuilder::getSequenceFlat(float* p_outBuffer, size_t p_maxOutSize) const {
    if (!p_outBuffer || ((uintptr_t)p_outBuffer & 15) != 0) return;

    size_t v_validFrames = v_isFull ? v_frames : v_head;
    if (v_validFrames == 0) return;

    size_t v_requiredBytes = v_validFrames * v_dim * sizeof(float);
    
    // [힙 파괴 방어] 교차 검증
    if (p_maxOutSize < v_requiredBytes) return;

    uint16_t v_oldestIdx = v_isFull ? v_head : 0; 
    size_t v_offset = 0; 

    for (uint16_t i = 0; i < v_validFrames; i++) {
        uint16_t v_currentIdx = v_oldestIdx + i;
        if (v_currentIdx >= v_frames) v_currentIdx -= v_frames; 
        
        memcpy(p_outBuffer + v_offset, v_data[v_currentIdx], sizeof(float) * v_dim);
        v_offset += v_dim;
    }

    size_t v_totalTensorBytes = v_frames * v_dim * sizeof(float);
    if (v_validFrames < v_frames && p_maxOutSize >= v_totalTensorBytes) {
        memset(p_outBuffer + v_offset, 0, v_totalTensorBytes - v_requiredBytes);
    }
}

void T445_SequenceBuilder::reset() {
    v_head = 0;
    v_isFull = false;
}
