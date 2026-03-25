G_T20_MFCC_COEFFS_DEFAULT        // 기본값
G_T20_MFCC_COEFFS_MAX    // 메모리 최대값
G_T20_FEATURE_DIM_DEFAULT  // 기본 feature dim
G_T20_FEATURE_DIM_MAX    // 최대 feature dim buffer


좋습니다.
지금 올리신 009 초안은 방향은 맞는데, 아직 “가변 길이화”가 반쯤만 적용된 상태라서 이대로는 컴파일/동작 둘 다 문제가 생깁니다.

핵심만 먼저 말하면:

MAX/DEFAULT 상수는 잘 도입했는데, 실제 코드 로직은 아직 예전의 고정 13/39 전제를 많이 끌고 있습니다.

아래처럼 정리하면 됩니다.


---

지금 초안의 핵심 문제

1) G_T20_MFCC_COEFFS, G_T20_FEATURE_DIM를 지웠는데 코드엔 아직 남아 있음

예를 들면 아래는 바로 문제입니다.

if (p_cfg->feature.mfcc_coeffs != G_T20_MFCC_COEFFS) {
    return false;
}

for (int i = 0; i < G_T20_MFCC_COEFFS; ++i)

memcpy(..., sizeof(float) * G_T20_MFCC_COEFFS);

sizeof(float) * G_T20_FEATURE_DIM

이제는 이 값들이 고정 상수가 아니라 런타임 설정값이어야 합니다.

즉 기준은 아래로 바뀌어야 합니다.

MFCC 실제 길이: p->cfg.feature.mfcc_coeffs

feature vector 실제 길이: p->cfg.feature.mfcc_coeffs * 3

sequence feature dimension: p_rb->feature_dim



---

2) DEFAULT를 실제 처리 길이로 쓰고 있음

이 부분이 가장 큰 논리 오류입니다.

예:

bool CL_T20_Mfcc::getLatestVector(float* p_out_vec, uint16_t p_len) const
{
    if (p_out_vec == nullptr || p_len < G_T20_FEATURE_DIM_DEFAULT || _impl->mutex == nullptr) {
        return false;
    }
    ...
    memcpy(p_out_vec, _impl->latest_feature.vector, sizeof(float) * G_T20_FEATURE_DIM_DEFAULT);
}

이건 가변화 설계와 안 맞습니다.

왜냐하면:

DEFAULT = 39는 기본값일 뿐

실제 vector 길이는 cfg.feature.mfcc_coeffs * 3


즉 여기서는 latest_feature.vector_len 또는 현재 cfg 기준 길이를 써야 합니다.


---

3) ST_T20_FeatureVector_t는 가변 구조로 바꿨는데, 채우는 코드가 아직 고정형

지금 구조체는 잘 바뀌었습니다.

typedef struct
{
    uint16_t log_mel_len;
    uint16_t mfcc_len;
    uint16_t delta_len;
    uint16_t delta2_len;
    uint16_t vector_len;

    float log_mel[G_T20_MEL_FILTERS];
    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];
    float vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_FeatureVector_t;

그런데 실제 ProcessTask에서는 길이 필드를 거의 안 채우고 있습니다.

즉 아래가 빠져 있습니다.

p->latest_feature.log_mel_len = p->cfg.feature.mel_filters;
p->latest_feature.mfcc_len    = p->cfg.feature.mfcc_coeffs;
p->latest_feature.delta_len   = p->cfg.feature.mfcc_coeffs;
p->latest_feature.delta2_len  = p->cfg.feature.mfcc_coeffs;
p->latest_feature.vector_len  = p->cfg.feature.mfcc_coeffs * 3;

그리고 log_mel[]도 복사해야 합니다.


---

4) seq_rb.feature_dim를 정의해놓고 실제 사용을 안 함

구조체는 잘 바뀌었습니다.

typedef struct
{
    float    data[G_T20_SEQUENCE_FRAMES_MAX][G_T20_FEATURE_DIM_MAX];
    uint16_t frames;
    uint16_t feature_dim;
    uint16_t head;
    bool     full;
} ST_T20_FeatureRingBuffer_t;

그런데 아래 함수들이 아직 예전 고정 차원 전제를 씁니다.

void T20_seqPush(...){
    memcpy(..., sizeof(float) * G_T20_FEATURE_DIM);
}

void T20_seqExportFlatten(...){
    memcpy(..., sizeof(float) * G_T20_FEATURE_DIM);
}

이건 전부:

sizeof(float) * p_rb->feature_dim

으로 바뀌어야 합니다.


---

5) T20_seqInit()가 feature_dim을 초기화하지 않음

지금은:

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames)

이렇게 되어 있는데, 가변 구조에서는 최소한 아래 둘이 필요합니다.

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim)

그리고 내부에서:

p_rb->frames = p_frames;
p_rb->feature_dim = p_feature_dim;

를 넣어야 합니다.


---

6) validateConfig()가 아직 고정 13 기준

현재:

if (p_cfg->feature.mfcc_coeffs != G_T20_MFCC_COEFFS) {
    return false;
}

이건 이제 아래처럼 바뀌어야 합니다.

if (p_cfg->feature.mfcc_coeffs == 0 ||
    p_cfg->feature.mfcc_coeffs > G_T20_MFCC_COEFFS_MAX) {
    return false;
}


---

7) mfcc_history는 MAX 크기로 잘 바꿨는데, 계산 루프는 아직 고정 길이

예:

memcpy(... sizeof(float) * G_T20_MFCC_COEFFS);
for (int c = 0; c < G_T20_MFCC_COEFFS; ++c)

이건 전부:

const uint16_t v_dim = p->cfg.feature.mfcc_coeffs;

기준으로 바뀌어야 합니다.


---

8) T20_buildVector()도 p->cfg.feature.mfcc_coeffs 기준이어야 함

현재는:

for (int i = 0; i < G_T20_MFCC_COEFFS; ++i)

이런 식인데, 이제는 아래처럼 dimension을 받아야 합니다.

void T20_buildVector(const float* p_mfcc,
                     const float* p_delta,
                     const float* p_delta2,
                     uint16_t p_dim,
                     float* p_out_vec)


---

9) T20_computeMFCC()도 아직 고정 13 기준

아래가 문제입니다.

T20_computeDCT2(p->log_mel, p_mfcc_out, G_T20_MEL_FILTERS, G_T20_MFCC_COEFFS);

이제는:

T20_computeDCT2(p->log_mel, p_mfcc_out, p->cfg.feature.mel_filters, p->cfg.feature.mfcc_coeffs);

로 바꿔야 합니다.


---

10) printLatest()도 길이 필드를 써야 함

현재는:

for (int i = 0; i < G_T20_MFCC_COEFFS; ++i)

이 아니라:

for (uint16_t i = 0; i < feat.mfcc_len; ++i)

여야 합니다.

그리고 log_mel도 출력할 거면 feat.log_mel_len 기준으로 출력해야 합니다.


---

지금 초안에서 꼭 바꿔야 할 설계 규칙

이제부터는 아래 규칙으로 통일하면 됩니다.

1. 배열 선언은 MAX

float mfcc[G_T20_MFCC_COEFFS_MAX];
float vector[G_T20_FEATURE_DIM_MAX];

2. 실제 반복은 cfg 또는 len

for (uint16_t i = 0; i < p->cfg.feature.mfcc_coeffs; ++i)

또는

for (uint16_t i = 0; i < feat.mfcc_len; ++i)

3. sequence는 feature_dim 사용

memcpy(..., sizeof(float) * p_rb->feature_dim);

4. 외부 API는 DEFAULT가 아니라 실제 길이 기준

need = latest_feature.vector_len;


---

바로 고쳐야 하는 함수들

우선순위만 뽑으면 이 8개입니다.

T20_validateConfig()

getLatestVector()

getLatestSequenceFlat()

printLatest()

T20_pushMfccHistory()

T20_computeDeltaFromHistory()

T20_computeDeltaDeltaFromHistory()

T20_buildVector()

T20_seqInit()

T20_seqPush()

T20_seqExportFlatten()

T20_updateOutput()

T20_computeMFCC()

T20_processTask()


사실상 핵심 전부입니다.


---

가장 중요한 수정 예시

1) validate

bool T20_validateConfig(const ST_T20_Config_t* p_cfg)
{
    if (p_cfg == nullptr) {
        return false;
    }

    if (p_cfg->feature.fft_size != G_T20_FFT_SIZE) {
        return false;
    }

    if (p_cfg->feature.mel_filters != G_T20_MEL_FILTERS) {
        return false;
    }

    if (p_cfg->feature.mfcc_coeffs == 0 ||
        p_cfg->feature.mfcc_coeffs > G_T20_MFCC_COEFFS_MAX) {
        return false;
    }

    if (p_cfg->feature.sample_rate_hz <= 0.0f) {
        return false;
    }

    if (p_cfg->feature.delta_window > (G_T20_MFCC_HISTORY / 2)) {
        return false;
    }

    if (p_cfg->output.sequence_frames == 0 ||
        p_cfg->output.sequence_frames > G_T20_SEQUENCE_FRAMES_MAX) {
        return false;
    }

    return true;
}


---

2) vector build

void T20_buildVector(const float* p_mfcc,
                     const float* p_delta,
                     const float* p_delta2,
                     uint16_t p_dim,
                     float* p_out_vec)
{
    uint16_t idx = 0;

    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_mfcc[i];
    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_delta[i];
    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_delta2[i];
}


---

3) seq init

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim)
{
    memset(p_rb, 0, sizeof(ST_T20_FeatureRingBuffer_t));
    p_rb->frames = p_frames;
    p_rb->feature_dim = p_feature_dim;
    p_rb->head = 0;
    p_rb->full = false;
}


---

4) seq push/export

void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec)
{
    memcpy(p_rb->data[p_rb->head], p_feature_vec, sizeof(float) * p_rb->feature_dim);

    p_rb->head++;
    if (p_rb->head >= p_rb->frames) {
        p_rb->head = 0;
        p_rb->full = true;
    }
}

void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat)
{
    uint16_t frames = p_rb->frames;
    uint16_t start = p_rb->full ? p_rb->head : 0;
    uint16_t written = 0;

    for (uint16_t i = 0; i < frames; ++i) {
        uint16_t idx = (uint16_t)((start + i) % frames);
        memcpy(&p_out_flat[written * p_rb->feature_dim],
               p_rb->data[idx],
               sizeof(float) * p_rb->feature_dim);
        written++;
    }
}


---

5) processTask에서 길이 필드 채우기

uint16_t dim = p->cfg.feature.mfcc_coeffs;
uint16_t vector_len = dim * 3;

if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    p->latest_feature.log_mel_len = p->cfg.feature.mel_filters;
    p->latest_feature.mfcc_len    = dim;
    p->latest_feature.delta_len   = dim;
    p->latest_feature.delta2_len  = dim;
    p->latest_feature.vector_len  = vector_len;

    memcpy(p->latest_feature.log_mel, p->log_mel, sizeof(float) * p->cfg.feature.mel_filters);
    memcpy(p->latest_feature.mfcc,   mfcc,   sizeof(float) * dim);
    memcpy(p->latest_feature.delta,  delta,  sizeof(float) * dim);
    memcpy(p->latest_feature.delta2, delta2, sizeof(float) * dim);

    T20_buildVector(mfcc, delta, delta2, dim, p->latest_feature.vector);
    p->latest_vector_valid = true;

    T20_updateOutput(p);

    xSemaphoreGive(p->mutex);
}


---

6) getLatestVector

bool CL_T20_Mfcc::getLatestVector(float* p_out_vec, uint16_t p_len) const
{
    if (p_out_vec == nullptr || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool ok = _impl->latest_vector_valid;
    uint16_t need = _impl->latest_feature.vector_len;

    if (!ok || p_len < need) {
        xSemaphoreGive(_impl->mutex);
        return false;
    }

    memcpy(p_out_vec, _impl->latest_feature.vector, sizeof(float) * need);
    xSemaphoreGive(_impl->mutex);
    return true;
}


---

추가로 보이는 사소하지만 중요한 것

T20_Mfcc_Inter_009.h

파일명 오타처럼 보입니다.

Inter 보다 Internal이 더 자연스럽습니다.

T20_Mfcc_Internal_009.h


---

G_T20_VERSION_STR

좋습니다. 009로 맞춰졌습니다.


---

G_T20_FEATURE_DIM_DEFAULT

이건 유지해도 되지만, 실제 처리 로직의 기준으로 쓰면 안 됩니다.
오직 default config 또는 초기 설명용으로만 쓰는 게 맞습니다.


---

결론

지금 009 초안은 방향은 정확합니다.
하지만 아직 상태는 이렇게 보는 게 맞습니다.

“자료구조는 가변형으로 바뀌었지만, 처리 로직은 아직 고정형 흔적이 많이 남아 있다.”

즉 해야 할 일은 한 줄입니다:

👉 모든 처리/복사/반복/출력의 기준을 DEFAULT가 아니라 cfg.feature.mfcc_coeffs, latest_feature.vector_len, seq_rb.feature_dim으로 바꾸기


---

원하시면 다음 답변에서 제가
지금 올리신 009 초안을 기준으로, 컴파일 가능하도록 Core_009.cpp와 Dsp_009.cpp를 수정 반영한 정리본만 딱 다시 써드리겠습니다.

