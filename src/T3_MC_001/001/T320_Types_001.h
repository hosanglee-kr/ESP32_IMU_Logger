/* ============================================================================
 * File: T320_Types_001.h
 * Version: 1.0 (Full Version)
 * [유형 2 원칙] IEEE-754 비트 마스킹 NaN/Inf 방어 매크로 유지
 * ========================================================================== */
#pragma once
#include <stdint.h>

#define IS_INVALID_FLOAT(x) (((*(uint32_t*)&(x)) & 0x7F800000) == 0x7F800000)

enum class DecisionResult { GOOD = 0, RULE_NG = 1, ML_NG = 1, TEST_FAIL = 2 };

struct SignalFeatures {
    float cpsr_max[4];
    float cpsr_mxrms[4];
    float sptr_rms_1k_3k;
    float sptr_rms_3k_6k;
    float sptr_rms_6k_9k;
    float rms;
    float energy;
    float wvfm_stddev;
    int trigger_count;
};

struct InferenceOutput {
    DecisionResult final_r;
    float ml_probability;
    bool is_test_ng;
    bool is_rule_ng;
    bool is_ml_ng;
};

struct TriggerPayload {
    int trigger_idx;
    int test_number;
};
