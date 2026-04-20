/* ============================================================================
 * File: T350_InfEng_001.h / .cpp 통합본
 * [유형 3 방어] memset(0)을 통한 이전 추론 쓰레기값 간섭 원천 차단
 * ========================================================================== */
#pragma once
#include "T310_Config_001.h"
#include "T320_Types_001.h"
#include <string.h>

class InferenceEngine {
public:
    InferenceOutput runHybridDecision(const SignalFeatures& ft, int lid) {
        InferenceOutput res;
        memset(_flat_features, 0, sizeof(_flat_features));

        _flat_features[0] = ft.cpsr_max[0]; _flat_features[1] = ft.cpsr_max[1];
        _flat_features[2] = ft.cpsr_max[2]; _flat_features[3] = ft.cpsr_max[3];
        _flat_features[4] = ft.cpsr_mxrms[0]; _flat_features[5] = ft.cpsr_mxrms[1];
        _flat_features[6] = ft.cpsr_mxrms[2]; _flat_features[7] = ft.cpsr_mxrms[3];
        _flat_features[8] = ft.rms;
        _flat_features[9] = ft.sptr_rms_1k_3k;
        _flat_features[10] = ft.sptr_rms_3k_6k;
        _flat_features[11] = ft.sptr_rms_6k_9k;

        for (int i = 0; i < 12; i++) {
            _flat_features[i] = (_flat_features[i] - Config::MLScaler::MEAN[i]) / Config::MLScaler::SCALE[i];
            if (IS_INVALID_FLOAT(_flat_features[i])) _flat_features[i] = 0.0f;
        }

        // TFLite 모델 추론 부분 (가중치 연동 전 더미 반환)
        res.ml_probability = 0.15f; 

        res.is_test_ng = (ft.trigger_count < Config::Thresholds::MIN_TRIGGER_COUNT);
        res.is_rule_ng = (ft.energy > Config::Thresholds::ENERGY) || (ft.wvfm_stddev > Config::Thresholds::STDDEV);

        float cutoff = 0.5f;
        for (const auto& l : Config::LID_TABLE) {
            if (l.lid == lid) { cutoff = l.ml_cutoff; break; }
        }
        res.is_ml_ng = (res.ml_probability >= cutoff);

        if (res.is_test_ng) res.final_r = DecisionResult::TEST_FAIL;
        else if (res.is_rule_ng) res.final_r = DecisionResult::RULE_NG;
        else res.final_r = res.is_ml_ng ? DecisionResult::ML_NG : DecisionResult::GOOD;

        return res;
    }
private:
    float _flat_features[12];
};
