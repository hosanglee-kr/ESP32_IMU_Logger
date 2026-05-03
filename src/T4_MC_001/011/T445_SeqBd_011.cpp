/* ============================================================================
 * File: T445_SeqBd_011.cpp
 * Summary: Sequence Tensor Builder Implementation (PSRAM & SIMD Optimized)
 * ============================================================================
 * * [AI 메모: 마이그레이션 적용 완료 사항]
 * - 2D 정적 배열을 PSRAM 1D Flat 배열로 포인터 연산 전환 완료 (SRAM 확보).
 * - SIMD 처리용 패딩 영역(Garbage 값)에 대한 NaN 독성 차단(memset 0.0f) 유지.
 * - TFLite 등 직렬화 추출 시 Zero-padding (시퀀스 미달 시 0으로 채움) 로직 유지.
 * ========================================================================== */

#include "T445_SeqBd_011.hpp"
#include "esp_log.h"

static const char* TAG = "T445_SEQ";

T445_SequenceBuilder::T445_SequenceBuilder() {
    _dataFlat = nullptr;
    _frames = SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST;
    _dim = SmeaConfig::System::MFCC_TOTAL_DIM_CONST;
    _strideDim = (_dim + 3) & ~3;
    _head = 0;
    _isFull = false;
}

T445_SequenceBuilder::~T445_SequenceBuilder() {
    if (_dataFlat) {
        heap_caps_free(_dataFlat);
        _dataFlat = nullptr;
    }
}

bool T445_SequenceBuilder::init(uint16_t p_sequenceFrames, uint16_t p_featureDim) {
    // 1. [방어] 프레임 및 차원 수 상하한 바운더리 클램프
    _frames = p_sequenceFrames;
    if (_frames < 1) _frames = 1;
    if (_frames > SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST) _frames = SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST;

    _dim = p_featureDim;
    if (_dim < 1) _dim = 1;
    if (_dim > SmeaConfig::System::MFCC_TOTAL_DIM_CONST) _dim = SmeaConfig::System::MFCC_TOTAL_DIM_CONST;

    // SIMD 처리를 위한 스트라이드(패딩 포함) 크기 도출
    _strideDim = (_dim + 3) & ~3;

    // 2. [교정] Internal SRAM 보호를 위한 20KB 텐서 버퍼 PSRAM 동적 할당 (16바이트 정렬)
    if (!_dataFlat) {
        size_t v_allocSize = SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST * MAX_DIM_PADDED * sizeof(float);
        _dataFlat = (float*)heap_caps_aligned_alloc(16, v_allocSize, MALLOC_CAP_SPIRAM);
        
        if (!_dataFlat) {
            ESP_LOGE(TAG, "[CRITICAL] PSRAM Allocation Failed for SeqBuilder! (Req: %zu Bytes)", v_allocSize);
            return false;
        }
        // 초기화 시 전체 0.0f 패딩
        memset(_dataFlat, 0, v_allocSize);
    }

    _head = 0;
    _isFull = false;
    return true;
}

void T445_SequenceBuilder::pushVector(const float* p_vector) {
    if (!p_vector || !_dataFlat || _frames == 0) return;

    // 삽입될 1D Flat 버퍼의 시작 위치 계산 (stride 기준)
    float* v_targetPtr = &_dataFlat[_head * MAX_DIM_PADDED];

    // 1. 유효 차원(_dim)만큼 데이터 고속 복사
    memcpy(v_targetPtr, p_vector, sizeof(float) * _dim);

    // 2. [방어] SIMD NaN 독성 전염을 막기 위해 패딩 영역(Garbage)을 0.0f로 클리어
    if (_strideDim > _dim) {
        memset(v_targetPtr + _dim, 0, (_strideDim - _dim) * sizeof(float));
    }

    // 3. 링버퍼(환형 큐) 인덱스 회전
    _head++;
    if (_head >= _frames) {
        _head = 0;
        _isFull = true;
    }
}

void T445_SequenceBuilder::getSequenceFlat(float* p_outBuffer, size_t p_maxOutSize) const {
    // [방어] SIMD 처리를 위한 16바이트(15 비트마스킹) 메모리 정렬 검사 및 할당 확인
    if (!p_outBuffer || !_dataFlat || ((uintptr_t)p_outBuffer & 15) != 0) return;

    size_t v_validFrames = _isFull ? _frames : _head;
    if (v_validFrames == 0) return;

    // 패딩이 제거된 '순수 차원(_dim)' 기준으로만 직렬화 요구 바이트 계산
    size_t v_requiredBytes = v_validFrames * _dim * sizeof(float);

    // [힙 파괴 방어] 출력 버퍼의 크기가 요구량보다 작으면 메모리 침범 방지를 위해 즉시 중단
    if (p_maxOutSize < v_requiredBytes) return;

    // 링버퍼 내에서 시간순(가장 오래된 데이터부터) 추출 시작 위치
    uint16_t v_oldestIdx = _isFull ? _head : 0;
    size_t v_offset = 0;

    for (uint16_t i = 0; i < v_validFrames; i++) {
        uint16_t v_currentIdx = v_oldestIdx + i;
        if (v_currentIdx >= _frames) v_currentIdx -= _frames;

        // 원본 배열(패딩 포함된 위치)에서 순수 데이터만 추출하여 Tightly Packed 조립
        memcpy(p_outBuffer + v_offset, &_dataFlat[v_currentIdx * MAX_DIM_PADDED], sizeof(float) * _dim);
        v_offset += _dim;
    }

    // [방어/ML] 아직 시퀀스(예: 128프레임)가 다 채워지지 않은 상태에서 추출을 요구받으면, 
    // 잔여 공간을 Zero-Padding 처리하여 TFLite 추론 모델의 텐서 규격을 깨지 않도록 보정
    size_t v_totalTensorBytes = _frames * _dim * sizeof(float);
    if (v_validFrames < _frames && p_maxOutSize >= v_totalTensorBytes) {
        memset(p_outBuffer + v_offset, 0, v_totalTensorBytes - v_requiredBytes);
    }
}

void T445_SequenceBuilder::reset() {
    // memset 없이 O(1)의 속도로 링버퍼 포인터만 초기화
    _head = 0;
    _isFull = false;
}
