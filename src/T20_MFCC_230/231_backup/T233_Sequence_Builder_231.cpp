/* ============================================================================
 * File: T233_Sequence_Builder_231.cpp
 * Summary: Sequence Builder Engine Implementation
 * ========================================================================== */
#include "T233_Sequence_Builder_231.h"



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

/* ============================================================================
 * 버퍼 상태 강제 초기화 (세션 재시작용)
 * ========================================================================== */
void CL_T20_SequenceBuilder::reset() {
    _head = 0;
    _is_full = false;
    memset(_data, 0, sizeof(_data));
}





- 현재까지 확인된 이슈

### 🚨 [최종 집대성] T20 시퀀스 빌더(T233) 극한 스트레스 보완 통합 리스트 (총 17건)
#### **A. 메모리 파괴 및 하드웨어(SIMD) 패닉 방어**
 1. **[메모리 파괴] Feature 차원 최대치 초과 (begin):** feature_dim 외부 주입 시 배열 경계 파괴. ➔ MAX_FEATURE_DIM 클램핑.
 2. **[HW 패닉] Feature 차원 하한선(0) 누락 (begin):** 차원 0 입력 시 0 나누기 및 복사 무효화 패닉. ➔ 최소값 1 이상 방어.
 3. **[코어 패닉] 시퀀스 프레임 0값 입력 방어 (getSequenceFlat):** sequence_frames 0 입력 시 % 연산 패닉. ➔ 최소값 1 이상 방어.
 4. **[HW 패닉] 2D 배열 Row 물리적 비정렬 (헤더 선언):** 2D 배열 열(Column)이 16의 배수가 아니면 두 번째 행부터 정렬 붕괴. ➔ 열 크기를 16바이트 패딩 규격화.
 5. **[HW 패닉] TinyML 텐서 메모리 비정렬 (getSequenceFlat):** 추출 시 패딩 스트라이드가 안 맞으면 추론 엔진(TFLite) LoadProhibited 패닉. ➔ 추출 시 16바이트 패딩 스트라이드(Stride) 적용.
 6. **[힙 파괴] 외부 출력 버퍼 크기 검증 누락 (getSequenceFlat):** p_out_buffer의 실제 크기를 모른 채 memcpy 수행. ➔ 인자에 max_out_size 추가 및 안전 경계(Boundary Check) 구축.
 7. **[🆕 신규: HW 패닉] 출력 버퍼 포인터 정렬 검증 누락 (getSequenceFlat):** 외부에서 주입된 p_out_buffer 포인터 자체가 16바이트 정렬되지 않았을 경우 텐서 복사 후 AI 연산 시 패닉 유발. ➔ 포인터 주소 비트 검사(& 15) 방어벽 추가.
#### **B. 데이터 무결성 및 AI 추론 정합성 보장**
 8. **[논리 결함] 부분 플러시(Partial Flush) 시작 인덱스 꼬임 (getSequenceFlat):** 버퍼가 덜 찼을 때(!_is_full), 가장 오래된 데이터는 _head가 아닌 0번 인덱스임. ➔ 상태에 따른 읽기 시작점 분기(_is_full ? _head : 0).
 9. **[데이터 유실] Early Stop 시 부분 시퀀스 증발 (getSequenceFlat):** 정지 명령 시 수집된 파형까지 날아감. ➔ 가동 중단 시 수집된 프레임까지만 안전하게 추출(Partial Flush) 허용.
 10. **[AI 환각] 부분 플러시 중 낡은 쓰레기값 혼입 (getSequenceFlat):** 덜 찬 텐서 공간에 이전 세션의 쓰레기값이 들어가 AI 오작동 유발. ➔ 유효 데이터 이후의 잉여 영역은 0.0f로 명시적 제로 패딩.
 11. **[데이터 오염] 패딩 영역(Tail) 쓰레기값 방치 (pushVector):** SIMD 정렬용으로 확장된 꼬리 공간에 과거 쓰레기값 방치. ➔ 데이터 복사 직후 꼬리 영역 0.0f 덮어쓰기.
 12. **[힙 파괴] 버퍼 오프셋 자료형 오버플로우 (getSequenceFlat):** 텐서 크기가 커져 offset이 uint16_t 최대치(65535) 초과 시 랩어라운드로 데이터 덮어씀. ➔ offset을 size_t로 승급.
 13. **[메모리 초과] 음수 주파수 언더플로우 (T231 연계 보완):** 음수 주파수 유입 시 캐스팅 언더플로우로 배열 초과 참조. ➔ 하한(0.0f) 및 상한(N/2) 클램핑 동시 적용.
#### **C. 실시간성(RTOS) 및 성능 병목 제거**
 14. **[실시간성 붕괴] 대규모 배열 초기화 CPU Spike (reset, begin):** 텐서 리셋 시마다 거대한 배열을 memset하여 프레임 드롭 유발. ➔ memset 완전 삭제 및 _head = 0, _is_full = false만 처리(O(1) 속도).
 15. **[🆕 신규: 생성자 병목] 인스턴스 초기화 CPU Spike (Constructor):** 클래스 생성자 내부의 memset도 동일한 부하를 유발. ➔ 생성자 내 memset 삭제로 부팅/인스턴스 생성 딜레이 원천 차단.
 16. **[병목] 링 버퍼 조립 시 고비용 Modulo(%) 연산 (getSequenceFlat):** 루프 안에서 매번 나눗셈(% _frames) 수행으로 고속 샘플링 시 CPU 낭비. ➔ 단순 덧셈과 분기문(if (idx >= _frames) idx -= _frames;)으로 최적화.
 17. **[🆕 신규: 연산 낭비] 매 프레임 패딩 스트라이드 반복 계산 (push/get):** 매 프레임마다 정렬 크기를 계산하면 낭비. ➔ begin()에서 1회만 계산 후 멤버 변수(_stride_dim)로 캐싱하여 사용.


- 현재까지 확인된 문제 아외에 추가로 코드를 정합성, 안정성, 신뢰성 관점에서 극한의 스트레스 상황을 가정하여 다시 한번 꼼꼼하게 교차 검증 해줘
- 기존에 확인된거나 추가 확인된 이슈/개선 필요 사항들만 축소/누락없이 잘 정리해줘(구현은 한번에 하게))
   문구 간결하게

