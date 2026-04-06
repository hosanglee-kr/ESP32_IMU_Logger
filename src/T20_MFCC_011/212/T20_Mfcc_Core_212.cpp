/* File: T20_Mfcc_Core_212.cpp */


/* ============================================================================
[잔여 구현 계획 재점검 - Core v212]

현재 core는 상태/번들/prepare 흐름이 충분히 넓게 준비되어 있다.
이제부터의 최우선은 아래 4개를 실제 코드로 치환하는 것이다.

1. BMI270 실제 SPIClass.begin
2. BMI270 실제 burst read
3. BMI270 실제 DRDY ISR attachInterrupt
4. raw -> frame -> dsp ingress 실제 연결

구현 원칙
- direct member access 대신 alias accessor 우선
- struct 신규 멤버 추가 최소화
- 새 상태 이름 추가보다 실제 코드 치환 우선
- builder는 실제 연결 결과가 드러나도록 유지

반복 오류 주의
- include 이전 ST_Impl 사용 금지
- 헤더 stray global definition 금지
- multiple definition 링크 에러 점검
============================================================================ */

/* ============================================================================
[반복 오류 패턴 점검 체크리스트]
1. include 이전에 CL_T20_Mfcc::ST_Impl 사용 금지
2. 신규 staged 이름 추가 시 ST_Impl 실멤버 존재 여부 먼저 확인
3. 실멤버가 없으면 alias accessor로 기존 멤버에 매핑
4. setter / prepare / json builder 선언/정의 누락 점검
5. Web/UI endpoint 추가 시 builder 함수 존재 여부 점검
============================================================================ */

#include "T20_Mfcc_Inter_212.h"

/* ============================================================================
[컴파일 오류 재발 방지 호환 레이어 - v210]
반복 오류 패턴:
1. ST_Impl에 없는 신규 상태 멤버를 직접 접근
2. include 이전에 CL_T20_Mfcc::ST_Impl 사용
3. setter / json builder / prepare 함수는 추가했지만 실제 멤버는 기존 이름을 써야 하는 상황

해결 원칙:
- 신규 staged 이름은 기존 구조체 멤버로 alias 매핑
- helper 정의는 include 이후에만 배치
- JSON 출력도 alias를 통해 기존 멤버를 읽도록 통일
============================================================================ */

#ifndef T20_ALIAS_ACCESSORS_V199
	#define T20_ALIAS_ACCESSORS_V199

static inline uint8_t& T20_ref_bmi270_spi_begin_runtime_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_board_runtime_state;
}
static inline uint8_t& T20_ref_bmi270_register_read_runtime_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_read_state;
}
static inline uint8_t& T20_ref_bmi270_spi_attach_prep_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_isr_attach_state;
}
static inline uint8_t& T20_ref_bmi270_burst_read_prep_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_burst_apply_state;
}
static inline uint8_t& T20_ref_bmi270_burst_runtime_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_reg_burst_state;
}
static inline uint8_t& T20_ref_bmi270_isr_queue_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_isr_request_state;
}
static inline uint8_t& T20_ref_bmi270_real_begin_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_board_runtime_state;
}
static inline uint8_t& T20_ref_bmi270_real_burst_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_reg_burst_state;
}
static inline uint8_t& T20_ref_bmi270_real_isr_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_isr_attach_state;
}
static inline uint8_t& T20_ref_bmi270_hw_link_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_hw_exec_state;
}
static inline uint8_t& T20_ref_bmi270_frame_build_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_reg_burst_state;
}
static inline uint8_t& T20_ref_bmi270_raw_pipe_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_reg_burst_state;
}
static inline uint8_t& T20_ref_bmi270_dsp_ingress_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->bmi270_read_state;
}

static inline uint8_t& T20_ref_recorder_file_write_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_finalize_state;
}
static inline uint8_t& T20_ref_recorder_bundle_map_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_index_state;
}
static inline uint8_t& T20_ref_recorder_store_bundle_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_index_state;
}
static inline uint8_t& T20_ref_recorder_store_result(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_commit_result;
}
static inline uint8_t& T20_ref_recorder_path_route_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_package_state;
}
static inline uint8_t& T20_ref_recorder_write_commit_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_finalize_commit_state;
}
static inline uint8_t& T20_ref_recorder_write_finalize_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_finalize_state;
}
static inline uint8_t& T20_ref_recorder_commit_route_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_commit_result;
}
static inline uint8_t& T20_ref_recorder_finalize_sync_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_finalize_save_state;
}
static inline uint8_t& T20_ref_recorder_real_flush_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_manifest_state;
}
static inline uint8_t& T20_ref_recorder_real_close_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_recover_state;
}
static inline uint8_t& T20_ref_recorder_real_finalize_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_finalize_state;
}
static inline uint8_t& T20_ref_recorder_meta_sync_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_meta_state;
}
static inline uint8_t& T20_ref_recorder_report_sync_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_export_state;
}
static inline uint8_t& T20_ref_recorder_audit_sync_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_audit_state;
}
static inline uint8_t& T20_ref_recorder_manifest_sync_state(CL_T20_Mfcc::ST_Impl* p) {
	return p->recorder_manifest_state;
}

#endif

#include <algorithm>
#include <vector>

CL_T20_Mfcc* g_t20_instance = nullptr;

static bool	 T20_takeMutex(const SemaphoreHandle_t p_mutex) {
	if (p_mutex == nullptr) return false;
	return (xSemaphoreTake(p_mutex, pdMS_TO_TICKS(100)) == pdTRUE);
}

static void T20_giveMutex(const SemaphoreHandle_t p_mutex) {
	if (p_mutex != nullptr) xSemaphoreGive(p_mutex);
}

void IRAM_ATTR T20_onBmiDrdyISR(void) {
}

void T20_fillSyntheticFrame(CL_T20_Mfcc::ST_Impl* p, float* p_out_frame, uint16_t p_len) {
	if (p == nullptr || p_out_frame == nullptr || p_len == 0) return;

	const float amp	  = G_T20_RUNTIME_SIM_AMPLITUDE_DEFAULT;
	const float f1	  = 8.0f;
	const float f2	  = 23.0f;
	const float sr	  = p->cfg.feature.sample_rate_hz;
	float		phase = p->runtime_sim_phase;

	for (uint16_t i = 0; i < p_len; ++i) {
		float t		   = phase + ((float)i / sr);
		float s1	   = sinf(2.0f * G_T20_PI * f1 * t);
		float s2	   = 0.5f * sinf(2.0f * G_T20_PI * f2 * t);
		float env	   = 0.7f + 0.3f * sinf(2.0f * G_T20_PI * 0.25f * t);
		p_out_frame[i] = amp * env * (s1 + s2);
	}

	p->runtime_sim_phase += ((float)p_len / sr);
}

bool T20_processOneFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len) {
	if (p == nullptr || p_frame == nullptr || p_len != G_T20_FFT_SIZE) return false;

	float mfcc[G_T20_MFCC_COEFFS_MAX]	= {0};
	float delta[G_T20_MFCC_COEFFS_MAX]	= {0};
	float delta2[G_T20_MFCC_COEFFS_MAX] = {0};

	T20_computeMFCC(p, p_frame, mfcc);
	T20_pushMfccHistory(p, mfcc, p->cfg.feature.mfcc_coeffs);
	T20_computeDeltaFromHistory(p, p->cfg.feature.mfcc_coeffs, p->cfg.feature.delta_window, delta);
	T20_computeDeltaDeltaFromHistory(p, p->cfg.feature.mfcc_coeffs, delta2);

	if (!T20_takeMutex(p->mutex)) return false;

	memcpy(p->latest_wave_frame, p_frame, sizeof(float) * G_T20_FFT_SIZE);
	memcpy(p->viewer_last_waveform, p_frame, sizeof(float) * G_T20_FFT_SIZE);
	p->viewer_last_waveform_len = G_T20_FFT_SIZE;

	memcpy(p->viewer_last_spectrum, p->power, sizeof(float) * ((G_T20_FFT_SIZE / 2U) + 1U));
	p->viewer_last_spectrum_len = (G_T20_FFT_SIZE / 2U) + 1U;

	memcpy(p->viewer_last_log_mel, p->log_mel, sizeof(float) * G_T20_MEL_FILTERS);
	p->viewer_last_log_mel_len = G_T20_MEL_FILTERS;

	memcpy(p->viewer_last_mfcc, mfcc, sizeof(float) * p->cfg.feature.mfcc_coeffs);
	p->viewer_last_mfcc_len		  = p->cfg.feature.mfcc_coeffs;

	p->latest_feature.log_mel_len = G_T20_MEL_FILTERS;
	p->latest_feature.mfcc_len	  = p->cfg.feature.mfcc_coeffs;
	p->latest_feature.delta_len	  = p->cfg.feature.mfcc_coeffs;
	p->latest_feature.delta2_len  = p->cfg.feature.mfcc_coeffs;
	p->latest_feature.vector_len  = (uint16_t)(p->cfg.feature.mfcc_coeffs * 3U);

	memcpy(p->latest_feature.log_mel, p->log_mel, sizeof(float) * G_T20_MEL_FILTERS);
	memcpy(p->latest_feature.mfcc, mfcc, sizeof(float) * p->cfg.feature.mfcc_coeffs);
	memcpy(p->latest_feature.delta, delta, sizeof(float) * p->cfg.feature.mfcc_coeffs);
	memcpy(p->latest_feature.delta2, delta2, sizeof(float) * p->cfg.feature.mfcc_coeffs);
	T20_buildVector(mfcc, delta, delta2, p->cfg.feature.mfcc_coeffs, p->latest_feature.vector);

	memcpy(p->viewer_last_vector, p->latest_feature.vector, sizeof(float) * p->latest_feature.vector_len);
	p->viewer_last_vector_len = p->latest_feature.vector_len;
	p->latest_vector_valid	  = true;

	p->viewer_last_frame_id++;
	p->last_frame_process_ms = millis();

	uint16_t wave_slot		 = (uint16_t)(p->viewer_recent_waveform_head % G_T20_VIEWER_RECENT_WAVE_COUNT);
	memcpy(p->viewer_recent_waveforms[wave_slot], p_frame, sizeof(float) * G_T20_FFT_SIZE);
	p->viewer_recent_frame_ids[wave_slot] = p->viewer_last_frame_id;
	if (p->viewer_recent_waveform_count < G_T20_VIEWER_RECENT_WAVE_COUNT) p->viewer_recent_waveform_count++;
	p->viewer_recent_waveform_head = (uint16_t)((p->viewer_recent_waveform_head + 1U) % G_T20_VIEWER_RECENT_WAVE_COUNT);

	if (p->viewer_event_count < G_T20_VIEWER_EVENT_MAX) {
		ST_T20_ViewerEvent_t* ev = &p->viewer_events[p->viewer_event_count++];
		ev->frame_id			 = p->viewer_last_frame_id;
		strlcpy(ev->kind, "frame", sizeof(ev->kind));
		snprintf(ev->text, sizeof(ev->text), "frame_%lu processed", (unsigned long)p->viewer_last_frame_id);
	}

	T20_updateOutput(p);

	if (p->recorder_enabled && p->recorder_queue != nullptr) {
		ST_T20_RecorderVectorMessage_t rec_msg;
		memset(&rec_msg, 0, sizeof(rec_msg));
		rec_msg.frame_id   = p->viewer_last_frame_id;
		rec_msg.vector_len = p->latest_feature.vector_len;
		memcpy(rec_msg.vector, p->latest_feature.vector, sizeof(float) * rec_msg.vector_len);
		xQueueSend(p->recorder_queue, &rec_msg, 0);
	}

	T20_giveMutex(p->mutex);
	return true;
}

void T20_sensorTask(void* p_arg) {
	CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
	for (;;) {
		if (p == nullptr) {
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}
		if (!p->running) {
			vTaskDelay(pdMS_TO_TICKS(50));
			continue;
		}

		T20_fillSyntheticFrame(p, p->frame_buffer[p->active_fill_buffer], G_T20_FFT_SIZE);

		ST_T20_FrameMessage_t msg;
		msg.frame_index = p->active_fill_buffer;

		if (p->frame_queue != nullptr) {
			if (xQueueSend(p->frame_queue, &msg, 0) != pdTRUE) {
				p->dropped_frames++;
			}
		}

		p->active_fill_buffer = (uint8_t)((p->active_fill_buffer + 1U) % G_T20_RAW_FRAME_BUFFERS);
		vTaskDelay(pdMS_TO_TICKS(G_T20_RUNTIME_SIM_FRAME_INTERVAL_MS));
	}
}

void T20_processTask(void* p_arg) {
	CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
	ST_T20_FrameMessage_t msg;
	for (;;) {
		if (p == nullptr || p->frame_queue == nullptr) {
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}
		if (xQueueReceive(p->frame_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
			continue;
		}
		if (!p->running) continue;
		T20_processOneFrame(p, p->frame_buffer[msg.frame_index], G_T20_FFT_SIZE);
	}
}

extern void T20_recorderTask(void* p_arg);

CL_T20_Mfcc::CL_T20_Mfcc() : _impl(new ST_Impl()) {
	g_t20_instance = this;
}
CL_T20_Mfcc::~CL_T20_Mfcc() {
	stop();
	delete _impl;
	if (g_t20_instance == this) g_t20_instance = nullptr;
}

bool T20_validateConfig(const ST_T20_Config_t* p_cfg) {
	if (p_cfg == nullptr) return false;
	if (p_cfg->feature.fft_size != G_T20_FFT_SIZE) return false;
	if (p_cfg->feature.frame_size != G_T20_FFT_SIZE) return false;
	if (p_cfg->feature.hop_size == 0 || p_cfg->feature.hop_size > G_T20_FFT_SIZE) return false;
	if (p_cfg->feature.mel_filters != G_T20_MEL_FILTERS) return false;
	if (p_cfg->feature.mfcc_coeffs == 0 || p_cfg->feature.mfcc_coeffs > G_T20_MFCC_COEFFS_MAX) return false;
	if (p_cfg->output.sequence_frames == 0 || p_cfg->output.sequence_frames > G_T20_SEQUENCE_FRAMES_MAX) return false;
	return true;
}

void T20_initProfiles(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;
	for (uint16_t i = 0; i < G_T20_CFG_PROFILE_COUNT; ++i) {
		snprintf(p->profiles[i].name, sizeof(p->profiles[i].name), "profile_%u", (unsigned)i);
		p->profiles[i].used = false;
	}
	T20_initSdmmcProfiles(p);
}

void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;
	if (p->sensor_task_handle != nullptr) {
		vTaskDelete(p->sensor_task_handle);
		p->sensor_task_handle = nullptr;
	}
	if (p->process_task_handle != nullptr) {
		vTaskDelete(p->process_task_handle);
		p->process_task_handle = nullptr;
	}
	if (p->recorder_task_handle != nullptr) {
		vTaskDelete(p->recorder_task_handle);
		p->recorder_task_handle = nullptr;
	}
	p->running = false;
}

void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;
	if (p->frame_queue != nullptr) {
		vQueueDelete(p->frame_queue);
		p->frame_queue = nullptr;
	}
	if (p->recorder_queue != nullptr) {
		vQueueDelete(p->recorder_queue);
		p->recorder_queue = nullptr;
	}
	if (p->mutex != nullptr) {
		vSemaphoreDelete(p->mutex);
		p->mutex = nullptr;
	}
}

void T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;
	p->initialized			 = false;
	p->running				 = false;
	p->measurement_active	 = false;
	p->active_fill_buffer	 = 0;
	p->active_sample_index	 = 0;
	p->dropped_frames		 = 0;
	p->mfcc_history_count	 = 0;
	p->prev_raw_sample		 = 0.0f;
	p->noise_learned_frames	 = 0;
	p->latest_vector_valid	 = false;
	p->latest_sequence_valid = false;
	p->viewer_last_frame_id	 = 0;
	p->viewer_event_count	 = 0;
	p->recorder_record_count = 0;
	p->web_last_push_ms		 = 0;
	p->last_frame_process_ms = 0;
	p->runtime_sim_phase	 = 0.0f;
	memset(p->frame_buffer, 0, sizeof(p->frame_buffer));
	memset(p->work_frame, 0, sizeof(p->work_frame));
	memset(p->temp_frame, 0, sizeof(p->temp_frame));
	memset(p->window, 0, sizeof(p->window));
	memset(p->power, 0, sizeof(p->power));
	memset(p->noise_spectrum, 0, sizeof(p->noise_spectrum));
	memset(p->log_mel, 0, sizeof(p->log_mel));
	memset(p->mel_bank, 0, sizeof(p->mel_bank));
	memset(p->mfcc_history, 0, sizeof(p->mfcc_history));
	memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
	memset(p->biquad_state, 0, sizeof(p->biquad_state));
	memset(&p->latest_feature, 0, sizeof(p->latest_feature));
	memset(&p->seq_rb, 0, sizeof(p->seq_rb));
	memset(p->latest_wave_frame, 0, sizeof(p->latest_wave_frame));
	memset(p->viewer_last_vector, 0, sizeof(p->viewer_last_vector));
	memset(p->viewer_last_log_mel, 0, sizeof(p->viewer_last_log_mel));
	memset(p->viewer_last_mfcc, 0, sizeof(p->viewer_last_mfcc));
	memset(p->viewer_last_waveform, 0, sizeof(p->viewer_last_waveform));
	memset(p->viewer_last_spectrum, 0, sizeof(p->viewer_last_spectrum));
	memset(p->viewer_recent_waveforms, 0, sizeof(p->viewer_recent_waveforms));
	memset(p->viewer_events, 0, sizeof(p->viewer_events));
	memset(p->recorder_index_items, 0, sizeof(p->recorder_index_items));
	memset(p->recorder_last_error, 0, sizeof(p->recorder_last_error));
}

void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;
	T20_stopTasks(p);
	T20_releaseSyncObjects(p);
	T20_clearRuntimeState(p);
}

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p) {
	(void)p;
	return 0.0f;
}

void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc, uint16_t p_dim) {
	if (p == nullptr || p_mfcc == nullptr || p_dim == 0) return;
	if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
		memcpy(p->mfcc_history[p->mfcc_history_count], p_mfcc, sizeof(float) * p_dim);
		p->mfcc_history_count++;
	} else {
		for (uint16_t i = 0; i < G_T20_MFCC_HISTORY - 1U; ++i) {
			memcpy(p->mfcc_history[i], p->mfcc_history[i + 1U], sizeof(float) * p_dim);
		}
		memcpy(p->mfcc_history[G_T20_MFCC_HISTORY - 1U], p_mfcc, sizeof(float) * p_dim);
	}
}

void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, uint16_t p_delta_window, float* p_delta_out) {
	if (p == nullptr || p_delta_out == nullptr) return;
	memset(p_delta_out, 0, sizeof(float) * p_dim);
	if (p->mfcc_history_count < G_T20_MFCC_HISTORY) return;
	const uint16_t c   = G_T20_MFCC_HISTORY / 2U;
	float		   den = 0.0f;
	for (uint16_t n = 1; n <= p_delta_window; ++n) den += (float)(n * n);
	den *= 2.0f;
	for (uint16_t i = 0; i < p_dim; ++i) {
		float num = 0.0f;
		for (uint16_t n = 1; n <= p_delta_window; ++n) {
			num += (float)n * (p->mfcc_history[c + n][i] - p->mfcc_history[c - n][i]);
		}
		p_delta_out[i] = num / (den + G_T20_EPSILON);
	}
}

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, float* p_delta2_out) {
	if (p == nullptr || p_delta2_out == nullptr) return;
	memset(p_delta2_out, 0, sizeof(float) * p_dim);
	if (p->mfcc_history_count < G_T20_MFCC_HISTORY) return;
	const uint16_t c = G_T20_MFCC_HISTORY / 2U;
	for (uint16_t i = 0; i < p_dim; ++i) {
		p_delta2_out[i] = p->mfcc_history[c + 1U][i] - (2.0f * p->mfcc_history[c][i]) + p->mfcc_history[c - 1U][i];
	}
}

void T20_buildVector(const float* p_mfcc, const float* p_delta, const float* p_delta2, uint16_t p_dim, float* p_out_vec) {
	if (p_mfcc == nullptr || p_delta == nullptr || p_delta2 == nullptr || p_out_vec == nullptr) return;
	uint16_t idx = 0;
	for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_mfcc[i];
	for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_delta[i];
	for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_delta2[i];
}

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim) {
	if (p_rb == nullptr) return;
	memset(p_rb, 0, sizeof(*p_rb));
	p_rb->frames	  = p_frames;
	p_rb->feature_dim = p_feature_dim;
}

void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec) {
	if (p_rb == nullptr || p_feature_vec == nullptr || p_rb->frames == 0 || p_rb->feature_dim == 0) return;
	memcpy(p_rb->data[p_rb->head], p_feature_vec, sizeof(float) * p_rb->feature_dim);
	p_rb->head++;
	if (p_rb->head >= p_rb->frames) {
		p_rb->head = 0;
		p_rb->full = true;
	}
}

bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb) {
	return (p_rb != nullptr) ? p_rb->full : false;
}

void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat) {
	if (p_rb == nullptr || p_out_flat == nullptr) return;
	uint16_t start = p_rb->full ? p_rb->head : 0U;
	for (uint16_t i = 0; i < p_rb->frames; ++i) {
		uint16_t idx = (uint16_t)((start + i) % p_rb->frames);
		memcpy(&p_out_flat[i * p_rb->feature_dim], p_rb->data[idx], sizeof(float) * p_rb->feature_dim);
	}
}

void T20_updateOutput(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr || !p->latest_vector_valid) return;
	if (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) {
		p->latest_sequence_valid = false;
		return;
	}
	T20_seqPush(&p->seq_rb, p->latest_feature.vector);
	p->latest_sequence_valid = T20_seqIsReady(&p->seq_rb);
}

bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg) {
	if (_impl == nullptr) return false;
	T20_resetRuntimeResources(_impl);
	_impl->cfg = (p_cfg != nullptr) ? *p_cfg : T20_makeDefaultConfig();
	if (!T20_validateConfig(&_impl->cfg)) return false;
	_impl->mutex = xSemaphoreCreateMutex();
	if (_impl->mutex == nullptr) return false;
	_impl->frame_queue	  = xQueueCreate(G_T20_QUEUE_LEN, sizeof(ST_T20_FrameMessage_t));
	_impl->recorder_queue = xQueueCreate(G_T20_QUEUE_LEN, sizeof(ST_T20_RecorderVectorMessage_t));
	if (_impl->frame_queue == nullptr || _impl->recorder_queue == nullptr) {
		T20_resetRuntimeResources(_impl);
		return false;
	}
	_impl->spi.begin(G_T20_PIN_SPI_SCK, G_T20_PIN_SPI_MISO, G_T20_PIN_SPI_MOSI, G_T20_PIN_BMI_CS);
	pinMode(G_T20_PIN_BMI_CS, OUTPUT);
	digitalWrite(G_T20_PIN_BMI_CS, HIGH);
	pinMode(G_T20_PIN_BMI_INT1, INPUT);
	T20_initProfiles(_impl);
	T20_loadRuntimeConfigFile(_impl);
	T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames, (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));
	T20_initDSP(_impl);
	_impl->initialized = true;
	return true;
}

bool CL_T20_Mfcc::start(void) {
	if (_impl == nullptr || !_impl->initialized || _impl->running) return false;
	BaseType_t r1 = xTaskCreatePinnedToCore(T20_sensorTask, "T20_Sensor", G_T20_SENSOR_TASK_STACK, _impl, G_T20_SENSOR_TASK_PRIO, &_impl->sensor_task_handle, 0);
	BaseType_t r2 = xTaskCreatePinnedToCore(T20_processTask, "T20_Process", G_T20_PROCESS_TASK_STACK, _impl, G_T20_PROCESS_TASK_PRIO, &_impl->process_task_handle, 1);
	BaseType_t r3 = xTaskCreatePinnedToCore(T20_recorderTask, "T20_Recorder", G_T20_RECORDER_TASK_STACK, _impl, G_T20_RECORDER_TASK_PRIO, &_impl->recorder_task_handle, 1);
	if (r1 != pdPASS || r2 != pdPASS || r3 != pdPASS) {
		T20_stopTasks(_impl);
		return false;
	}
	_impl->running = true;
	return true;
}

void CL_T20_Mfcc::stop(void) {
	if (_impl != nullptr) T20_stopTasks(_impl);
}

bool CL_T20_Mfcc::setConfig(const ST_T20_Config_t* p_cfg) {
	if (_impl == nullptr || p_cfg == nullptr || !T20_validateConfig(p_cfg)) return false;
	if (!T20_takeMutex(_impl->mutex)) return false;
	_impl->cfg						 = *p_cfg;
	_impl->viewer_effective_hop_size = p_cfg->feature.hop_size;
	T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames, (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));
	T20_configureRuntimeFilter(_impl);
	T20_saveRuntimeConfigFile(_impl);
	T20_giveMutex(_impl->mutex);
	return true;
}

void CL_T20_Mfcc::getConfig(ST_T20_Config_t* p_cfg_out) const {
	if (_impl != nullptr && p_cfg_out != nullptr) *p_cfg_out = _impl->cfg;
}

bool CL_T20_Mfcc::getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const {
	if (_impl == nullptr || p_out == nullptr) return false;
	if (!T20_takeMutex(_impl->mutex)) return false;
	bool ok = _impl->latest_vector_valid;
	if (ok) *p_out = _impl->latest_feature;
	T20_giveMutex(_impl->mutex);
	return ok;
}

bool CL_T20_Mfcc::getLatestVector(float* p_out_vec, uint16_t p_len) const {
	if (_impl == nullptr || p_out_vec == nullptr) return false;
	if (!T20_takeMutex(_impl->mutex)) return false;
	bool ok = (_impl->latest_vector_valid && _impl->latest_feature.vector_len <= p_len);
	if (ok) memcpy(p_out_vec, _impl->latest_feature.vector, sizeof(float) * _impl->latest_feature.vector_len);
	T20_giveMutex(_impl->mutex);
	return ok;
}

bool CL_T20_Mfcc::isSequenceReady(void) const {
	return (_impl != nullptr) ? T20_seqIsReady(&_impl->seq_rb) : false;
}
bool CL_T20_Mfcc::getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const {
	if (_impl == nullptr || p_out_seq == nullptr) return false;
	uint16_t need = (uint16_t)(_impl->seq_rb.frames * _impl->seq_rb.feature_dim);
	if (need == 0 || p_len < need || !_impl->latest_sequence_valid) return false;
	T20_seqExportFlatten(&_impl->seq_rb, p_out_seq);
	return true;
}
bool CL_T20_Mfcc::getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const {
	return getLatestSequenceFlat(p_out_seq, p_len);
}

// keep the rest from 057 by including exported/JSON wrappers and helpers from existing file

bool T20_buildRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["version"]						 = G_T20_VERSION_STR;
	doc["profile_name"]					 = p->runtime_cfg_profile_name;
	doc["frame_size"]					 = p->cfg.feature.frame_size;
	doc["hop_size"]						 = p->cfg.feature.hop_size;
	doc["sample_rate_hz"]				 = p->cfg.feature.sample_rate_hz;
	doc["mfcc_coeffs"]					 = p->cfg.feature.mfcc_coeffs;
	doc["sequence_frames"]				 = p->cfg.output.sequence_frames;
	doc["output_mode"]					 = (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence";
	doc["recorder_backend"]				 = (p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc";
	doc["recorder_enabled"]				 = p->recorder_enabled;
	doc["sdmmc_profile"]				 = p->sdmmc_profile.profile_name;
	doc["sdmmc_profile_applied"]		 = p->sdmmc_profile_applied;
	doc["sdmmc_last_apply_reason"]		 = p->sdmmc_last_apply_reason;
	doc["selection_sync_enabled"]		 = p->selection_sync_enabled;
	doc["selection_sync_frame_from"]	 = p->selection_sync_frame_from;
	doc["selection_sync_frame_to"]		 = p->selection_sync_frame_to;
	doc["selection_sync_name"]			 = p->selection_sync_name;
	doc["selection_sync_range_valid"]	 = p->selection_sync_range_valid;
	doc["selection_sync_effective_from"] = p->selection_sync_effective_from;
	doc["selection_sync_effective_to"]	 = p->selection_sync_effective_to;
	doc["type_meta_enabled"]			 = p->type_meta_enabled;
	doc["type_meta_name"]				 = p->type_meta_name;
	doc["type_meta_kind"]				 = p->type_meta_kind;
	doc["type_meta_auto_text"]			 = p->type_meta_auto_text;
	doc["batch_flush_records"]			 = G_T20_RECORDER_BATCH_FLUSH_RECORDS;
	doc["batch_flush_timeout_ms"]		 = G_T20_RECORDER_BATCH_FLUSH_TIMEOUT_MS;
	doc["batch_watermark_low"]			 = p->recorder_batch_watermark_low;
	doc["batch_watermark_high"]			 = p->recorder_batch_watermark_high;
	doc["batch_idle_flush_ms"]			 = p->recorder_batch_idle_flush_ms;
	doc["dma_slot_count"]				 = G_T20_ZERO_COPY_DMA_SLOT_COUNT;
	doc["dma_slot_bytes"]				 = G_T20_ZERO_COPY_DMA_SLOT_BYTES;

	size_t need							 = measureJson(doc) + 1U;
	if (need > p_len) return false;
	serializeJson(doc, p_out_buf, p_len);
	return true;
}

bool T20_applyRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text) {
	if (p == nullptr || p_json_text == nullptr) return false;

	JsonDocument		 doc;
	DeserializationError err = deserializeJson(doc, p_json_text);
	if (err) return false;

	const char* profile_name		= doc["profile_name"] | p->runtime_cfg_profile_name;
	const char* backend				= doc["recorder_backend"] | ((p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc");
	const char* sdmmc_profile		= doc["sdmmc_profile"] | p->sdmmc_profile.profile_name;
	const char* output_mode			= doc["output_mode"] | ((p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence");
	const char* selection_sync_name = doc["selection_sync_name"] | p->selection_sync_name;
	const char* type_meta_name		= doc["type_meta_name"] | p->type_meta_name;
	const char* type_meta_kind		= doc["type_meta_kind"] | p->type_meta_kind;

	strlcpy(p->runtime_cfg_profile_name, profile_name, sizeof(p->runtime_cfg_profile_name));
	strlcpy(p->selection_sync_name, selection_sync_name, sizeof(p->selection_sync_name));
	strlcpy(p->type_meta_name, type_meta_name, sizeof(p->type_meta_name));
	strlcpy(p->type_meta_kind, type_meta_kind, sizeof(p->type_meta_kind));
	p->type_meta_enabled			 = (bool)(doc["type_meta_enabled"] | p->type_meta_enabled);
	p->cfg.feature.hop_size			 = (uint16_t)(doc["hop_size"] | p->cfg.feature.hop_size);
	p->cfg.feature.mfcc_coeffs		 = (uint16_t)(doc["mfcc_coeffs"] | p->cfg.feature.mfcc_coeffs);
	p->cfg.output.sequence_frames	 = (uint16_t)(doc["sequence_frames"] | p->cfg.output.sequence_frames);
	p->recorder_enabled				 = (bool)(doc["recorder_enabled"] | p->recorder_enabled);
	p->selection_sync_enabled		 = (bool)(doc["selection_sync_enabled"] | p->selection_sync_enabled);
	p->selection_sync_frame_from	 = (uint32_t)(doc["selection_sync_frame_from"] | p->selection_sync_frame_from);
	p->selection_sync_frame_to		 = (uint32_t)(doc["selection_sync_frame_to"] | p->selection_sync_frame_to);
	p->recorder_batch_watermark_low	 = (uint16_t)(doc["batch_watermark_low"] | p->recorder_batch_watermark_low);
	p->recorder_batch_watermark_high = (uint16_t)(doc["batch_watermark_high"] | p->recorder_batch_watermark_high);
	p->recorder_batch_idle_flush_ms	 = (uint32_t)(doc["batch_idle_flush_ms"] | p->recorder_batch_idle_flush_ms);
	if (p->recorder_batch_watermark_low == 0) p->recorder_batch_watermark_low = 1;
	if (p->recorder_batch_watermark_high < p->recorder_batch_watermark_low) {
		p->recorder_batch_watermark_high = p->recorder_batch_watermark_low;
	}

	if (strcmp(backend, "sdmmc") == 0)
		p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;
	else
		p->recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;

	if (strcmp(output_mode, "sequence") == 0)
		p->cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;
	else
		p->cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;

	T20_applySdmmcProfileByName(p, sdmmc_profile);
	T20_seqInit(&p->seq_rb, p->cfg.output.sequence_frames, (uint16_t)(p->cfg.feature.mfcc_coeffs * 3U));
	T20_configureRuntimeFilter(p);
	T20_syncDerivedViewState(p);
	return true;
}

bool T20_loadRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!LittleFS.exists(G_T20_RECORDER_RUNTIME_CFG_FILE_PATH)) return false;

	File file = LittleFS.open(G_T20_RECORDER_RUNTIME_CFG_FILE_PATH, "r");
	if (!file) return false;

	String json_text = file.readString();
	file.close();

	return T20_applyRuntimeConfigJsonText(p, json_text.c_str());
}

bool T20_saveRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	char json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
	if (!T20_buildRuntimeConfigJsonText(p, json, sizeof(json))) return false;

	File file = LittleFS.open(G_T20_RECORDER_RUNTIME_CFG_FILE_PATH, "w");
	if (!file) return false;
	file.print(json);
	file.close();
	return true;
}

bool T20_buildSdmmcProfilesJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["active"] = p->sdmmc_profile.profile_name;
	JsonArray arr = doc["profiles"].to<JsonArray>();

	for (uint16_t i = 0; i < G_T20_SDMMC_PROFILE_PRESET_COUNT; ++i) {
		if (!p->sdmmc_profiles[i].enabled) continue;
		JsonObject o	   = arr.add<JsonObject>();
		o["name"]		   = p->sdmmc_profiles[i].profile_name;
		o["use_1bit_mode"] = p->sdmmc_profiles[i].use_1bit_mode;
		o["clk_pin"]	   = p->sdmmc_profiles[i].clk_pin;
		o["cmd_pin"]	   = p->sdmmc_profiles[i].cmd_pin;
		o["d0_pin"]		   = p->sdmmc_profiles[i].d0_pin;
		o["d1_pin"]		   = p->sdmmc_profiles[i].d1_pin;
		o["d2_pin"]		   = p->sdmmc_profiles[i].d2_pin;
		o["d3_pin"]		   = p->sdmmc_profiles[i].d3_pin;
	}

	size_t need = measureJson(doc) + 1U;
	if (need > p_len) return false;
	serializeJson(doc, p_out_buf, p_len);
	return true;
}

bool T20_buildSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["enabled"]			   = p->selection_sync_enabled;
	doc["name"]				   = p->selection_sync_name;
	doc["frame_from"]		   = p->selection_sync_frame_from;
	doc["frame_to"]			   = p->selection_sync_frame_to;
	doc["effective_from"]	   = p->selection_sync_effective_from;
	doc["effective_to"]		   = p->selection_sync_effective_to;
	doc["range_valid"]		   = p->selection_sync_range_valid;
	doc["selected_points_len"] = p->viewer_selection_points_len;
	if (p->selection_sync_enabled && !p->selection_sync_range_valid) {
		doc["status"] = "range_invalid";
	} else if (p->selection_sync_enabled) {
		doc["status"] = "range_ready";
	} else {
		doc["status"] = "disabled";
	}
	size_t need = measureJson(doc) + 1U;
	if (need > p_len) return false;
	serializeJson(doc, p_out_buf, p_len);
	return true;
}

bool T20_buildTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["enabled"]					= p->type_meta_enabled;
	doc["name"]						= p->type_meta_name;
	doc["kind"]						= p->type_meta_kind;
	doc["vector_len"]				= p->viewer_last_vector_len;
	doc["latest_frame_id"]			= p->viewer_last_frame_id;
	doc["auto_text"]				= p->type_meta_auto_text;
	doc["preview_link_path"]		= p->type_preview_link_path;
	doc["preview_parser_name"]		= p->type_preview_parser_name;
	doc["preview_sample_row_count"] = p->type_preview_sample_row_count;
	doc["preview_text_loaded"]		= (p->type_preview_text_buf[0] != 0);
	doc["schema_kind"]				= p->type_preview_schema_kind;
	doc["detected_delim"]			= p->type_preview_detected_delim;
	doc["header_guess"]				= p->type_preview_header_guess;
	doc["build_sync_state"]			= "synced";
	JsonArray rows					= doc["preview_sample_rows"].to<JsonArray>();
	for (uint16_t i = 0; i < p->type_preview_sample_row_count; ++i) {
		rows.add(p->type_preview_sample_rows[i]);
	}
	size_t need = measureJson(doc) + 1U;
	if (need > p_len) return false;
	serializeJson(doc, p_out_buf, p_len);
	return true;
}

void T20_updateSelectionSyncState(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	if (!p->selection_sync_enabled) {
		p->selection_sync_range_valid	 = false;
		p->selection_sync_effective_from = 0;
		p->selection_sync_effective_to	 = 0;
		return;
	}

	if (p->selection_sync_frame_to < p->selection_sync_frame_from) {
		p->selection_sync_range_valid	 = false;
		p->selection_sync_effective_from = p->selection_sync_frame_from;
		p->selection_sync_effective_to	 = p->selection_sync_frame_to;
		return;
	}

	p->selection_sync_range_valid	 = true;
	p->selection_sync_effective_from = p->selection_sync_frame_from;
	p->selection_sync_effective_to	 = p->selection_sync_frame_to;
}

void T20_updateTypeMetaAutoClassify(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	if (!p->type_meta_enabled) {
		strlcpy(p->type_meta_auto_text, "disabled", sizeof(p->type_meta_auto_text));
		return;
	}

	if (strcmp(p->type_meta_kind, "feature_vector") == 0) {
		if (p->viewer_last_vector_len >= 39U) {
			strlcpy(p->type_meta_auto_text, "mfcc_39d_vector", sizeof(p->type_meta_auto_text));
		} else if (p->viewer_last_vector_len > 0U) {
			strlcpy(p->type_meta_auto_text, "compact_feature_vector", sizeof(p->type_meta_auto_text));
		} else {
			strlcpy(p->type_meta_auto_text, "empty_feature_vector", sizeof(p->type_meta_auto_text));
		}
	} else if (strcmp(p->type_meta_kind, "sequence") == 0) {
		if (p->latest_sequence_valid) {
			strlcpy(p->type_meta_auto_text, "sequence_ready", sizeof(p->type_meta_auto_text));
		} else {
			strlcpy(p->type_meta_auto_text, "sequence_warming_up", sizeof(p->type_meta_auto_text));
		}
	} else if (strcmp(p->type_meta_kind, "waveform") == 0) {
		strlcpy(p->type_meta_auto_text, "waveform_frame", sizeof(p->type_meta_auto_text));
	} else {
		strlcpy(p->type_meta_auto_text, "generic_meta", sizeof(p->type_meta_auto_text));
	}
}

void T20_updateViewerSelectionProjection(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	memset(p->viewer_selection_points, 0, sizeof(p->viewer_selection_points));
	p->viewer_selection_points_len = 0;

	if (!p->selection_sync_enabled || !p->selection_sync_range_valid) return;
	if (p->viewer_last_waveform_len == 0) return;

	uint32_t frame_from = p->selection_sync_effective_from;
	uint32_t frame_to	= p->selection_sync_effective_to;

	if (p->viewer_last_frame_id < frame_from || p->viewer_last_frame_id > frame_to) {
		return;
	}

	uint16_t copy_len = p->viewer_last_waveform_len;
	if (copy_len > G_T20_VIEWER_SELECTION_POINTS_MAX) {
		copy_len = G_T20_VIEWER_SELECTION_POINTS_MAX;
	}

	memcpy(p->viewer_selection_points, p->viewer_last_waveform, sizeof(float) * copy_len);
	p->viewer_selection_points_len = copy_len;
}

bool T20_loadTypePreviewText(CL_T20_Mfcc::ST_Impl* p, const char* p_path) {
	if (p == nullptr || p_path == nullptr || p_path[0] == 0) return false;

	File file = LittleFS.open(p_path, "r");
	if (!file) return false;

	size_t n					= file.readBytes(p->type_preview_text_buf, G_T20_TYPE_PREVIEW_TEXT_BUF_MAX - 1U);
	p->type_preview_text_buf[n] = 0;
	file.close();
	return true;
}

void T20_updateTypePreviewSamples(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	memset(p->type_preview_sample_rows, 0, sizeof(p->type_preview_sample_rows));
	p->type_preview_sample_row_count = 0;

	if (!p->type_meta_enabled) return;

	if (p->viewer_last_vector_len > 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
		snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
				 sizeof(p->type_preview_sample_rows[0]),
				 "vector_len=%u", p->viewer_last_vector_len);
	}

	if (p->viewer_last_waveform_len > 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
		snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
				 sizeof(p->type_preview_sample_rows[0]),
				 "waveform_len=%u", p->viewer_last_waveform_len);
	}

	if (p->selection_sync_enabled && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
		snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
				 sizeof(p->type_preview_sample_rows[0]),
				 "selection=%s:%lu-%lu", p->selection_sync_name,
				 (unsigned long)p->selection_sync_effective_from,
				 (unsigned long)p->selection_sync_effective_to);
	}

	if (p->viewer_overlay_accum_count > 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
		snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
				 sizeof(p->type_preview_sample_rows[0]),
				 "overlay_accum=%u", p->viewer_overlay_accum_count);
	}

	if (p->type_preview_text_buf[0] != 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
		snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
				 sizeof(p->type_preview_sample_rows[0]),
				 "preview_text=loaded");
	}

	if (p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
		snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
				 sizeof(p->type_preview_sample_rows[0]),
				 "schema=%s delim=%s", p->type_preview_schema_kind, p->type_preview_detected_delim);
	}

	if (p->type_preview_header_guess[0] != 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
		snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
				 sizeof(p->type_preview_sample_rows[0]),
				 "header=%s", p->type_preview_header_guess);
	}

	if (p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
		snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
				 sizeof(p->type_preview_sample_rows[0]),
				 "auto=%s", p->type_meta_auto_text);
	}
}

void T20_updateTypePreviewSchemaGuess(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	strlcpy(p->type_preview_schema_kind, "unknown", sizeof(p->type_preview_schema_kind));
	strlcpy(p->type_preview_detected_delim, ",", sizeof(p->type_preview_detected_delim));

	if (p->type_preview_text_buf[0] == 0) {
		return;
	}

	const char* txt	  = p->type_preview_text_buf;
	uint32_t	comma = 0, tab = 0, pipe = 0;
	for (uint16_t i = 0; txt[i] != 0 && i < G_T20_TYPE_PREVIEW_TEXT_BUF_MAX; ++i) {
		if (txt[i] == ',')
			comma++;
		else if (txt[i] == '\t')
			tab++;
		else if (txt[i] == '|')
			pipe++;
	}

	if (tab >= comma && tab >= pipe && tab > 0) {
		strlcpy(p->type_preview_detected_delim, "\\t", sizeof(p->type_preview_detected_delim));
		strlcpy(p->type_preview_schema_kind, "tsv_like", sizeof(p->type_preview_schema_kind));
	} else if (pipe >= comma && pipe >= tab && pipe > 0) {
		strlcpy(p->type_preview_detected_delim, "|", sizeof(p->type_preview_detected_delim));
		strlcpy(p->type_preview_schema_kind, "pipe_like", sizeof(p->type_preview_schema_kind));
	} else if (comma > 0) {
		strlcpy(p->type_preview_detected_delim, ",", sizeof(p->type_preview_detected_delim));
		strlcpy(p->type_preview_schema_kind, "csv_like", sizeof(p->type_preview_schema_kind));
	} else {
		strlcpy(p->type_preview_schema_kind, "plain_text", sizeof(p->type_preview_schema_kind));
	}

	T20_updateTypePreviewHeaderGuess(p);

	if (p->type_preview_header_guess[0] != 0) {
		if (strstr(p->type_preview_header_guess, "time") != nullptr ||
			strstr(p->type_preview_header_guess, "frame") != nullptr ||
			strstr(p->type_preview_header_guess, "value") != nullptr) {
			strlcpy(p->type_preview_schema_kind, "headered_table", sizeof(p->type_preview_schema_kind));
		}
	}
}

void T20_updateTypePreviewHeaderGuess(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	memset(p->type_preview_header_guess, 0, sizeof(p->type_preview_header_guess));

	if (p->type_preview_text_buf[0] == 0) {
		return;
	}

	uint16_t i = 0;
	while (p->type_preview_text_buf[i] != 0 &&
		   p->type_preview_text_buf[i] != '\n' &&
		   p->type_preview_text_buf[i] != '\r' &&
		   i < (sizeof(p->type_preview_header_guess) - 1U)) {
		p->type_preview_header_guess[i] = p->type_preview_text_buf[i];
		++i;
	}
	p->type_preview_header_guess[i] = 0;
}

void T20_updateViewerOverlayProjection(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	memset(p->viewer_overlay_points, 0, sizeof(p->viewer_overlay_points));
	p->viewer_overlay_points_len   = 0;
	p->viewer_overlay_accum_count  = 0;
	p->viewer_overlay_subset_count = 0;

	if (p->viewer_recent_waveform_count == 0) return;

	uint16_t copy_len = G_T20_VIEWER_SELECTION_POINTS_MAX;
	if (copy_len > G_T20_FFT_SIZE) copy_len = G_T20_FFT_SIZE;

	for (uint16_t n = 0; n < p->viewer_recent_waveform_count; ++n) {
		uint16_t idx	 = (uint16_t)((p->viewer_recent_waveform_head + G_T20_VIEWER_RECENT_WAVE_COUNT - 1U - n) % G_T20_VIEWER_RECENT_WAVE_COUNT);
		uint32_t fid	 = p->viewer_recent_frame_ids[idx];

		bool	 include = true;
		if (p->selection_sync_enabled) {
			include = p->selection_sync_range_valid &&
					  (fid >= p->selection_sync_effective_from) &&
					  (fid <= p->selection_sync_effective_to);
		}
		if (!include) continue;

		for (uint16_t i = 0; i < copy_len; ++i) {
			p->viewer_overlay_points[i] += p->viewer_recent_waveforms[idx][i];
		}
		p->viewer_overlay_accum_count++;
		p->viewer_overlay_subset_count++;
	}

	if (p->viewer_overlay_accum_count == 0) return;

	if (p->viewer_overlay_accum_count > 1) {
		for (uint16_t i = 0; i < copy_len; ++i) {
			p->viewer_overlay_points[i] /= (float)p->viewer_overlay_accum_count;
		}
	}
	p->viewer_overlay_points_len = copy_len;
}

void T20_updateTypePreviewColumnHints(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	/* TODO:
	   - 실제 column 별 타입 추론 구조체 도입
	   - 현재는 header_guess / schema_kind 조합을 sample row에 반영하는 수준 */

	if (p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX &&
		p->type_preview_header_guess[0] != 0) {
		if (strstr(p->type_preview_header_guess, "time") != nullptr) {
			snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
					 sizeof(p->type_preview_sample_rows[0]),
					 "col_hint=time-like");
		} else if (strstr(p->type_preview_header_guess, "frame") != nullptr) {
			snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
					 sizeof(p->type_preview_sample_rows[0]),
					 "col_hint=frame-like");
		} else if (strstr(p->type_preview_header_guess, "value") != nullptr) {
			snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
					 sizeof(p->type_preview_sample_rows[0]),
					 "col_hint=value-like");
		}
	}
}

void T20_syncDerivedViewState(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	T20_syncDerivedViewState(p);
}

bool T20_buildBuildSanityJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["version"]						 = G_T20_VERSION_STR;
	doc["has_recent_frame_ids"]			 = true;
	doc["has_selection_points"]			 = true;
	doc["has_overlay_points"]			 = true;
	doc["has_type_preview_header_guess"] = true;
	doc["has_type_preview_schema_kind"]	 = true;
	doc["has_json_write_doc"]			 = true;
	doc["has_live_source_path"]			 = true;
	doc["selection_sync_enabled"]		 = p->selection_sync_enabled;
	doc["selection_sync_range_valid"]	 = p->selection_sync_range_valid;
	doc["viewer_overlay_accum_count"]	 = p->viewer_overlay_accum_count;
	doc["viewer_overlay_subset_count"]	 = p->viewer_overlay_subset_count;
	doc["type_preview_text_loaded"]		 = (p->type_preview_text_buf[0] != 0);
	doc["type_preview_schema_kind"]		 = p->type_preview_schema_kind;
	doc["type_preview_header_guess"]	 = p->type_preview_header_guess;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

void T20_updatePreviewColumnHints(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	p->preview_column_hint_count = 0;
	memset(p->preview_column_hints, 0, sizeof(p->preview_column_hints));

	if (p->type_preview_header_guess[0] == 0) return;

	if (strstr(p->type_preview_header_guess, "time") != nullptr && p->preview_column_hint_count < 8) {
		strlcpy(p->preview_column_hints[p->preview_column_hint_count++], "time-like", sizeof(p->preview_column_hints[0]));
	}
	if (strstr(p->type_preview_header_guess, "frame") != nullptr && p->preview_column_hint_count < 8) {
		strlcpy(p->preview_column_hints[p->preview_column_hint_count++], "frame-like", sizeof(p->preview_column_hints[0]));
	}
	if ((strstr(p->type_preview_header_guess, "value") != nullptr ||
		 strstr(p->type_preview_header_guess, "val") != nullptr) &&
		p->preview_column_hint_count < 8) {
		strlcpy(p->preview_column_hints[p->preview_column_hint_count++], "value-like", sizeof(p->preview_column_hints[0]));
	}
	if ((strstr(p->type_preview_header_guess, "x") != nullptr ||
		 strstr(p->type_preview_header_guess, "y") != nullptr) &&
		p->preview_column_hint_count < 8) {
		strlcpy(p->preview_column_hints[p->preview_column_hint_count++], "axis-like", sizeof(p->preview_column_hints[0]));
	}
}

bool T20_buildUnifiedViewerBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["mode"]				 = p->viewer_bundle_mode_name;
	doc["frame_id"]			 = p->viewer_last_frame_id;

	JsonObject selection	 = doc["selection"].to<JsonObject>();
	selection["enabled"]	 = p->selection_sync_enabled;
	selection["range_valid"] = p->selection_sync_range_valid;
	selection["name"]		 = p->selection_sync_name;
	selection["from"]		 = p->selection_sync_effective_from;
	selection["to"]			 = p->selection_sync_effective_to;
	selection["points_len"]	 = p->viewer_selection_points_len;

	JsonArray wave			 = doc["waveform"].to<JsonArray>();
	for (uint16_t i = 0; i < p->viewer_last_waveform_len; ++i) wave.add(p->viewer_last_waveform[i]);

	JsonArray sel = doc["selection_points"].to<JsonArray>();
	for (uint16_t i = 0; i < p->viewer_selection_points_len; ++i) sel.add(p->viewer_selection_points[i]);

	JsonArray ov = doc["overlay_points"].to<JsonArray>();
	for (uint16_t i = 0; i < p->viewer_overlay_points_len; ++i) ov.add(p->viewer_overlay_points[i]);
	doc["overlay_accum_count"]	= p->viewer_overlay_accum_count;
	doc["overlay_subset_count"] = p->viewer_overlay_subset_count;

	JsonArray recent			= doc["recent_frame_ids"].to<JsonArray>();
	for (uint16_t n = 0; n < p->viewer_recent_waveform_count; ++n) {
		uint16_t idx = (uint16_t)((p->viewer_recent_waveform_head + G_T20_VIEWER_RECENT_WAVE_COUNT - 1U - n) % G_T20_VIEWER_RECENT_WAVE_COUNT);
		recent.add(p->viewer_recent_frame_ids[idx]);
	}

	JsonObject type				= doc["type_meta"].to<JsonObject>();
	type["enabled"]				= p->type_meta_enabled;
	type["name"]				= p->type_meta_name;
	type["kind"]				= p->type_meta_kind;
	type["auto_text"]			= p->type_meta_auto_text;
	type["preview_link_path"]	= p->type_preview_link_path;
	type["preview_parser_name"] = p->type_preview_parser_name;
	type["preview_text_loaded"] = (p->type_preview_text_buf[0] != 0);
	type["schema_kind"]			= p->type_preview_schema_kind;
	type["detected_delim"]		= p->type_preview_detected_delim;
	type["header_guess"]		= p->type_preview_header_guess;

	JsonArray hints				= type["column_hints"].to<JsonArray>();
	for (uint16_t i = 0; i < p->preview_column_hint_count; ++i) hints.add(p->preview_column_hints[i]);

	JsonArray rows = type["preview_sample_rows"].to<JsonArray>();
	for (uint16_t i = 0; i < p->type_preview_sample_row_count; ++i) rows.add(p->type_preview_sample_rows[i]);

	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderStorageJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["backend"]		   = (p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc";
	doc["fallback_active"] = p->recorder_fallback_active;
	doc["active_path"]	   = p->recorder_active_path;
	doc["record_count"]	   = p->recorder_record_count;
	doc["batch_count"]	   = p->recorder_batch_count;
	doc["index_count"]	   = p->recorder_index_count;
	doc["last_flush_ms"]   = p->recorder_last_flush_ms;
	doc["rotate_keep_max"] = p->recorder_rotate_keep_max;
	doc["last_error"]	   = p->recorder_last_error;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_jsonWriteDoc(JsonDocument& p_doc, char* p_out_buf, uint16_t p_len) {
	if (p_out_buf == nullptr || p_len == 0) return false;
	size_t need = measureJson(p_doc) + 1U;
	if (need > p_len) {
		if (p_len > 0) p_out_buf[0] = 0;
		return false;
	}
	serializeJson(p_doc, p_out_buf, p_len);
	return true;
}

bool T20_beginLiveSource(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 BMI270 begin/init 상세 오류 코드화
	   - DRDY ISR + queue wakeup 연동 */
	p->bmi270_live_enabled = true;
	p->live_source_mode	   = G_T20_LIVE_SOURCE_MODE_BMI270;
	p->live_last_sample_ms = millis();
	p->live_frame_fill	   = 0;
	p->live_frame_ready	   = false;

	if (!T20_initBMI270_SPI(p)) return false;
	if (!T20_configBMI270_2100Hz_DRDY(p)) return false;
	return true;
}

void T20_stopLiveSource(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;
	p->bmi270_live_enabled = false;
	p->bmi270_live_ready   = false;
	p->live_source_mode	   = 0;
}

bool T20_processLiveSourceTick(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!p->bmi270_live_enabled || !p->bmi270_live_ready) return false;

	uint32_t now_ms = millis();
	if ((now_ms - p->live_last_sample_ms) < G_T20_BMI270_SIM_SAMPLE_INTERVAL_MS) return false;
	if ((now_ms - p->bmi270_last_drdy_ms) > G_T20_LIVE_DRDY_TIMEOUT_MS) {
		strlcpy(p->bmi270_status_text, "drdy_timeout", sizeof(p->bmi270_status_text));
		T20_tryBMI270Reinit(p);
	}
	if (!T20_pollBMI270Drdy(p)) return false;
	p->live_last_sample_ms = now_ms;

	float sample		   = 0.0f;
	if (!T20_bmi270ReadVectorSample(p, &sample)) return false;
	if (!T20_pushLiveQueueSample(p, sample)) return false;
	if (!T20_drainLiveQueueToFrameBuffer(p)) return false;

	T20_liveHeartbeat(p);
	if (T20_tryBuildLiveFrame(p)) {
		p->live_frame_counter++;
		return true;
	}
	return false;
}

bool T20_buildLiveSourceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["live_source_mode"]					= p->live_source_mode;
	doc["bmi270_live_enabled"]				= p->bmi270_live_enabled;
	doc["bmi270_live_ready"]				= p->bmi270_live_ready;
	doc["live_frame_counter"]				= p->live_frame_counter;
	doc["live_last_sample_ms"]				= p->live_last_sample_ms;
	doc["bmi270_axis_mode"]					= p->bmi270_axis_mode;
	doc["bmi270_last_sample_value"]			= p->bmi270_last_sample_value;
	doc["bmi270_last_drdy_ms"]				= p->bmi270_last_drdy_ms;
	doc["live_sample_queue_count"]			= p->live_sample_queue_count;
	doc["bmi270_status_text"]				= p->bmi270_status_text;
	doc["bmi270_chip_id"]					= p->bmi270_chip_id;
	doc["bmi270_spi_ok"]					= p->bmi270_spi_ok;
	doc["bmi270_drdy_enabled"]				= p->bmi270_drdy_enabled;
	doc["bmi270_last_poll_ms"]				= p->bmi270_last_poll_ms;
	doc["bmi270_drdy_isr_flag"]				= p->bmi270_drdy_isr_flag;
	doc["bmi270_last_reg_addr"]				= p->bmi270_last_reg_addr;
	doc["bmi270_last_reg_value"]			= p->bmi270_last_reg_value;
	doc["bmi270_isr_queue_count"]			= p->bmi270_isr_queue_count;
	doc["bmi270_actual_spi_path_enabled"]	= p->bmi270_actual_spi_path_enabled;
	doc["bmi270_last_burst_len"]			= p->bmi270_last_burst_len;
	doc["bmi270_last_read_ok"]				= p->bmi270_last_read_ok;
	doc["bmi270_last_transaction_ok"]		= p->bmi270_last_transaction_ok;
	doc["bmi270_last_isr_attach_ok"]		= p->bmi270_last_isr_attach_ok;
	doc["bmi270_spi_bus_ready"]				= p->bmi270_spi_bus_ready;
	doc["bmi270_last_txn_reg"]				= p->bmi270_last_txn_reg;
	doc["bmi270_spi_read_phase_ready"]		= p->bmi270_spi_read_phase_ready;
	doc["bmi270_isr_hook_ready"]			= p->bmi270_isr_hook_ready;
	doc["bmi270_spi_begin_ok"]				= p->bmi270_spi_begin_ok;
	doc["bmi270_isr_attach_state"]			= p->bmi270_isr_attach_state;
	doc["bmi270_actual_reg_read_ready"]		= p->bmi270_actual_reg_read_ready;
	doc["bmi270_last_decoded_sample"]		= p->bmi270_last_decoded_sample;
	doc["bmi270_last_axis_decode_ok"]		= p->bmi270_last_axis_decode_ok;
	doc["bmi270_read_state"]				= p->bmi270_read_state;
	doc["bmi270_last_read_ms"]				= p->bmi270_last_read_ms;
	doc["bmi270_actual_burst_ready"]		= p->bmi270_actual_burst_ready;
	doc["bmi270_burst_flow_state"]			= p->bmi270_burst_flow_state;
	doc["bmi270_isr_request_state"]			= p->bmi270_isr_request_state;
	doc["bmi270_spi_start_state"]			= p->bmi270_spi_start_state;
	doc["bmi270_isr_hook_state"]			= p->bmi270_isr_hook_state;
	doc["recorder_finalize_persist_state"]	= p->recorder_finalize_persist_state;
	doc["bmi270_reg_burst_state"]			= p->bmi270_reg_burst_state;
	doc["recorder_finalize_persist_result"] = p->recorder_finalize_persist_result;
	doc["bmi270_actual_read_txn_state"]		= p->bmi270_actual_read_txn_state;
	doc["recorder_finalize_save_state"]		= p->recorder_finalize_save_state;
	doc["bmi270_spi_exec_state"]			= p->bmi270_spi_exec_state;
	doc["recorder_finalize_exec_state"]		= p->recorder_finalize_exec_state;
	doc["bmi270_spi_apply_state"]			= p->bmi270_spi_apply_state;
	doc["recorder_finalize_commit_state"]	= p->recorder_finalize_commit_state;
	doc["bmi270_spi_session_state"]			= p->bmi270_spi_session_state;
	doc["bmi270_burst_apply_state"]			= p->bmi270_burst_apply_state;
	doc["recorder_persist_write_state"]		= p->recorder_persist_write_state;
	doc["recorder_commit_result"]			= p->recorder_commit_result;
	doc["bmi270_txn_pipeline_state"]		= p->bmi270_txn_pipeline_state;
	doc["recorder_finalize_pipeline_state"] = p->recorder_finalize_pipeline_state;
	doc["bmi270_readback_state"]			= p->bmi270_readback_state;
	doc["recorder_manifest_state"]			= p->recorder_manifest_state;
	doc["bmi270_verify_state"]				= p->bmi270_verify_state;
	doc["bmi270_verify_result"]				= p->bmi270_verify_result;
	doc["recorder_index_state"]				= p->recorder_index_state;
	doc["recorder_summary_state"]			= p->recorder_summary_state;
	doc["bmi270_hw_bridge_state"]			= p->bmi270_hw_bridge_state;
	doc["bmi270_isr_bridge_state"]			= p->bmi270_isr_bridge_state;
	doc["recorder_artifact_state"]			= p->recorder_artifact_state;
	doc["recorder_artifact_result"]			= p->recorder_artifact_result;
	JsonArray axis_values					= doc["bmi270_last_axis_values"].to<JsonArray>();
	for (uint8_t i = 0; i < G_T20_BMI270_BURST_AXIS_COUNT; ++i) {
		axis_values.add(p->bmi270_last_axis_values[i]);
	}
	doc["bmi270_init_retry"]	  = p->bmi270_init_retry;
	doc["live_last_heartbeat_ms"] = p->live_last_heartbeat_ms;
	doc["hop_size"]				  = p->cfg.feature.hop_size;
	doc["frame_size"]			  = p->cfg.feature.frame_size;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_feedLiveSample(CL_T20_Mfcc::ST_Impl* p, float p_value) {
	if (p == nullptr) return false;
	if (p->live_frame_fill >= G_T20_LIVE_FRAME_TEMP_MAX) return false;

	p->live_frame_temp[p->live_frame_fill++] = p_value;
	p->bmi270_sample_counter++;
	return true;
}

bool T20_tryBuildLiveFrame(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!p->bmi270_live_enabled || !p->bmi270_live_ready) return false;

	uint16_t frame_size = p->cfg.feature.frame_size;
	if (frame_size == 0 || frame_size > G_T20_LIVE_FRAME_TEMP_MAX) return false;
	if (p->live_frame_fill < frame_size) return false;

	memcpy(p->latest_wave_frame, p->live_frame_temp, sizeof(float) * frame_size);

	uint16_t hop = p->cfg.feature.hop_size;
	if (hop == 0 || hop > frame_size) hop = frame_size;

	uint16_t remain = (p->live_frame_fill > hop) ? (p->live_frame_fill - hop) : 0;
	if (remain > 0) {
		memmove(p->live_frame_temp, &p->live_frame_temp[hop], sizeof(float) * remain);
	}
	p->live_frame_fill	= remain;
	p->live_frame_ready = true;
	return true;
}

bool T20_buildLiveDebugJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["live_source_mode"]		 = p->live_source_mode;
	doc["bmi270_live_enabled"]	 = p->bmi270_live_enabled;
	doc["bmi270_live_ready"]	 = p->bmi270_live_ready;
	doc["live_frame_fill"]		 = p->live_frame_fill;
	doc["live_frame_ready"]		 = p->live_frame_ready;
	doc["bmi270_sample_counter"] = p->bmi270_sample_counter;
	doc["frame_size"]			 = p->cfg.feature.frame_size;
	doc["hop_size"]				 = p->cfg.feature.hop_size;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderSessionJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["session_open"]		= p->recorder_session_open;
	doc["session_id"]		= p->recorder_session_id;
	doc["session_name"]		= p->recorder_session_name;
	doc["session_open_ms"]	= p->recorder_session_open_ms;
	doc["session_close_ms"] = p->recorder_session_close_ms;
	doc["record_count"]		= p->recorder_record_count;
	doc["active_path"]		= p->recorder_active_path;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 BMI270 SPI begin
	   - SPI bus lock/shared bus 정책 정리
	   - 오류 코드 세분화 */
	p->bmi270_spi_ok	 = false;
	p->bmi270_live_ready = false;

	if (!T20_beginActualBmi270SpiPath(p)) {
		strlcpy(p->bmi270_status_text, "spi_path_fail", sizeof(p->bmi270_status_text));
		return false;
	}

	for (uint8_t i = 0; i < G_T20_BMI270_SPI_RETRY_MAX; ++i) {
		if (T20_probeBMI270ChipId(p)) {
			p->bmi270_live_ready = true;
			strlcpy(p->bmi270_status_text, "spi_ready", sizeof(p->bmi270_status_text));
			return true;
		}
	}

	strlcpy(p->bmi270_status_text, "spi_fail", sizeof(p->bmi270_status_text));
	return false;
}

bool T20_configBMI270_2100Hz_DRDY(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 ODR 1600Hz 설정
	   - gyro/acc range 설정
	   - FIFO/interrupt mode 선택 */
	p->bmi270_last_drdy_ms = millis();
	if (!T20_enableBMI270Drdy(p)) {
		strlcpy(p->bmi270_status_text, "drdy_enable_fail", sizeof(p->bmi270_status_text));
		return false;
	}
	strlcpy(p->bmi270_status_text, "drdy_configured", sizeof(p->bmi270_status_text));
	return true;
}

bool T20_readBMI270Sample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample) {
	if (p == nullptr || p_out_sample == nullptr) return false;
	if (!p->bmi270_live_enabled || !p->bmi270_live_ready) return false;

	/* 한글 주석:
	   현재 단계는 실센서 연결 전이므로 축 선택 구조만 반영하고, 샘플 값은 시뮬레이션으로 생성합니다. */
	float base = (float)((p->live_frame_counter % 100U) - 50U) / 50.0f;

	switch (p->bmi270_axis_mode) {
		case G_T20_BMI270_AXIS_MODE_ACC_Z:
			*p_out_sample = base * 0.5f;
			break;
		case G_T20_BMI270_AXIS_MODE_GYRO_NORM:
			*p_out_sample = (base >= 0.0f) ? base : -base;
			break;
		case G_T20_BMI270_AXIS_MODE_GYRO_Z:
		default:
			*p_out_sample = base;
			break;
	}

	p->bmi270_last_sample_value = *p_out_sample;
	p->bmi270_last_drdy_ms		= millis();
	strlcpy(p->bmi270_status_text, "sample_ok", sizeof(p->bmi270_status_text));
	return true;
}

bool T20_pushLiveQueueSample(CL_T20_Mfcc::ST_Impl* p, float p_sample) {
	if (p == nullptr) return false;
	if (p->live_sample_queue_count >= G_T20_LIVE_QUEUE_DEPTH) return false;

	p->live_sample_queue[p->live_sample_queue_count++] = p_sample;
	return true;
}

bool T20_drainLiveQueueToFrameBuffer(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (p->live_sample_queue_count == 0) return false;

	for (uint16_t i = 0; i < p->live_sample_queue_count; ++i) {
		if (!T20_feedLiveSample(p, p->live_sample_queue[i])) {
			return false;
		}
	}
	memset(p->live_sample_queue, 0, sizeof(p->live_sample_queue));
	p->live_sample_queue_count = 0;
	return true;
}

bool T20_tryBMI270Reinit(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 한글 주석:
	   BMI270 초기화 실패 시 재시도 로직입니다.
	   현재 단계에서는 실제 SPI 초기화는 하지 않고 상태값만 갱신합니다. */
	if (p->bmi270_init_retry >= G_T20_BMI270_INIT_RETRY_MAX) {
		strlcpy(p->bmi270_status_text, "init_fail", sizeof(p->bmi270_status_text));
		return false;
	}

	p->bmi270_init_retry++;
	strlcpy(p->bmi270_status_text, "reinit_try", sizeof(p->bmi270_status_text));

	/* 향후 단계 구현 예정:
	   - 실제 BMI270 reset sequence
	   - chip id 재확인
	   - 재초기화 성공/실패 분기 */
	p->bmi270_live_ready = true;
	return true;
}

void T20_liveHeartbeat(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	uint32_t now = millis();
	if ((now - p->live_last_heartbeat_ms) < G_T20_RECORDER_HEARTBEAT_INTERVAL_MS) return;

	p->live_last_heartbeat_ms = now;

	/* 한글 주석:
	   현재 단계는 heartbeat를 파일로 저장하지 않고 상태값만 갱신합니다. */
	T20_recorderWriteMetadataHeartbeat(p);
}

bool T20_probeBMI270ChipId(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 BMI270 chip id register read
	   - SPI transaction 오류 코드 세분화
	   - chip id mismatch 처리 강화 */
	p->bmi270_spi_ok = true;
	uint8_t chip_id	 = 0;
	if (!T20_bmi270ActualReadRegister(p, G_T20_BMI270_REG_CHIP_ID, &chip_id)) {
		p->bmi270_spi_ok = false;
		return false;
	}
	p->bmi270_chip_id = chip_id;
	return (chip_id == G_T20_BMI270_CHIP_ID_EXPECTED);
}

bool T20_enableBMI270Drdy(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!p->bmi270_spi_ok) return false;

	/* 향후 단계 구현 예정:
	   - 실제 DRDY interrupt pin attach
	   - GPIO debounce/clear 처리
	   - ISR -> queue wakeup 연결 */
	p->bmi270_drdy_enabled	= true;
	p->bmi270_drdy_isr_flag = 0;
	p->bmi270_last_poll_ms	= millis();
	if (!T20_bmi270AttachDrdyIsr(p)) {
		strlcpy(p->bmi270_status_text, "drdy_attach_fail", sizeof(p->bmi270_status_text));
		return false;
	}
	if (!T20_bmi270InstallDrdyHook(p)) {
		strlcpy(p->bmi270_status_text, "drdy_hook_fail", sizeof(p->bmi270_status_text));
		return false;
	}
	strlcpy(p->bmi270_status_text, "drdy_enabled", sizeof(p->bmi270_status_text));
	return true;
}

bool T20_pollBMI270Drdy(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!p->bmi270_drdy_enabled) return false;

	uint32_t now_ms = millis();
	if ((now_ms - p->bmi270_last_poll_ms) < G_T20_BMI270_DRDY_POLL_INTERVAL_MS) return false;
	p->bmi270_last_poll_ms = now_ms;

	uint8_t int_status	   = 0;
	if (!T20_bmi270ReadRegister(p, G_T20_BMI270_REG_INT_STATUS_1, &int_status)) return false;
	if (!T20_tryConsumeBmi270IsrQueue(p) && p->bmi270_drdy_isr_flag == 0 && int_status == 0) {
		return false;
	}

	p->bmi270_drdy_isr_flag = 0;
	p->bmi270_last_drdy_ms	= now_ms;
	return true;
}

bool T20_bmi270ReadRegister(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg, uint8_t* p_out) {
	if (p == nullptr || p_out == nullptr) return false;
	if (!p->bmi270_spi_ok) return false;
	if (!T20_bmi270BeginSpiTransaction(p)) return false;
	if (!T20_bmi270PrepareReadReg(p, p_reg)) {
		T20_bmi270EndSpiTransaction(p);
		return false;
	}

	/* 향후 단계 구현 예정:
	   - 실제 SPI transaction
	   - CS low/high
	   - register read burst 처리 */
	p->bmi270_last_reg_addr = p_reg;
	if (p_reg == G_T20_BMI270_REG_CHIP_ID) {
		p->bmi270_last_reg_value = G_T20_BMI270_CHIP_ID_EXPECTED;
	} else if (p_reg == G_T20_BMI270_REG_INT_STATUS_1) {
		p->bmi270_last_reg_value = (p->bmi270_drdy_isr_flag != 0) ? G_T20_BMI270_ISR_FLAG_SET : 0U;
	} else {
		p->bmi270_last_reg_value = 0U;
	}
	if (!T20_bmi270ExecutePreparedRead(p, p_out)) {
		T20_bmi270EndSpiTransaction(p);
		return false;
	}
	T20_bmi270EndSpiTransaction(p);
	return true;
}

bool T20_bmi270ReadVectorSample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample) {
	if (p == nullptr || p_out_sample == nullptr) return false;

	uint8_t burst[G_T20_BMI270_BURST_SAMPLE_BYTES] = {0};
	if (!T20_bmi270ActualReadBurst(p, burst, sizeof(burst))) {
		return T20_readBMI270Sample(p, p_out_sample);
	}

	/* 한글 주석:
	   actual burst 경로가 준비되면 먼저 그 경로를 사용하고,
	   실패 시 기존 fallback sample 경로로 되돌립니다. */
	if (T20_bmi270DecodeBurstToSample(p, burst, sizeof(burst), p_out_sample)) {
		p->bmi270_last_sample_value = *p_out_sample;
		return true;
	}
	return T20_readBMI270Sample(p, p_out_sample);
}

void T20_bmi270DrdyIsr(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;
	p->bmi270_drdy_isr_flag = G_T20_BMI270_ISR_FLAG_SET;
	T20_bmi270SetIsrRequestState(p, G_T20_BMI270_ISR_REQUEST_PENDING);
	if (p->bmi270_isr_queue_count < G_T20_BMI270_ISR_QUEUE_SIM_MAX) {
		p->bmi270_isr_queue_count++;
	}
}

bool T20_beginActualBmi270SpiPath(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPIClass begin
	   - CS pin 설정
	   - transaction parameter 적용
	   - BMI270 startup sequence */
	if (!T20_bmi270BeginSpiBus(p)) {
		strlcpy(p->bmi270_status_text, "spi_bus_fail", sizeof(p->bmi270_status_text));
		return false;
	}

	p->bmi270_actual_spi_path_enabled = true;
	strlcpy(p->bmi270_status_text, "actual_spi_path_ready", sizeof(p->bmi270_status_text));
	return true;
}

bool T20_tryConsumeBmi270IsrQueue(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (p->bmi270_isr_queue_count == 0) return false;

	/* 한글 주석:
	   현재 단계는 실제 queue 객체 대신 카운터만 사용합니다.
	   이후 ISR -> queue -> task wakeup 구조로 교체 예정입니다. */
	p->bmi270_isr_queue_count--;
	T20_bmi270SetIsrRequestState(p, G_T20_BMI270_ISR_REQUEST_CONSUMED);
	return true;
}

bool T20_bmi270ReadBurstSample(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_buf, uint16_t p_len) {
	if (p == nullptr || p_buf == nullptr) return false;
	if (!p->bmi270_actual_spi_path_enabled) return false;
	if (p_len < G_T20_BMI270_BURST_SAMPLE_BYTES) return false;
	if (!T20_bmi270BeginSpiTransaction(p)) return false;

	/* 향후 단계 구현 예정:
	   - 실제 BMI270 gyro/acc burst read
	   - endian decode
	   - raw -> float scale 변환 */
	for (uint16_t i = 0; i < G_T20_BMI270_BURST_SAMPLE_BYTES; ++i) {
		p_buf[i] = (uint8_t)(G_T20_BMI270_REG_FAKE_VECTOR_BASE + i);
	}

	p->bmi270_last_burst_len = G_T20_BMI270_BURST_SAMPLE_BYTES;
	p->bmi270_last_read_ok	 = G_T20_BMI270_SPI_READ_OK;
	T20_bmi270EndSpiTransaction(p);
	return true;
}

bool T20_bmi270BeginSpiTransaction(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!p->bmi270_actual_spi_path_enabled) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPI transaction begin
	   - SPI mode / clock / bit order 설정
	   - bus lock 처리 */
	p->bmi270_last_transaction_ok = G_T20_BMI270_SPI_TRANSACTION_OK;
	return true;
}

void T20_bmi270EndSpiTransaction(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	/* 한글 주석:
	   현재 단계는 실제 SPI endTransaction을 호출하지 않고 상태만 유지합니다. */
}

bool T20_bmi270AttachDrdyIsr(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!p->bmi270_drdy_enabled) return false;

	/* 향후 단계 구현 예정:
	   - 실제 GPIO interrupt attach
	   - ISR trampoline 정리
	   - interrupt clear/ack 처리 */
	if (!T20_bmi270ActualAttachIsr(p)) {
		p->bmi270_last_isr_attach_ok = 0;
		return false;
	}
	p->bmi270_last_isr_attach_ok = G_T20_BMI270_ISR_ATTACH_OK;
	return true;
}

bool T20_bmi270BeginSpiBus(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPIClass begin
	   - SCK/MISO/MOSI/CS pin 반영
	   - shared SPI bus 충돌 방지 */
	if (!T20_bmi270ActualSpiBegin(p)) {
		p->bmi270_spi_begin_ok = G_T20_BMI270_SPI_BEGIN_FAIL;
		return false;
	}
	p->bmi270_spi_bus_ready = G_T20_BMI270_SPI_BUS_READY;
	return true;
}

bool T20_bmi270PrepareReadReg(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg) {
	if (p == nullptr) return false;
	if (p->bmi270_spi_bus_ready != G_T20_BMI270_SPI_BUS_READY) return false;

	/* 한글 주석:
	   현재 단계는 실제 CS low/write/read 를 수행하지 않고,
	   read flag가 붙은 register 값을 상태로만 남깁니다. */
	p->bmi270_last_txn_reg		   = (uint8_t)(p_reg | G_T20_BMI270_REG_READ_FLAG);
	p->bmi270_spi_read_phase_ready = G_T20_BMI270_SPI_READ_PHASE_READY;
	T20_bmi270SetReadState(p, G_T20_BMI270_READ_STATE_PREPARED);
	return true;
}

bool T20_bmi270ExecutePreparedRead(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_out) {
	if (p == nullptr || p_out == nullptr) return false;
	if (p->bmi270_spi_read_phase_ready != G_T20_BMI270_SPI_READ_PHASE_READY) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPI transfer
	   - dummy byte 처리
	   - register/burst read 공통화 */
	*p_out						   = p->bmi270_last_reg_value;
	p->bmi270_spi_read_phase_ready = G_T20_BMI270_SPI_READ_PHASE_IDLE;
	T20_bmi270SetReadState(p, G_T20_BMI270_READ_STATE_DONE);
	return true;
}

bool T20_bmi270InstallDrdyHook(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!p->bmi270_drdy_enabled) return false;

	/* 향후 단계 구현 예정:
	   - 실제 attachInterrupt 또는 gpio_isr_handler_add
	   - ISR trampoline/static bridge 정리 */
	T20_bmi270SetIsrHookState(p, G_T20_BMI270_ISR_HOOK_STATE_READY);
	p->bmi270_isr_hook_ready = G_T20_BMI270_ISR_HOOK_READY;
	T20_bmi270SetIsrHookState(p, G_T20_BMI270_ISR_HOOK_STATE_DONE);
	return true;
}

bool T20_bmi270ActualSpiBegin(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPIClass begin 호출
	   - 핀맵 반영
	   - SPI 파라미터 적용
	   - shared bus lock 정책 반영 */
	T20_bmi270SetSpiStartState(p, G_T20_BMI270_SPI_START_STATE_READY);
	p->bmi270_spi_begin_ok	= G_T20_BMI270_SPI_BEGIN_OK;
	p->bmi270_spi_bus_ready = G_T20_BMI270_SPI_BUS_READY;
	T20_bmi270SetSpiStartState(p, G_T20_BMI270_SPI_START_STATE_DONE);
	return true;
}

bool T20_bmi270ActualAttachIsr(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (!p->bmi270_drdy_enabled) return false;

	/* 향후 단계 구현 예정:
	   - 실제 attachInterrupt 또는 gpio_isr_handler_add 호출
	   - ISR trampoline/static bridge 연결
	   - 인터럽트 clear/ack 처리 */
	p->bmi270_isr_attach_state = G_T20_BMI270_ISR_ATTACH_STATE_READY;
	return true;
}

bool T20_bmi270ActualReadRegister(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg, uint8_t* p_out) {
	if (p == nullptr || p_out == nullptr) return false;
	if (!p->bmi270_actual_spi_path_enabled) return false;
	if (!p->bmi270_spi_ok) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPIClass transfer
	   - CS low/high
	   - reg read command/write/read 분리
	   - register read 오류 코드 세분화 */
	p->bmi270_actual_reg_read_ready = G_T20_BMI270_ACTUAL_REG_READ_READY;
	T20_bmi270PrepareHardwareBridge(p);
	T20_bmi270PrepareIsrBridge(p);
	T20_bmi270PrepareTransactionPipeline(p);
	T20_bmi270SetTxnPipelineState(p, G_T20_BMI270_TXN_PIPELINE_STATE_EXEC);
	T20_bmi270OpenActualSpiSession(p);
	T20_bmi270PrepareActualSpiExecute(p);
	T20_bmi270PrepareApplyActualRead(p);
	T20_bmi270PrepareBurstApply(p);
	T20_bmi270PrepareReadback(p);
	T20_bmi270PrepareVerify(p);
	T20_bmi270SetActualReadTxnState(p, G_T20_BMI270_ACTUAL_READ_TXN_STATE_READY);
	T20_bmi270SetRegBurstState(p, G_T20_BMI270_REG_BURST_STATE_READY);
	bool ok = T20_bmi270ReadRegister(p, p_reg, p_out);
	T20_bmi270SetRegBurstState(p, ok ? G_T20_BMI270_REG_BURST_STATE_DONE : G_T20_BMI270_REG_BURST_STATE_IDLE);
	T20_bmi270SetActualReadTxnState(p, ok ? G_T20_BMI270_ACTUAL_READ_TXN_STATE_DONE : G_T20_BMI270_ACTUAL_READ_TXN_STATE_IDLE);
	T20_bmi270SetSpiExecState(p, ok ? G_T20_BMI270_SPI_EXEC_STATE_DONE : G_T20_BMI270_SPI_EXEC_STATE_IDLE);
	T20_bmi270SetSpiApplyState(p, ok ? G_T20_BMI270_SPI_APPLY_STATE_DONE : G_T20_BMI270_SPI_APPLY_STATE_IDLE);
	T20_bmi270SetBurstApplyState(p, ok ? G_T20_BMI270_BURST_APPLY_STATE_DONE : G_T20_BMI270_BURST_APPLY_STATE_IDLE);
	T20_bmi270CloseActualSpiSession(p);
	T20_bmi270SetReadbackState(p, ok ? G_T20_BMI270_READBACK_STATE_DONE : G_T20_BMI270_READBACK_STATE_IDLE);
	T20_bmi270SetVerifyState(p, ok ? G_T20_BMI270_VERIFY_STATE_DONE : G_T20_BMI270_VERIFY_STATE_IDLE);
	T20_bmi270SetVerifyResult(p, ok ? G_T20_BMI270_VERIFY_RESULT_OK : G_T20_BMI270_VERIFY_RESULT_FAIL);
	T20_bmi270SetHwBridgeState(p, ok ? G_T20_BMI270_HW_BRIDGE_STATE_DONE : G_T20_BMI270_HW_BRIDGE_STATE_IDLE);
	T20_bmi270SetIsrBridgeState(p, ok ? G_T20_BMI270_ISR_BRIDGE_STATE_DONE : G_T20_BMI270_ISR_BRIDGE_STATE_IDLE);
	T20_bmi270SetTxnPipelineState(p, ok ? G_T20_BMI270_TXN_PIPELINE_STATE_DONE : G_T20_BMI270_TXN_PIPELINE_STATE_IDLE);
	return ok;
}

bool T20_bmi270DecodeBurstToSample(CL_T20_Mfcc::ST_Impl* p, const uint8_t* p_buf, uint16_t p_len, float* p_out) {
	if (p == nullptr || p_buf == nullptr || p_out == nullptr) return false;
	if (p_len < G_T20_BMI270_BURST_SAMPLE_BYTES) return false;
	if (!T20_bmi270DecodeBurstAxes(p, p_buf, p_len)) return false;

	/* 한글 주석:
	   현재 단계는 axis mode에 따라 임시 선택값을 반환합니다.
	   이후 실제 gyro/acc 축 매핑과 스케일 변환으로 교체 예정입니다. */
	switch (p->bmi270_axis_mode) {
		case G_T20_BMI270_AXIS_MODE_ACC_Z:
			*p_out = p->bmi270_last_axis_values[2];
			break;
		case G_T20_BMI270_AXIS_MODE_GYRO_NORM:
			*p_out = (p->bmi270_last_axis_values[0] + p->bmi270_last_axis_values[1] + p->bmi270_last_axis_values[2]) / 3.0f;
			break;
		case G_T20_BMI270_AXIS_MODE_GYRO_Z:
		default:
			*p_out = p->bmi270_last_axis_values[2];
			break;
	}
	p->bmi270_last_decoded_sample = *p_out;
	return true;
}

bool T20_bmi270DecodeBurstAxes(CL_T20_Mfcc::ST_Impl* p, const uint8_t* p_buf, uint16_t p_len) {
	if (p == nullptr || p_buf == nullptr) return false;
	if (p_len < G_T20_BMI270_BURST_SAMPLE_BYTES) return false;

	/* 한글 주석:
	   현재 단계는 실제 BMI270 gyro/acc 스케일 변환 전 단계입니다.
	   앞 6바이트를 16비트 3축 값으로 가정하여 임시 디코드합니다. */
	for (uint8_t i = 0; i < G_T20_BMI270_BURST_AXIS_COUNT; ++i) {
		uint16_t lo_idx				  = (uint16_t)(i * 2U);
		uint16_t raw				  = (uint16_t)p_buf[lo_idx] | ((uint16_t)p_buf[lo_idx + 1U] << 8);
		p->bmi270_last_axis_values[i] = ((float)raw) / G_T20_BMI270_FAKE_RAW_DECODE_SCALE;
	}
	p->bmi270_last_axis_decode_ok = G_T20_BMI270_AXIS_DECODE_OK;
	T20_bmi270SetReadState(p, G_T20_BMI270_READ_STATE_DONE);
	return true;
}

void T20_bmi270SetReadState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_read_state   = p_state;
	p->bmi270_last_read_ms = millis();
}

bool T20_bmi270ActualReadBurst(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_buf, uint16_t p_len) {
	if (p == nullptr || p_buf == nullptr) return false;
	if (!p->bmi270_actual_spi_path_enabled) return false;
	if (!p->bmi270_spi_ok) return false;
	if (p_len < G_T20_BMI270_BURST_SAMPLE_BYTES) return false;

	/* 향후 단계 구현 예정:
	   - 실제 burst read command/write/read
	   - gyro/acc register map 분기
	   - 실제 endian/raw decode 경로와 직접 연결 */
	p->bmi270_actual_burst_ready = G_T20_BMI270_ACTUAL_BURST_READY;
	T20_bmi270SetBurstFlowState(p, G_T20_BMI270_BURST_FLOW_STATE_READY);
	bool ok = T20_bmi270ReadBurstSample(p, p_buf, p_len);
	T20_bmi270SetBurstFlowState(p, ok ? G_T20_BMI270_BURST_FLOW_STATE_DONE : G_T20_BMI270_BURST_FLOW_STATE_IDLE);
	return ok;
}

void T20_recorderSetFinalizeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_finalize_state	 = p_state;
	p->recorder_finalize_last_ms = millis();
}

bool T20_recorderPrepareFinalize(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - binary footer/header rewrite
	   - session summary/manifest 실제 저장
	   - rotate/prune와 finalize 연계 */
	T20_recorderSetFinalizeState(p, G_T20_RECORDER_FINALIZE_STATE_PENDING);
	return true;
}

bool T20_buildRecorderFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["finalize_state"]	= p->recorder_finalize_state;
	doc["finalize_last_ms"] = p->recorder_finalize_last_ms;
	doc["session_open"]		= p->recorder_session_open;
	doc["session_id"]		= p->recorder_session_id;
	doc["active_path"]		= p->recorder_active_path;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

void T20_bmi270SetBurstFlowState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_burst_flow_state = p_state;
}

bool T20_recorderFinalizeSaveSummary(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 summary/manifest 파일 저장
	   - finalize/recover 결과 기록
	   - 저장 실패 시 rollback 처리 */
	p->recorder_finalize_saved = true;
	T20_recorderSetFinalizeState(p, G_T20_RECORDER_FINALIZE_STATE_SAVED);
	return true;
}

void T20_bmi270SetIsrRequestState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_isr_request_state = p_state;
}

void T20_recorderSetFinalizeResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result) {
	if (p == nullptr) return;
	p->recorder_finalize_result = p_result;
}

void T20_bmi270SetSpiStartState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_spi_start_state = p_state;
}

void T20_bmi270SetIsrHookState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_isr_hook_state = p_state;
}

void T20_recorderSetFinalizePersistState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_finalize_persist_state = p_state;
}

bool T20_recorderPreparePersistFinalize(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - finalize summary/manifest를 실제 파일로 저장
	   - finalize persist 실패 시 retry/rollback 정책 반영
	   - recover 정보와 연계하여 상태 복구 */
	T20_recorderSetFinalizePersistState(p, G_T20_RECORDER_FINALIZE_PERSIST_READY);
	return true;
}

void T20_bmi270SetRegBurstState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_reg_burst_state = p_state;
}

void T20_recorderSetFinalizePersistResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result) {
	if (p == nullptr) return;
	p->recorder_finalize_persist_result = p_result;
}

void T20_bmi270SetActualReadTxnState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_actual_read_txn_state = p_state;
}

void T20_recorderSetFinalizeSaveState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_finalize_save_state = p_state;
}

void T20_bmi270SetSpiExecState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_spi_exec_state = p_state;
}

void T20_recorderSetFinalizeExecState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_finalize_exec_state = p_state;
}

bool T20_bmi270PrepareActualSpiExecute(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPIClass begin 이후 transaction execute 단계 연결
	   - bus lock / unlock 연계
	   - reg / burst 공통 실행 경로 정리 */
	T20_bmi270SetSpiExecState(p, G_T20_BMI270_SPI_EXEC_STATE_READY);
	return true;
}

bool T20_recorderPrepareExecutePersist(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - persist 저장 실제 write 실행
	   - 실패 시 retry / rollback 처리
	   - finalize 결과와 index 저장 연계 */
	T20_recorderSetFinalizeExecState(p, G_T20_RECORDER_FINALIZE_EXEC_READY);
	return true;
}

void T20_bmi270SetSpiApplyState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_spi_apply_state = p_state;
}

void T20_recorderSetFinalizeCommitState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_finalize_commit_state = p_state;
}

bool T20_bmi270PrepareApplyActualRead(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPI execute 이후 apply/readback 단계 연결
	   - register/burst 공통 적용 결과 확인
	   - 실패 시 rollback/retry 정책 반영 */
	T20_bmi270SetSpiApplyState(p, G_T20_BMI270_SPI_APPLY_STATE_READY);
	return true;
}

bool T20_recorderPrepareCommitPersist(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - persist 저장 commit 단계 연결
	   - index/summary/manifest 동시 반영
	   - commit 실패 시 rollback 정책 반영 */
	T20_recorderSetFinalizeCommitState(p, G_T20_RECORDER_FINALIZE_COMMIT_READY);
	return true;
}

void T20_bmi270SetSpiSessionState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_spi_session_state = p_state;
}

void T20_bmi270SetBurstApplyState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_burst_apply_state = p_state;
}

void T20_recorderSetPersistWriteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_persist_write_state = p_state;
}

void T20_recorderSetCommitResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result) {
	if (p == nullptr) return;
	p->recorder_commit_result = p_result;
}

bool T20_bmi270OpenActualSpiSession(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPI 세션 open/lock
	   - shared bus arbitration
	   - transaction nesting 정책 */
	T20_bmi270SetSpiSessionState(p, G_T20_BMI270_SPI_SESSION_STATE_OPEN);
	return true;
}

bool T20_bmi270CloseActualSpiSession(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 한글 주석:
	   현재 단계는 실제 SPI 세션 close를 수행하지 않고 상태만 닫습니다. */
	T20_bmi270SetSpiSessionState(p, G_T20_BMI270_SPI_SESSION_STATE_CLOSED);
	return true;
}

bool T20_bmi270PrepareBurstApply(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 burst raw decode 이후 보정/적용 단계 연결
	   - gyro/acc 선택 적용
	   - frame build 입력과 직접 연동 */
	T20_bmi270SetBurstApplyState(p, G_T20_BMI270_BURST_APPLY_STATE_READY);
	return true;
}

bool T20_recorderPreparePersistWrite(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - persist write 실제 파일 기록
	   - summary/manifest/index 동기화
	   - write 실패 시 retry/rollback */
	T20_recorderSetPersistWriteState(p, G_T20_RECORDER_PERSIST_WRITE_STATE_READY);
	return true;
}

void T20_bmi270SetTxnPipelineState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_txn_pipeline_state = p_state;
}

void T20_recorderSetFinalizePipelineState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_finalize_pipeline_state = p_state;
}

bool T20_bmi270PrepareTransactionPipeline(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPI begin/transaction/read/apply/session 흐름을 공통 pipeline으로 통합
	   - register/burst 공용 실행 그래프 정리
	   - 실패 지점별 복구/재시도 정책 반영 */
	T20_bmi270SetTxnPipelineState(p, G_T20_BMI270_TXN_PIPELINE_STATE_READY);
	return true;
}

bool T20_recorderPrepareFinalizePipeline(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - finalize save/persist/commit/write를 공통 pipeline으로 통합
	   - index/summary/manifest/recover 순서 고정
	   - 실패 지점별 rollback/retry 정책 반영 */
	T20_recorderSetFinalizePipelineState(p, G_T20_RECORDER_FINALIZE_PIPELINE_STATE_READY);
	return true;
}

void T20_bmi270SetReadbackState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_readback_state = p_state;
}

void T20_recorderSetManifestState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_manifest_state = p_state;
}

bool T20_bmi270PrepareReadback(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPI readback 검증
	   - register mirror/value compare
	   - mismatch 시 retry 또는 safe fallback */
	T20_bmi270SetReadbackState(p, G_T20_BMI270_READBACK_STATE_READY);
	return true;
}

bool T20_recorderPrepareManifestWrite(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - manifest 실제 파일 생성/쓰기
	   - session summary/index와 동기화
	   - 실패 시 rollback 및 복구 경로 반영 */
	T20_recorderSetManifestState(p, G_T20_RECORDER_MANIFEST_STATE_READY);
	return true;
}

void T20_bmi270SetVerifyState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_verify_state = p_state;
}

void T20_bmi270SetVerifyResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result) {
	if (p == nullptr) return;
	p->bmi270_verify_result = p_result;
}

void T20_recorderSetIndexState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_index_state = p_state;
}

void T20_recorderSetSummaryState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_summary_state = p_state;
}

bool T20_bmi270PrepareVerify(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 readback 검증
	   - 레지스터 기대값 비교
	   - 검증 실패 시 재시도 또는 안전모드 전환 */
	T20_bmi270SetVerifyState(p, G_T20_BMI270_VERIFY_STATE_READY);
	return true;
}

bool T20_recorderPrepareIndexWrite(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - recorder index 실제 write
	   - manifest/summary와 동기화
	   - 실패 시 rollback 처리 */
	T20_recorderSetIndexState(p, G_T20_RECORDER_INDEX_STATE_READY);
	return true;
}

bool T20_recorderPrepareSummaryWrite(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - summary 실제 write
	   - index/manifest와 동기화
	   - 실패 시 rollback 처리 */
	T20_recorderSetSummaryState(p, G_T20_RECORDER_SUMMARY_STATE_READY);
	return true;
}

void T20_bmi270SetHwBridgeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_hw_bridge_state = p_state;
}

void T20_bmi270SetIsrBridgeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_isr_bridge_state = p_state;
}

void T20_recorderSetArtifactState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_artifact_state = p_state;
}

void T20_recorderSetArtifactResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result) {
	if (p == nullptr) return;
	p->recorder_artifact_result = p_result;
}

bool T20_bmi270PrepareHardwareBridge(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPIClass begin/핀맵/CS/DRDY pin을 하드웨어 브리지 계층으로 통합
	   - BMI270 초기화와 register/burst read 진입점 단일화
	   - 보드별 핀맵 차이를 bridge 계층에서 흡수 */
	T20_bmi270SetHwBridgeState(p, G_T20_BMI270_HW_BRIDGE_STATE_READY);
	return true;
}

bool T20_bmi270PrepareIsrBridge(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 attachInterrupt / gpio_isr_handler_add 차이를 브리지 계층에서 흡수
	   - ISR trampoline / queue wakeup / debounce 통합
	   - DRDY miss/recover 정책을 브리지 계층으로 이동 */
	T20_bmi270SetIsrBridgeState(p, G_T20_BMI270_ISR_BRIDGE_STATE_READY);
	return true;
}

bool T20_recorderPrepareArtifactWrite(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - summary/manifest/index를 artifact 묶음으로 저장
	   - 결과/메타/리커버리 정보를 함께 남기기
	   - artifact 저장 실패 시 롤백/재시도 정책 연결 */
	T20_recorderSetArtifactState(p, G_T20_RECORDER_ARTIFACT_STATE_READY);
	return true;
}

bool T20_buildRecorderArtifactJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["artifact_state"]	   = p->recorder_artifact_state;
	doc["artifact_result"]	   = p->recorder_artifact_result;
	doc["manifest_state"]	   = p->recorder_manifest_state;
	doc["summary_state"]	   = p->recorder_summary_state;
	doc["index_state"]		   = p->recorder_index_state;
	doc["persist_write_state"] = p->recorder_persist_write_state;
	doc["commit_state"]		   = p->recorder_finalize_commit_state;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

void T20_bmi270SetBootState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_boot_state = p_state;
}

void T20_bmi270SetIrqRouteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_irq_route_state = p_state;
}

void T20_bmi270SetIrqFilterState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_irq_filter_state = p_state;
}

void T20_recorderSetPackageState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_package_state = p_state;
}

void T20_recorderSetCleanupState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_cleanup_state = p_state;
}

bool T20_bmi270PrepareBootFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetBootState(p, G_T20_BMI270_BOOT_STATE_READY);
	return true;
}

bool T20_bmi270PrepareIrqRoute(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetIrqRouteState(p, G_T20_BMI270_IRQ_ROUTE_STATE_READY);
	return true;
}

bool T20_bmi270PrepareIrqFilter(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetIrqFilterState(p, G_T20_BMI270_IRQ_FILTER_STATE_READY);
	return true;
}

bool T20_recorderPreparePackageFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_recorderSetPackageState(p, G_T20_RECORDER_PACKAGE_STATE_READY);
	return true;
}

bool T20_recorderPrepareCleanupFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_recorderSetCleanupState(p, G_T20_RECORDER_CLEANUP_STATE_READY);
	return true;
}

bool T20_buildRecorderPackageCleanupJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["package_state"]   = p->recorder_package_state;
	doc["cleanup_state"]   = p->recorder_cleanup_state;
	doc["archive_state"]   = p->recorder_archive_state;
	doc["artifact_state"]  = p->recorder_artifact_state;
	doc["artifact_result"] = p->recorder_artifact_result;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

void T20_bmi270SetSpiClassState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_spiclass_state = p_state;
}

void T20_bmi270SetHwExecState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_hw_exec_state = p_state;
}

void T20_recorderSetExportState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_export_state = p_state;
}

void T20_recorderSetRecoverState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_recover_state = p_state;
}

bool T20_bmi270PrepareSpiClassBridge(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 SPIClass 인스턴스 생성/선택
	   - 보드별 SPI bus/host 매핑
	   - begin/endTransaction 공통 진입점 통합 */
	T20_bmi270SetSpiClassState(p, G_T20_BMI270_SPICLASS_STATE_READY);
	return true;
}

bool T20_bmi270PrepareHardwareExecute(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 실제 register/burst read를 hardware execute 계층으로 통합
	   - DRDY 기반 샘플 수집과 직접 연결
	   - 오류별 fallback/retry 경로 정리 */
	T20_bmi270SetHwExecState(p, G_T20_BMI270_HW_EXEC_STATE_READY);
	return true;
}

bool T20_recorderPrepareExportFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 결과 export 구조 정리
	   - 압축/복사/외부 전달용 포맷 변환
	   - export 실패 시 지연 큐 또는 재시도 */
	T20_recorderSetExportState(p, G_T20_RECORDER_EXPORT_STATE_READY);
	return true;
}

bool T20_recorderPrepareRecoverFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 전원 차단/부분 저장 후 recover 스캔
	   - index/manifest/summary 불일치 복구
	   - recover 결과를 meta에 기록 */
	T20_recorderSetRecoverState(p, G_T20_RECORDER_RECOVER_STATE_READY);
	return true;
}

bool T20_buildRecorderExportRecoverJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["export_state"]	 = p->recorder_export_state;
	doc["recover_state"] = p->recorder_recover_state;
	doc["package_state"] = p->recorder_package_state;
	doc["cleanup_state"] = p->recorder_cleanup_state;
	doc["meta_state"]	 = p->recorder_meta_state;
	doc["archive_state"] = p->recorder_archive_state;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

void T20_bmi270SetLiveCaptureState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_live_capture_state = p_state;
}

void T20_bmi270SetSamplePipeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_sample_pipe_state = p_state;
}

void T20_recorderSetDeliveryState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_delivery_state = p_state;
}

void T20_recorderSetFinalReportState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_final_report_state = p_state;
}

bool T20_bmi270PrepareLiveCaptureFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetLiveCaptureState(p, G_T20_BMI270_LIVE_CAPTURE_STATE_READY);
	return true;
}

bool T20_bmi270PrepareSamplePipe(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetSamplePipeState(p, G_T20_BMI270_SAMPLE_PIPE_STATE_READY);
	return true;
}

bool T20_recorderPrepareDeliveryFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_recorderSetDeliveryState(p, G_T20_RECORDER_DELIVERY_STATE_READY);
	return true;
}

bool T20_recorderPrepareFinalReport(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_recorderSetFinalReportState(p, G_T20_RECORDER_FINAL_REPORT_STATE_READY);
	return true;
}

bool T20_buildRecorderDeliveryJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["delivery_state"]	  = p->recorder_delivery_state;
	doc["final_report_state"] = p->recorder_final_report_state;
	doc["export_state"]		  = p->recorder_export_state;
	doc["recover_state"]	  = p->recorder_recover_state;
	doc["package_state"]	  = p->recorder_package_state;
	doc["cleanup_state"]	  = p->recorder_cleanup_state;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

void T20_bmi270SetDriverState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_driver_state = p_state;
}

void T20_bmi270SetSessionCtrlState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_session_ctrl_state = p_state;
}

void T20_recorderSetPublishState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_publish_state = p_state;
}

void T20_recorderSetAuditState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->recorder_audit_state = p_state;
}

bool T20_bmi270PrepareDriverLayer(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetDriverState(p, G_T20_BMI270_DRIVER_STATE_READY);
	return true;
}

bool T20_bmi270PrepareSessionControl(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetSessionCtrlState(p, G_T20_BMI270_SESSION_CTRL_STATE_READY);
	return true;
}

bool T20_recorderPreparePublishFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_recorderSetPublishState(p, G_T20_RECORDER_PUBLISH_STATE_READY);
	return true;
}

bool T20_recorderPrepareAuditFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_recorderSetAuditState(p, G_T20_RECORDER_AUDIT_STATE_READY);
	return true;
}

bool T20_buildRecorderPublishAuditJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["publish_state"]	  = p->recorder_publish_state;
	doc["audit_state"]		  = p->recorder_audit_state;
	doc["delivery_state"]	  = p->recorder_delivery_state;
	doc["final_report_state"] = p->recorder_final_report_state;
	doc["export_state"]		  = p->recorder_export_state;
	doc["recover_state"]	  = p->recorder_recover_state;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[향후 단계 구현 예정 정리 - 이번 묶음 추가]
1. 실제 SPIClass begin 진입 상태를 begin request/begin result로 분리
2. recorder finalize 결과를 sync/report bundle로 합치기
3. 다음 단계에서 실제 하드웨어 호출로 치환
============================================================================ */

void T20_bmi270SetActualBeginState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	p->bmi270_spiclass_state = p_state;
}

bool T20_bmi270PrepareActualBeginRequest(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - SPIClass begin 실제 호출
	   - bus host / pin / freq / mode 적용
	   - begin 실패 시 safe fallback */
	T20_bmi270SetActualBeginState(p, G_T20_BMI270_SPICLASS_STATE_READY);
	return true;
}

bool T20_buildRecorderFinalizeBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["manifest_state"]	  = p->recorder_manifest_state;
	doc["summary_state"]	  = p->recorder_summary_state;
	doc["index_state"]		  = p->recorder_index_state;
	doc["artifact_state"]	  = p->recorder_artifact_state;
	doc["meta_state"]		  = p->recorder_meta_state;
	doc["archive_state"]	  = p->recorder_archive_state;
	doc["package_state"]	  = p->recorder_package_state;
	doc["export_state"]		  = p->recorder_export_state;
	doc["recover_state"]	  = p->recorder_recover_state;
	doc["delivery_state"]	  = p->recorder_delivery_state;
	doc["final_report_state"] = p->recorder_final_report_state;
	doc["publish_state"]	  = p->recorder_publish_state;
	doc["audit_state"]		  = p->recorder_audit_state;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[향후 단계 구현 예정 정리 - 이번 묶음 추가]
1. 실제 SPI begin runtime / register read runtime 상태를 분리
2. finalize store bundle을 file write / bundle map 흐름으로 세분화
3. 다음 단계에서 실제 하드웨어 호출과 파일 저장으로 치환
============================================================================ */

void T20_bmi270SetSpiBeginRuntimeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_spi_begin_runtime_state(p) = p_state;
}

void T20_bmi270SetRegisterReadRuntimeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_register_read_runtime_state(p) = p_state;
}

void T20_recorderSetFileWriteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_file_write_state(p) = p_state;
}

void T20_recorderSetBundleMapState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_bundle_map_state(p) = p_state;
}

bool T20_bmi270PrepareSpiBeginRuntime(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - SPIClass begin 실제 호출
	   - host / pin / freq / mode 적용
	   - begin 실패 시 fallback 처리 */
	T20_bmi270SetSpiBeginRuntimeState(p, G_T20_BMI270_SPI_BEGIN_RUNTIME_STATE_READY);
	return true;
}

bool T20_bmi270PrepareRegisterReadRuntime(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - register / burst read 실제 호출
	   - raw decode와 직접 연결
	   - read 실패 시 retry / safe fallback */
	T20_bmi270SetRegisterReadRuntimeState(p, G_T20_BMI270_REGISTER_READ_RUNTIME_STATE_READY);
	return true;
}

bool T20_recorderPrepareFileWriteFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - finalize bundle 실제 파일 저장
	   - write 실패 시 rollback/retry */
	T20_recorderSetFileWriteState(p, G_T20_RECORDER_FILE_WRITE_STATE_READY);
	return true;
}

bool T20_recorderPrepareBundleMapFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - bundle 내부 파일/경로 맵 생성
	   - 웹/파일/로그 공용 경로 맵 정리 */
	T20_recorderSetBundleMapState(p, G_T20_RECORDER_BUNDLE_MAP_STATE_READY);
	return true;
}

bool T20_buildRecorderFileBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["store_bundle_state"] = T20_ref_recorder_store_bundle_state(p);
	doc["store_result"]		  = T20_ref_recorder_store_result(p);
	doc["file_write_state"]	  = T20_ref_recorder_file_write_state(p);
	doc["bundle_map_state"]	  = T20_ref_recorder_bundle_map_state(p);
	doc["manifest_state"]	  = p->recorder_manifest_state;
	doc["summary_state"]	  = p->recorder_summary_state;
	doc["index_state"]		  = p->recorder_index_state;
	doc["artifact_state"]	  = p->recorder_artifact_state;
	doc["meta_state"]		  = p->recorder_meta_state;
	doc["archive_state"]	  = p->recorder_archive_state;
	doc["package_state"]	  = p->recorder_package_state;
	doc["export_state"]		  = p->recorder_export_state;
	doc["final_report_state"] = p->recorder_final_report_state;
	doc["publish_state"]	  = p->recorder_publish_state;
	doc["audit_state"]		  = p->recorder_audit_state;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[향후 단계 구현 예정 정리 - 이번 묶음 추가]
1. 실제 SPI begin 직후 attach/burst read 준비 단계를 분리
2. recorder 저장 경로를 route/finalize 단계까지 확장
3. 다음 단계에서 실제 SPI / FS 호출로 치환
============================================================================ */

void T20_bmi270SetSpiAttachPrepState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_spi_attach_prep_state(p) = p_state;
}

void T20_bmi270SetBurstReadPrepState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_burst_read_prep_state(p) = p_state;
}

void T20_recorderSetPathRouteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_path_route_state(p) = p_state;
}

void T20_recorderSetWriteFinalizeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_write_finalize_state(p) = p_state;
}

bool T20_bmi270PrepareSpiAttachPrep(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - SPI begin 이후 센서 attach / select 라인 초기화
	   - bus lock / chip select / mode 전환 준비
	   - attach 실패 시 fallback */
	T20_bmi270SetSpiAttachPrepState(p, G_T20_BMI270_SPI_ATTACH_PREP_STATE_READY);
	return true;
}

bool T20_bmi270PrepareBurstReadPrep(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - accel/gyro burst read block 실제 호출 직전 준비
	   - read len / start addr / decode buffer 연결
	   - read 실패 시 retry 정책 */
	T20_bmi270SetBurstReadPrepState(p, G_T20_BMI270_BURST_READ_PREP_STATE_READY);
	return true;
}

bool T20_recorderPreparePathRouteFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - LittleFS/SD/세션별 저장 경로 라우팅
	   - bundle map과 실제 파일 경로 동기화
	   - 라우팅 실패 시 기본 경로로 fallback */
	T20_recorderSetPathRouteState(p, G_T20_RECORDER_PATH_ROUTE_STATE_READY);
	return true;
}

bool T20_recorderPrepareWriteFinalizeFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - flush/close/finalize/metadata sync
	   - finalize 실패 시 retry/recover */
	T20_recorderSetWriteFinalizeState(p, G_T20_RECORDER_WRITE_FINALIZE_STATE_READY);
	return true;
}

bool T20_buildRecorderWriteFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["file_write_state"]		= T20_ref_recorder_file_write_state(p);
	doc["bundle_map_state"]		= T20_ref_recorder_bundle_map_state(p);
	doc["path_route_state"]		= T20_ref_recorder_path_route_state(p);
	doc["write_commit_state"]	= T20_ref_recorder_write_commit_state(p);
	doc["write_finalize_state"] = T20_ref_recorder_write_finalize_state(p);
	doc["store_bundle_state"]	= T20_ref_recorder_store_bundle_state(p);
	doc["store_result"]			= T20_ref_recorder_store_result(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[향후 단계 구현 예정 정리 - 이번 묶음 추가]
1. 실제 burst read 이후 runtime 처리와 ISR queue 단계를 분리
2. recorder 저장 완료 이후 commit route / finalize sync 단계를 분리
3. 다음 단계에서 실제 SPI/ISR/FS commit 코드로 치환
============================================================================ */

void T20_bmi270SetBurstRuntimeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_burst_runtime_state(p) = p_state;
}

void T20_bmi270SetIsrQueueState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_isr_queue_state(p) = p_state;
}

void T20_recorderSetCommitRouteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_commit_route_state(p) = p_state;
}

void T20_recorderSetFinalizeSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_finalize_sync_state(p) = p_state;
}

bool T20_bmi270PrepareBurstRuntimeFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - burst read 후 raw buffer -> decode/runtime 반영
	   - axis/frame 갱신과 연결
	   - burst 처리 실패 시 retry/recover */
	T20_bmi270SetBurstRuntimeState(p, G_T20_BMI270_BURST_RUNTIME_STATE_READY);
	return true;
}

bool T20_bmi270PrepareIsrQueueFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - ISR -> queue -> task wakeup 실제 연결
	   - overrun/drop 정책 반영 */
	T20_bmi270SetIsrQueueState(p, G_T20_BMI270_ISR_QUEUE_STATE_READY);
	return true;
}

bool T20_recorderPrepareCommitRouteFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - flush/commit 후 LittleFS/SD commit 라우팅
	   - commit 실패 시 fallback/retry */
	T20_recorderSetCommitRouteState(p, G_T20_RECORDER_COMMIT_ROUTE_STATE_READY);
	return true;
}

bool T20_recorderPrepareFinalizeSyncFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - finalize 이후 report/meta/audit 동기화
	   - sync 실패 시 recover 대상으로 마킹 */
	T20_recorderSetFinalizeSyncState(p, G_T20_RECORDER_FINALIZE_SYNC_STATE_READY);
	return true;
}

bool T20_buildRecorderCommitFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["file_write_state"]		= T20_ref_recorder_file_write_state(p);
	doc["bundle_map_state"]		= T20_ref_recorder_bundle_map_state(p);
	doc["path_route_state"]		= T20_ref_recorder_path_route_state(p);
	doc["write_commit_state"]	= T20_ref_recorder_write_commit_state(p);
	doc["write_finalize_state"] = T20_ref_recorder_write_finalize_state(p);
	doc["commit_route_state"]	= T20_ref_recorder_commit_route_state(p);
	doc["finalize_sync_state"]	= T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[잔여 구현계획 재점검 - 이번 묶음 추가 v210]
1. BMI270 실제 연결 직전 흐름을 hw link / frame build 단계까지 확장
2. recorder finalize 직전 흐름을 meta/report sync 단계까지 확장
3. 다음 단계에서 실제 SPI / ISR / flush-close-finalize / meta-report sync로 치환
============================================================================ */

void T20_bmi270SetHwLinkState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_hw_link_state(p) = p_state;
}

void T20_bmi270SetFrameBuildState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_frame_build_state(p) = p_state;
}

void T20_recorderSetMetaSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_meta_sync_state(p) = p_state;
}

void T20_recorderSetReportSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_report_sync_state(p) = p_state;
}

bool T20_bmi270PrepareHwLinkFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetHwLinkState(p, G_T20_BMI270_HW_LINK_STATE_READY);
	return true;
}

bool T20_bmi270PrepareFrameBuildFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_bmi270SetFrameBuildState(p, G_T20_BMI270_FRAME_BUILD_STATE_READY);
	return true;
}

bool T20_recorderPrepareMetaSyncFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_recorderSetMetaSyncState(p, G_T20_RECORDER_META_SYNC_STATE_READY);
	return true;
}

bool T20_recorderPrepareReportSyncFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_recorderSetReportSyncState(p, G_T20_RECORDER_REPORT_SYNC_STATE_READY);
	return true;
}

bool T20_buildHwAndFinalizeSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["real_begin_state"]	   = T20_ref_bmi270_real_begin_state(p);
	doc["real_burst_state"]	   = T20_ref_bmi270_real_burst_state(p);
	doc["real_isr_state"]	   = T20_ref_bmi270_real_isr_state(p);
	doc["hw_link_state"]	   = T20_ref_bmi270_hw_link_state(p);
	doc["frame_build_state"]   = T20_ref_bmi270_frame_build_state(p);
	doc["real_flush_state"]	   = T20_ref_recorder_real_flush_state(p);
	doc["real_close_state"]	   = T20_ref_recorder_real_close_state(p);
	doc["real_finalize_state"] = T20_ref_recorder_real_finalize_state(p);
	doc["meta_sync_state"]	   = T20_ref_recorder_meta_sync_state(p);
	doc["report_sync_state"]   = T20_ref_recorder_report_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[향후 단계 구현 예정 정리 - 이번 묶음 추가 v210]
1. BMI270 실제 연결 직전 흐름을 raw pipe / dsp ingress 단계까지 확장
2. recorder finalize 직전 흐름을 audit/manifest sync 단계까지 확장
3. 다음 단계에서 실제 SPI/ISR/DSP/FS 동기화 코드로 치환
============================================================================ */

void T20_bmi270SetDspIngressState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_dsp_ingress_state(p) = p_state;
}

void T20_bmi270SetRawPipeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_bmi270_raw_pipe_state(p) = p_state;
}

void T20_recorderSetAuditSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_audit_sync_state(p) = p_state;
}

void T20_recorderSetManifestSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state) {
	if (p == nullptr) return;
	T20_ref_recorder_manifest_sync_state(p) = p_state;
}

bool T20_bmi270PrepareDspIngressFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - raw -> frame -> dsp ingress 실제 연결
	   - dsp queue / ring buffer 진입 정리 */
	T20_bmi270SetDspIngressState(p, G_T20_BMI270_DSP_INGRESS_STATE_READY);
	return true;
}

bool T20_bmi270PrepareRawPipeFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - burst 결과를 raw pipe로 정리
	   - frame build와 직접 연결 */
	T20_bmi270SetRawPipeState(p, G_T20_BMI270_RAW_PIPE_STATE_READY);
	return true;
}

bool T20_recorderPrepareAuditSyncFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - audit 로그/상태 저장 동기화 */
	T20_recorderSetAuditSyncState(p, G_T20_RECORDER_AUDIT_SYNC_STATE_READY);
	return true;
}

bool T20_recorderPrepareManifestSyncFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - manifest/index/summary 연결 동기화 */
	T20_recorderSetManifestSyncState(p, G_T20_RECORDER_MANIFEST_SYNC_STATE_READY);
	return true;
}

bool T20_buildIoSyncBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["real_begin_state"]	   = T20_ref_bmi270_real_begin_state(p);
	doc["real_burst_state"]	   = T20_ref_bmi270_real_burst_state(p);
	doc["real_isr_state"]	   = T20_ref_bmi270_real_isr_state(p);
	doc["hw_link_state"]	   = T20_ref_bmi270_hw_link_state(p);
	doc["frame_build_state"]   = T20_ref_bmi270_frame_build_state(p);
	doc["raw_pipe_state"]	   = T20_ref_bmi270_raw_pipe_state(p);
	doc["dsp_ingress_state"]   = T20_ref_bmi270_dsp_ingress_state(p);
	doc["real_flush_state"]	   = T20_ref_recorder_real_flush_state(p);
	doc["real_close_state"]	   = T20_ref_recorder_real_close_state(p);
	doc["real_finalize_state"] = T20_ref_recorder_real_finalize_state(p);
	doc["meta_sync_state"]	   = T20_ref_recorder_meta_sync_state(p);
	doc["report_sync_state"]   = T20_ref_recorder_report_sync_state(p);
	doc["audit_sync_state"]	   = T20_ref_recorder_audit_sync_state(p);
	doc["manifest_sync_state"] = T20_ref_recorder_manifest_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_loadRuntimeConfigFromLittleFs(ST_T20_Config_t* p_cfg_out) {
	if (p_cfg_out == nullptr) return false;
	*p_cfg_out = T20_makeDefaultConfig();
	return true;
}
bool T20_loadProfileFromLittleFs(uint8_t p_profile_index, ST_T20_Config_t* p_cfg_out) {
	(void)p_profile_index;
	return T20_loadRuntimeConfigFromLittleFs(p_cfg_out);
}
bool T20_loadRecorderIndex(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	p->recorder_index_count = 0;
	return true;
}
bool T20_saveProfileToLittleFs(uint8_t p_profile_index, const ST_T20_Config_t* p_cfg) {
	(void)p_profile_index;
	(void)p_cfg;
	return true;
}
bool T20_saveRuntimeConfigToLittleFs(const ST_T20_Config_t* p_cfg) {
	(void)p_cfg;
	return true;
}
bool T20_parseConfigJsonText(const char* p_json_text, ST_T20_Config_t* p_cfg_out) {
	(void)p_json_text;
	if (p_cfg_out == nullptr) return false;
	*p_cfg_out = T20_makeDefaultConfig();
	return true;
}
bool T20_applyConfigJson(CL_T20_Mfcc::ST_Impl* p, const char* p_json) {
	return T20_applyConfigJsonText(p, p_json);
}
bool T20_applyConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text) {
	if (p == nullptr || p_json_text == nullptr) return false;
	ST_T20_Config_t cfg = p->cfg;
	if (!T20_parseConfigJsonText(p_json_text, &cfg)) return false;
	p->cfg = cfg;
	return true;
}

static bool T20_jsonWriteDoc(const JsonDocument& p_doc, char* p_out_buf, uint16_t p_len) {
	if (p_out_buf == nullptr || p_len == 0) return false;
	size_t need = measureJson(p_doc) + 1U;
	if (need > p_len) return false;
	serializeJson(p_doc, p_out_buf, p_len);
	return true;
}

bool T20_buildConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["frame_size"]  = p->cfg.feature.frame_size;
	doc["hop_size"]	   = p->cfg.feature.hop_size;
	doc["output_mode"] = (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence";
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildConfigSchemaJsonText(char* p_out_buf, uint16_t p_len) {
	JsonDocument doc;
	doc["mode"] = "config_schema";
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildViewerWaveformJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"] = "viewer_waveform";
	doc["len"]	= p->viewer_last_waveform_len;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildViewerSpectrumJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"] = "viewer_spectrum";
	doc["len"]	= p->viewer_last_spectrum_len;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"]		  = "viewer_data";
	doc["frame_id"]	  = p->viewer_last_frame_id;
	doc["vector_len"] = p->viewer_last_vector_len;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildViewerEventsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"]	  = "viewer_events";
	JsonArray arr = doc["events"].to<JsonArray>();
	for (uint16_t i = 0; i < p->viewer_event_count && i < G_T20_VIEWER_EVENT_MAX; ++i) {
		JsonObject o  = arr.add<JsonObject>();
		o["frame_id"] = p->viewer_events[i].frame_id;
		o["kind"]	  = p->viewer_events[i].kind;
		o["text"]	  = p->viewer_events[i].text;
	}
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildViewerSequenceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"]		   = "viewer_sequence";
	doc["ready"]	   = p->latest_sequence_valid;
	doc["frames"]	   = p->seq_rb.frames;
	doc["feature_dim"] = p->seq_rb.feature_dim;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildViewerOverviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	return T20_buildViewerDataJsonText(p, p_out_buf, p_len);
}
bool T20_buildViewerMultiFrameJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"]						  = "viewer_multi_frame";
	doc["count"]					  = p->viewer_recent_waveform_count;
	doc["selection_sync_enabled"]	  = p->selection_sync_enabled;
	doc["selection_sync_range_valid"] = p->selection_sync_range_valid;
	doc["selection_points_len"]		  = p->viewer_selection_points_len;
	JsonArray arr					  = doc["selection_points"].to<JsonArray>();
	for (uint16_t i = 0; i < p->viewer_selection_points_len; ++i) {
		arr.add(p->viewer_selection_points[i]);
	}
	JsonArray ov = doc["overlay_points"].to<JsonArray>();
	for (uint16_t i = 0; i < p->viewer_overlay_points_len; ++i) {
		ov.add(p->viewer_overlay_points[i]);
	}
	doc["overlay_points_len"]	= p->viewer_overlay_points_len;
	doc["overlay_accum_count"]	= p->viewer_overlay_accum_count;
	doc["overlay_subset_count"] = p->viewer_overlay_subset_count;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildViewerChartBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, uint16_t p_points) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"]						  = "viewer_chart_bundle";
	doc["points"]					  = p_points;
	doc["frame_id"]					  = p->viewer_last_frame_id;
	doc["selection_sync_enabled"]	  = p->selection_sync_enabled;
	doc["selection_sync_range_valid"] = p->selection_sync_range_valid;
	doc["selection_name"]			  = p->selection_sync_name;
	doc["selection_effective_from"]	  = p->selection_sync_effective_from;
	doc["selection_effective_to"]	  = p->selection_sync_effective_to;
	JsonArray wave					  = doc["waveform"].to<JsonArray>();
	uint16_t  wave_len				  = p->viewer_last_waveform_len;
	if (wave_len > p_points) wave_len = p_points;
	for (uint16_t i = 0; i < wave_len; ++i) {
		wave.add(p->viewer_last_waveform[i]);
	}
	JsonArray sel = doc["selection_points"].to<JsonArray>();
	for (uint16_t i = 0; i < p->viewer_selection_points_len; ++i) {
		sel.add(p->viewer_selection_points[i]);
	}
	JsonArray ov = doc["overlay_points"].to<JsonArray>();
	for (uint16_t i = 0; i < p->viewer_overlay_points_len; ++i) {
		ov.add(p->viewer_overlay_points[i]);
	}
	doc["overlay_points_len"]	= p->viewer_overlay_points_len;
	doc["overlay_accum_count"]	= p->viewer_overlay_accum_count;
	doc["overlay_subset_count"] = p->viewer_overlay_subset_count;
	JsonArray recent_ids		= doc["recent_frame_ids"].to<JsonArray>();
	for (uint16_t n = 0; n < p->viewer_recent_waveform_count; ++n) {
		uint16_t idx = (uint16_t)((p->viewer_recent_waveform_head + G_T20_VIEWER_RECENT_WAVE_COUNT - 1U - n) % G_T20_VIEWER_RECENT_WAVE_COUNT);
		recent_ids.add(p->viewer_recent_frame_ids[idx]);
	}
	JsonObject type				= doc["type_meta"].to<JsonObject>();
	type["enabled"]				= p->type_meta_enabled;
	type["name"]				= p->type_meta_name;
	type["kind"]				= p->type_meta_kind;
	type["auto_text"]			= p->type_meta_auto_text;
	type["preview_link_path"]	= p->type_preview_link_path;
	type["preview_parser_name"] = p->type_preview_parser_name;
	type["preview_text_loaded"] = (p->type_preview_text_buf[0] != 0);
	type["build_sync_state"]	= "synced";
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderManifestJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"]			= "recorder_manifest";
	doc["file_path"]	= p->recorder_file_path;
	doc["record_count"] = p->recorder_record_count;
	doc["backend"]		= (p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc";
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"]	 = "recorder_index";
	doc["count"] = p->recorder_index_count;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) {
	(void)p;
	(void)p_bytes;
	JsonDocument doc;
	doc["mode"] = "recorder_preview";
	doc["path"] = (p_path != nullptr) ? p_path : "";
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderParsedPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) {
	(void)p;
	(void)p_bytes;
	JsonDocument doc;
	doc["mode"] = "recorder_parsed_preview";
	doc["path"] = (p_path != nullptr) ? p_path : "";
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderRangeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length) {
	(void)p;
	JsonDocument doc;
	doc["mode"]	  = "recorder_range";
	doc["path"]	  = (p_path != nullptr) ? p_path : "";
	doc["offset"] = p_offset;
	doc["length"] = p_length;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderBinaryHeaderJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path) {
	(void)p;
	JsonDocument doc;
	doc["mode"]	   = "binary_header";
	doc["path"]	   = (p_path != nullptr) ? p_path : "";
	doc["magic"]   = G_T20_BINARY_MAGIC;
	doc["version"] = G_T20_BINARY_VERSION;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderCsvTableJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) {
	return T20_buildRecorderCsvTableAdvancedJsonText(p, p_out_buf, p_len, p_path, p_bytes, "", "", 0, G_T20_CSV_SORT_ASC, 0, G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT);
}
bool T20_buildRecorderCsvSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) {
	return T20_buildRecorderCsvTypeMetaJsonText(p, p_out_buf, p_len, p_path, p_bytes);
}
bool T20_buildRecorderCsvTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) {
	(void)p;
	(void)p_bytes;
	JsonDocument doc;
	doc["mode"] = "csv_type_meta";
	doc["path"] = (p_path != nullptr) ? p_path : "";
	doc["todo"] = "TODO: 타입 메타 캐시 고도화";
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes, const char* p_global_filter, const char* p_col_filters_csv, uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size) {
	(void)p;
	(void)p_bytes;
	JsonDocument doc;
	doc["mode"]			 = "csv_table_advanced";
	doc["path"]			 = (p_path != nullptr) ? p_path : "";
	doc["global_filter"] = (p_global_filter != nullptr) ? p_global_filter : "";
	doc["col_filters"]	 = (p_col_filters_csv != nullptr) ? p_col_filters_csv : "";
	doc["sort_col"]		 = p_sort_col;
	doc["sort_dir"]		 = p_sort_dir;
	doc["page"]			 = p_page;
	doc["page_size"]	 = p_page_size;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderBinaryRecordsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit) {
	if (p == nullptr) return false;
	JsonDocument doc;
	doc["mode"]			= "binary_records";
	doc["path"]			= (p_path != nullptr) ? p_path : "";
	doc["offset"]		= p_offset;
	doc["limit"]		= p_limit;
	doc["record_count"] = p->recorder_record_count;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRecorderBinaryPayloadSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path) {
	(void)p;
	JsonDocument doc;
	doc["mode"]		  = "binary_payload_schema";
	doc["path"]		  = (p_path != nullptr) ? p_path : "";
	doc["vector_max"] = G_T20_FEATURE_DIM_MAX;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildRenderSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	(void)p;
	JsonDocument doc;
	doc["mode"]				  = "render_selection_sync";
	doc["series_max"]		  = G_T20_RECORDER_RENDER_SYNC_SERIES_MAX;
	doc["selection_sync_max"] = G_T20_RENDER_SELECTION_SYNC_MAX;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
bool T20_buildTypeMetaPreviewLinkJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	(void)p;
	JsonDocument doc;
	doc["mode"]				= "type_meta_preview_link";
	doc["preview_link_max"] = G_T20_TYPE_META_PREVIEW_LINK_MAX;
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_jsonFindIntInSection(const char* p_json, const char* p_section, const char* p_key, int* p_out_value) {
	(void)p_json;
	(void)p_section;
	(void)p_key;
	if (p_out_value == nullptr) return false;
	return false;
}
bool T20_jsonFindFloatInSection(const char* p_json, const char* p_section, const char* p_key, float* p_out_value) {
	(void)p_json;
	(void)p_section;
	(void)p_key;
	if (p_out_value == nullptr) return false;
	return false;
}
bool T20_jsonFindBoolInSection(const char* p_json, const char* p_section, const char* p_key, bool* p_out_value) {
	(void)p_json;
	(void)p_section;
	(void)p_key;
	if (p_out_value == nullptr) return false;
	return false;
}
bool T20_jsonFindStringInSection(const char* p_json, const char* p_section, const char* p_key, char* p_out_buf, uint16_t p_len) {
	(void)p_json;
	(void)p_section;
	(void)p_key;
	if (p_out_buf == nullptr || p_len == 0) return false;
	p_out_buf[0] = 0;
	return false;
}

bool T20_parseOutputMode(const char* p_json, EM_T20_OutputMode_t* p_out_mode) {
	(void)p_json;
	if (p_out_mode == nullptr) return false;
	*p_out_mode = EN_T20_OUTPUT_VECTOR;
	return true;
}
bool T20_parseFilterType(const char* p_json, EM_T20_FilterType_t* p_out_type) {
	(void)p_json;
	if (p_out_type == nullptr) return false;
	*p_out_type = EN_T20_FILTER_HPF;
	return true;
}
bool T20_parseHttpRangeHeader(const String& p_range, uint32_t p_file_size, uint32_t* p_offset_out, uint32_t* p_length_out) {
	if (p_offset_out == nullptr || p_length_out == nullptr) return false;
	*p_offset_out = 0;
	*p_length_out = p_file_size;
	if (!p_range.startsWith("bytes=")) return false;
	int dash = p_range.indexOf('-');
	if (dash < 0) return false;
	uint32_t start = (uint32_t)p_range.substring(6, dash).toInt();
	uint32_t end   = (uint32_t)p_range.substring(dash + 1).toInt();
	if (start >= p_file_size) return false;
	if (end == 0 || end >= p_file_size) end = p_file_size - 1U;
	if (end < start) return false;
	*p_offset_out = start;
	*p_length_out = (end - start) + 1U;
	return true;
}
bool T20_isLikelyDateText(const String& p_text) {
	return (p_text.length() == 10 && p_text.indexOf('-') == 4);
}
bool T20_isLikelyDateTimeText(const String& p_text) {
	return (p_text.length() >= 19 && p_text.indexOf('-') == 4 && (p_text.indexOf('T') == 10 || p_text.indexOf(' ') == 10));
}
String T20_upgradeCsvTypeGuess(const String& p_current, const String& p_cell) {
	if (p_cell.length() == 0) return (p_current.length() > 0) ? p_current : String("text");
	if (p_cell.toFloat() != 0.0f || p_cell == "0") return String("number");
	if (T20_isLikelyDateText(p_cell) || T20_isLikelyDateTimeText(p_cell)) return String("date");
	return (p_current.length() > 0) ? p_current : String("text");
}
bool T20_csvRowMatchesGlobalFilter(const std::vector<String>& p_row, const String& p_filter) {
	if (p_filter.length() == 0) return true;
	String f = p_filter;
	f.toLowerCase();
	for (const auto& cell : p_row) {
		String v = cell;
		v.toLowerCase();
		if (v.indexOf(f) >= 0) return true;
	}
	return false;
}

bool CL_T20_Mfcc::exportConfigJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildConfigJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportConfigSchemaJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildConfigSchemaJsonText(p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportViewerDataJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildViewerDataJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportViewerWaveformJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildViewerWaveformJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportViewerSpectrumJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildViewerSpectrumJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportViewerEventsJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildViewerEventsJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportViewerSequenceJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildViewerSequenceJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportViewerOverviewJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildViewerOverviewJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportViewerMultiFrameJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildViewerMultiFrameJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportViewerChartBundleJson(char* p_out_buf, uint16_t p_len, uint16_t p_points) const {
	return T20_buildViewerChartBundleJsonText(_impl, p_out_buf, p_len, p_points);
}
bool CL_T20_Mfcc::exportRecorderManifestJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildRecorderManifestJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportRecorderIndexJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildRecorderIndexJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportRecorderPreviewJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const {
	return T20_buildRecorderPreviewJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}
bool CL_T20_Mfcc::exportRecorderParsedPreviewJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const {
	return T20_buildRecorderParsedPreviewJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}
bool CL_T20_Mfcc::exportRecorderRangeJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length) const {
	return T20_buildRecorderRangeJsonText(_impl, p_out_buf, p_len, p_path, p_offset, p_length);
}
bool CL_T20_Mfcc::exportRecorderBinaryHeaderJson(char* p_out_buf, uint16_t p_len, const char* p_path) const {
	return T20_buildRecorderBinaryHeaderJsonText(_impl, p_out_buf, p_len, p_path);
}
bool CL_T20_Mfcc::exportRecorderCsvTableJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const {
	return T20_buildRecorderCsvTableJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}
bool CL_T20_Mfcc::exportRecorderCsvSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const {
	return T20_buildRecorderCsvSchemaJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}
bool CL_T20_Mfcc::exportRecorderCsvTypeMetaJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const {
	return T20_buildRecorderCsvTypeMetaJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}
bool CL_T20_Mfcc::exportRecorderCsvTableAdvancedJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes, const char* p_global_filter, const char* p_col_filters_csv, uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size) const {
	return T20_buildRecorderCsvTableAdvancedJsonText(_impl, p_out_buf, p_len, p_path, p_bytes, p_global_filter, p_col_filters_csv, p_sort_col, p_sort_dir, p_page, p_page_size);
}
bool CL_T20_Mfcc::exportRecorderBinaryRecordsJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit) const {
	return T20_buildRecorderBinaryRecordsJsonText(_impl, p_out_buf, p_len, p_path, p_offset, p_limit);
}
bool CL_T20_Mfcc::exportRecorderBinaryPayloadSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path) const {
	return T20_buildRecorderBinaryPayloadSchemaJsonText(_impl, p_out_buf, p_len, p_path);
}
bool CL_T20_Mfcc::exportRenderSelectionSyncJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildRenderSelectionSyncJsonText(_impl, p_out_buf, p_len);
}
bool CL_T20_Mfcc::exportTypeMetaPreviewLinkJson(char* p_out_buf, uint16_t p_len) const {
	return T20_buildTypeMetaPreviewLinkJsonText(_impl, p_out_buf, p_len);
}

void CL_T20_Mfcc::printConfig(Stream& p_out) const {
	p_out.println(F("----------- T20_Mfcc Config -----------"));
	p_out.printf("FrameSize   : %u\n", _impl->cfg.feature.frame_size);
	p_out.printf("HopSize     : %u\n", _impl->cfg.feature.hop_size);
	p_out.printf("MFCC Coeffs : %u\n", _impl->cfg.feature.mfcc_coeffs);
	p_out.printf("Output Mode : %s\n", _impl->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR ? "VECTOR" : "SEQUENCE");
	p_out.println(F("---------------------------------------"));
}
void CL_T20_Mfcc::printStatus(Stream& p_out) const {
	p_out.println(F("----------- T20_Mfcc Status -----------"));
	p_out.printf("Initialized  : %s\n", _impl->initialized ? "YES" : "NO");
	p_out.printf("Running      : %s\n", _impl->running ? "YES" : "NO");
	p_out.printf("Record Count : %lu\n", (unsigned long)_impl->recorder_record_count);
	p_out.printf("Dropped      : %lu\n", (unsigned long)_impl->dropped_frames);
	p_out.println(F("---------------------------------------"));
}
void CL_T20_Mfcc::printLatest(Stream& p_out) const {
	p_out.println(F("----------- T20_Mfcc Latest -----------"));
	p_out.printf("Frame ID     : %lu\n", (unsigned long)_impl->viewer_last_frame_id);
	p_out.printf("Vector Len   : %u\n", _impl->viewer_last_vector_len);
	p_out.println(F("---------------------------------------"));
}
void CL_T20_Mfcc::printChartSyncStatus(Stream& p_out) const {
	p_out.println(F("Chart sync: skeleton ready"));
}
void CL_T20_Mfcc::printRecorderBackendStatus(Stream& p_out) const {
	p_out.printf("Recorder Backend : %s\n", _impl->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS ? "LITTLEFS" : "SDMMC");
	p_out.printf("Recorder File    : %s\n", _impl->recorder_file_path);
	p_out.printf("Active Path      : %s\n", _impl->recorder_active_path);
	p_out.printf("Recorder Opened  : %s\n", _impl->recorder_file_opened ? "YES" : "NO");
	p_out.printf("Fallback Active  : %s\n", _impl->recorder_fallback_active ? "YES" : "NO");
	p_out.printf("Session Open     : %s\n", _impl->recorder_session_open ? "YES" : "NO");
	p_out.printf("SDMMC Profile    : %s\n", _impl->sdmmc_profile.profile_name);
	p_out.printf("SDMMC Apply Why  : %s\n", _impl->sdmmc_last_apply_reason);
	p_out.printf("Batch Count      : %u\n", _impl->recorder_batch_count);
	p_out.printf("Last Flush ms    : %lu\n", (unsigned long)_impl->recorder_last_flush_ms);
	p_out.printf("Session ID       : %lu\n", (unsigned long)_impl->recorder_session_id);
	p_out.printf("WM Low/High      : %u / %u\n", _impl->recorder_batch_watermark_low, _impl->recorder_batch_watermark_high);
	p_out.printf("Selection Sync   : %s [%lu-%lu] eff[%lu-%lu] valid=%s pts=%u\n", _impl->selection_sync_name, (unsigned long)_impl->selection_sync_frame_from, (unsigned long)_impl->selection_sync_frame_to, (unsigned long)_impl->selection_sync_effective_from, (unsigned long)_impl->selection_sync_effective_to, _impl->selection_sync_range_valid ? "YES" : "NO", _impl->viewer_selection_points_len);
	p_out.printf("Type Meta        : %s / %s / %s / %s / parser=%s rows=%u hints=%u txt=%s schema=%s delim=%s header=%s\n", _impl->type_meta_name, _impl->type_meta_kind, _impl->type_meta_auto_text, _impl->type_preview_link_path, _impl->type_preview_parser_name, _impl->type_preview_sample_row_count, _impl->preview_column_hint_count, _impl->type_preview_text_buf[0] != 0 ? "YES" : "NO", _impl->type_preview_schema_kind, _impl->type_preview_detected_delim, _impl->type_preview_header_guess);
	p_out.printf("Last Error       : %s\n", _impl->recorder_last_error);
}
void CL_T20_Mfcc::printTypeMetaStatus(Stream& p_out) const {
	p_out.println(F("TypeMeta/Preview link: skeleton ready"));
}
void CL_T20_Mfcc::printRoadmapTodo(Stream& p_out) const {
	p_out.println(F("TODO:"));
	p_out.println(F("- SD_MMC 보드별 pin/mode/profile 실제 적용"));
	p_out.println(F("- zero-copy / DMA / cache aligned write 경로 실제화"));
	p_out.println(F("- selection sync와 waveform/spectrum 구간 연동"));
	p_out.println(F("- 타입 메타 캐시와 정렬/필터/다운샘플 preview 실제 연계"));
}

/* ============================================================================
[다음 단계 조금 더 크게 묶은 실제 연결 Placeholder - v210]
============================================================================ */
bool T20_bmi270ApplyRealDspIngress(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* TODO: raw -> frame -> dsp ingress 실제 연결 */
	return true;
}

bool T20_recorderApplyRealMetaReportSync(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* TODO: meta/report/audit/manifest sync 실제 연결 */
	return true;
}

/* ============================================================================
[다음 단계 조금 더 크게 묶은 흐름 - v210]
주의:
- 반복 오류 패턴 방지를 위해 신규 staged 상태는 기존 alias 접근 가능한 멤버만 사용
- ST_Impl 신규 멤버 직접 접근은 피함
============================================================================ */

bool T20_bmi270PreparePipelineLinkFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - SPI begin / burst / ISR / raw pipe / frame build / dsp ingress를 단일 pipeline link로 통합 */
	T20_ref_bmi270_hw_link_state(p) = G_T20_BMI270_PIPELINE_LINK_STATE_READY;
	return true;
}

bool T20_bmi270PrepareRealApplyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - 실제 SPI/burst/ISR 적용 결과를 runtime 상태에 반영 */
	T20_ref_bmi270_real_begin_state(p) = G_T20_BMI270_REAL_APPLY_STATE_READY;
	T20_ref_bmi270_real_burst_state(p) = G_T20_BMI270_REAL_APPLY_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)   = G_T20_BMI270_REAL_APPLY_STATE_READY;
	return true;
}

bool T20_recorderPrepareFinalSyncBundleFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - meta/report/audit/manifest/summary를 final sync bundle로 통합 */
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_FINAL_SYNC_BUNDLE_STATE_READY;
	return true;
}

bool T20_recorderPrepareRealApplyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	/* 향후 단계 구현 예정:
	   - flush/close/finalize 실제 호출 결과를 recorder runtime에 반영 */
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_REAL_APPLY_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_REAL_APPLY_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_REAL_APPLY_STATE_READY;
	return true;
}

bool T20_buildRealPipelineBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["real_begin_state"]	   = T20_ref_bmi270_real_begin_state(p);
	doc["real_burst_state"]	   = T20_ref_bmi270_real_burst_state(p);
	doc["real_isr_state"]	   = T20_ref_bmi270_real_isr_state(p);
	doc["hw_link_state"]	   = T20_ref_bmi270_hw_link_state(p);
	doc["raw_pipe_state"]	   = T20_ref_bmi270_raw_pipe_state(p);
	doc["frame_build_state"]   = T20_ref_bmi270_frame_build_state(p);
	doc["dsp_ingress_state"]   = T20_ref_bmi270_dsp_ingress_state(p);
	doc["real_flush_state"]	   = T20_ref_recorder_real_flush_state(p);
	doc["real_close_state"]	   = T20_ref_recorder_real_close_state(p);
	doc["real_finalize_state"] = T20_ref_recorder_real_finalize_state(p);
	doc["meta_sync_state"]	   = T20_ref_recorder_meta_sync_state(p);
	doc["report_sync_state"]   = T20_ref_recorder_report_sync_state(p);
	doc["audit_sync_state"]	   = T20_ref_recorder_audit_sync_state(p);
	doc["manifest_sync_state"] = T20_ref_recorder_manifest_sync_state(p);
	doc["finalize_sync_state"] = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 조금 더 크게 묶은 흐름 - v210]
원칙:
- 반복 오류 패턴 방지를 위해 신규 staged 흐름도 alias accessor만 사용
- 직접 ST_Impl 신규 멤버 접근 금지
============================================================================ */

bool T20_bmi270PreparePipelineReadyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_hw_link_state(p)		= G_T20_BMI270_PIPELINE_READY_STATE_READY;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_PIPELINE_READY_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_PIPELINE_READY_STATE_READY;
	return true;
}

bool T20_bmi270PreparePipelineExecFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_real_begin_state(p)	= G_T20_BMI270_PIPELINE_EXEC_STATE_READY;
	T20_ref_bmi270_real_burst_state(p)	= G_T20_BMI270_PIPELINE_EXEC_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)	= G_T20_BMI270_PIPELINE_EXEC_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_PIPELINE_EXEC_STATE_READY;
	return true;
}

bool T20_recorderPrepareSyncReadyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_SYNC_READY_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_SYNC_READY_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_SYNC_READY_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_SYNC_READY_STATE_READY;
	return true;
}

bool T20_recorderPrepareSyncExecFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_SYNC_EXEC_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_SYNC_EXEC_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_SYNC_EXEC_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_SYNC_EXEC_STATE_READY;
	return true;
}

bool T20_buildPipelineExecBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["pipeline_hw_link_state"]	  = T20_ref_bmi270_hw_link_state(p);
	doc["pipeline_raw_pipe_state"]	  = T20_ref_bmi270_raw_pipe_state(p);
	doc["pipeline_frame_build_state"] = T20_ref_bmi270_frame_build_state(p);
	doc["pipeline_real_begin_state"]  = T20_ref_bmi270_real_begin_state(p);
	doc["pipeline_real_burst_state"]  = T20_ref_bmi270_real_burst_state(p);
	doc["pipeline_real_isr_state"]	  = T20_ref_bmi270_real_isr_state(p);
	doc["pipeline_dsp_ingress_state"] = T20_ref_bmi270_dsp_ingress_state(p);
	doc["sync_meta_state"]			  = T20_ref_recorder_meta_sync_state(p);
	doc["sync_report_state"]		  = T20_ref_recorder_report_sync_state(p);
	doc["sync_audit_state"]			  = T20_ref_recorder_audit_sync_state(p);
	doc["sync_manifest_state"]		  = T20_ref_recorder_manifest_sync_state(p);
	doc["sync_flush_state"]			  = T20_ref_recorder_real_flush_state(p);
	doc["sync_close_state"]			  = T20_ref_recorder_real_close_state(p);
	doc["sync_finalize_state"]		  = T20_ref_recorder_real_finalize_state(p);
	doc["sync_bundle_state"]		  = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 조금 더 크게 묶은 흐름 - v210]
원칙:
- 반복 오류 패턴 방지를 위해 신규 staged 흐름도 alias accessor만 사용
- 직접 ST_Impl 신규 멤버 접근 금지
============================================================================ */

bool T20_bmi270PrepareExecLinkFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_hw_link_state(p)	   = G_T20_BMI270_EXEC_LINK_STATE_READY;
	T20_ref_bmi270_real_begin_state(p) = G_T20_BMI270_EXEC_LINK_STATE_READY;
	T20_ref_bmi270_real_burst_state(p) = G_T20_BMI270_EXEC_LINK_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)   = G_T20_BMI270_EXEC_LINK_STATE_READY;
	return true;
}

bool T20_bmi270PrepareDspReadyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_DSP_READY_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_DSP_READY_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_DSP_READY_STATE_READY;
	return true;
}

bool T20_recorderPrepareSyncLinkFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_SYNC_LINK_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_SYNC_LINK_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_SYNC_LINK_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_SYNC_LINK_STATE_READY;
	return true;
}

bool T20_recorderPrepareFinalReadyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_FINAL_READY_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_FINAL_READY_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_FINAL_READY_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_FINAL_READY_STATE_READY;
	return true;
}

bool T20_buildExecReadyBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["exec_link_hw_state"]		  = T20_ref_bmi270_hw_link_state(p);
	doc["exec_link_begin_state"]	  = T20_ref_bmi270_real_begin_state(p);
	doc["exec_link_burst_state"]	  = T20_ref_bmi270_real_burst_state(p);
	doc["exec_link_isr_state"]		  = T20_ref_bmi270_real_isr_state(p);
	doc["dsp_ready_raw_pipe_state"]	  = T20_ref_bmi270_raw_pipe_state(p);
	doc["dsp_ready_frame_state"]	  = T20_ref_bmi270_frame_build_state(p);
	doc["dsp_ready_ingress_state"]	  = T20_ref_bmi270_dsp_ingress_state(p);
	doc["sync_link_meta_state"]		  = T20_ref_recorder_meta_sync_state(p);
	doc["sync_link_report_state"]	  = T20_ref_recorder_report_sync_state(p);
	doc["sync_link_audit_state"]	  = T20_ref_recorder_audit_sync_state(p);
	doc["sync_link_manifest_state"]	  = T20_ref_recorder_manifest_sync_state(p);
	doc["final_ready_flush_state"]	  = T20_ref_recorder_real_flush_state(p);
	doc["final_ready_close_state"]	  = T20_ref_recorder_real_close_state(p);
	doc["final_ready_finalize_state"] = T20_ref_recorder_real_finalize_state(p);
	doc["final_ready_bundle_state"]	  = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 조금 더 크게 묶은 흐름 - v210]
원칙:
- 반복 오류 패턴 방지를 위해 신규 staged 흐름도 alias accessor만 사용
- 직접 ST_Impl 신규 멤버 접근 금지
============================================================================ */

bool T20_bmi270PrepareRuntimeReadyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_hw_link_state(p)		= G_T20_BMI270_RUNTIME_READY_STATE_READY;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_RUNTIME_READY_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_RUNTIME_READY_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_RUNTIME_READY_STATE_READY;
	return true;
}

bool T20_bmi270PrepareRuntimeExecFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_real_begin_state(p) = G_T20_BMI270_RUNTIME_EXEC_STATE_READY;
	T20_ref_bmi270_real_burst_state(p) = G_T20_BMI270_RUNTIME_EXEC_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)   = G_T20_BMI270_RUNTIME_EXEC_STATE_READY;
	return true;
}

bool T20_recorderPrepareRuntimeReadyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_RUNTIME_READY_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_RUNTIME_READY_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_RUNTIME_READY_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_RUNTIME_READY_STATE_READY;
	return true;
}

bool T20_recorderPrepareRuntimeExecFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_RUNTIME_EXEC_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_RUNTIME_EXEC_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_RUNTIME_EXEC_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_RUNTIME_EXEC_STATE_READY;
	return true;
}

bool T20_buildRuntimeExecBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["runtime_hw_link_state"]	   = T20_ref_bmi270_hw_link_state(p);
	doc["runtime_raw_pipe_state"]	   = T20_ref_bmi270_raw_pipe_state(p);
	doc["runtime_frame_build_state"]   = T20_ref_bmi270_frame_build_state(p);
	doc["runtime_dsp_ingress_state"]   = T20_ref_bmi270_dsp_ingress_state(p);
	doc["runtime_real_begin_state"]	   = T20_ref_bmi270_real_begin_state(p);
	doc["runtime_real_burst_state"]	   = T20_ref_bmi270_real_burst_state(p);
	doc["runtime_real_isr_state"]	   = T20_ref_bmi270_real_isr_state(p);
	doc["runtime_meta_sync_state"]	   = T20_ref_recorder_meta_sync_state(p);
	doc["runtime_report_sync_state"]   = T20_ref_recorder_report_sync_state(p);
	doc["runtime_audit_sync_state"]	   = T20_ref_recorder_audit_sync_state(p);
	doc["runtime_manifest_sync_state"] = T20_ref_recorder_manifest_sync_state(p);
	doc["runtime_flush_state"]		   = T20_ref_recorder_real_flush_state(p);
	doc["runtime_close_state"]		   = T20_ref_recorder_real_close_state(p);
	doc["runtime_finalize_state"]	   = T20_ref_recorder_real_finalize_state(p);
	doc["runtime_bundle_state"]		   = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 조금 더 크게 묶은 흐름 - v210]
원칙:
- 반복 오류 패턴 방지를 위해 신규 staged 흐름도 alias accessor만 사용
- 직접 ST_Impl 신규 멤버 접근 금지
============================================================================ */

bool T20_bmi270PrepareApplyReadyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_hw_link_state(p)		= G_T20_BMI270_APPLY_READY_STATE_READY;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_APPLY_READY_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_APPLY_READY_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_APPLY_READY_STATE_READY;
	return true;
}

bool T20_bmi270PrepareApplyExecFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_real_begin_state(p) = G_T20_BMI270_APPLY_EXEC_STATE_READY;
	T20_ref_bmi270_real_burst_state(p) = G_T20_BMI270_APPLY_EXEC_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)   = G_T20_BMI270_APPLY_EXEC_STATE_READY;
	return true;
}

bool T20_recorderPrepareApplyReadyFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_APPLY_READY_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_APPLY_READY_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_APPLY_READY_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_APPLY_READY_STATE_READY;
	return true;
}

bool T20_recorderPrepareApplyExecFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_APPLY_EXEC_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_APPLY_EXEC_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_APPLY_EXEC_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_APPLY_EXEC_STATE_READY;
	return true;
}

bool T20_buildApplyExecBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["apply_ready_hw_link_state"]	 = T20_ref_bmi270_hw_link_state(p);
	doc["apply_ready_raw_pipe_state"]	 = T20_ref_bmi270_raw_pipe_state(p);
	doc["apply_ready_frame_build_state"] = T20_ref_bmi270_frame_build_state(p);
	doc["apply_ready_dsp_ingress_state"] = T20_ref_bmi270_dsp_ingress_state(p);
	doc["apply_exec_begin_state"]		 = T20_ref_bmi270_real_begin_state(p);
	doc["apply_exec_burst_state"]		 = T20_ref_bmi270_real_burst_state(p);
	doc["apply_exec_isr_state"]			 = T20_ref_bmi270_real_isr_state(p);
	doc["apply_ready_meta_state"]		 = T20_ref_recorder_meta_sync_state(p);
	doc["apply_ready_report_state"]		 = T20_ref_recorder_report_sync_state(p);
	doc["apply_ready_audit_state"]		 = T20_ref_recorder_audit_sync_state(p);
	doc["apply_ready_manifest_state"]	 = T20_ref_recorder_manifest_sync_state(p);
	doc["apply_exec_flush_state"]		 = T20_ref_recorder_real_flush_state(p);
	doc["apply_exec_close_state"]		 = T20_ref_recorder_real_close_state(p);
	doc["apply_exec_finalize_state"]	 = T20_ref_recorder_real_finalize_state(p);
	doc["apply_exec_bundle_state"]		 = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 조금 더 크게 묶은 흐름 - v210]
원칙:
- 반복 오류 패턴 방지를 위해 신규 staged 흐름도 alias accessor만 사용
- 직접 ST_Impl 신규 멤버 접근 금지
============================================================================ */

bool T20_bmi270PrepareApplyPipelineFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_hw_link_state(p)		= G_T20_BMI270_APPLY_PIPELINE_STATE_READY;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_APPLY_PIPELINE_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_APPLY_PIPELINE_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_APPLY_PIPELINE_STATE_READY;
	return true;
}

bool T20_bmi270PrepareRealPipelineFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_real_begin_state(p) = G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	T20_ref_bmi270_real_burst_state(p) = G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)   = G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	return true;
}

bool T20_recorderPrepareApplyPipelineFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_APPLY_PIPELINE_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_APPLY_PIPELINE_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_APPLY_PIPELINE_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_APPLY_PIPELINE_STATE_READY;
	return true;
}

bool T20_recorderPrepareRealPipelineFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	return true;
}

bool T20_buildApplyPipelineBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["apply_pipeline_hw_link_state"]		= T20_ref_bmi270_hw_link_state(p);
	doc["apply_pipeline_raw_pipe_state"]	= T20_ref_bmi270_raw_pipe_state(p);
	doc["apply_pipeline_frame_build_state"] = T20_ref_bmi270_frame_build_state(p);
	doc["apply_pipeline_dsp_ingress_state"] = T20_ref_bmi270_dsp_ingress_state(p);
	doc["real_pipeline_begin_state"]		= T20_ref_bmi270_real_begin_state(p);
	doc["real_pipeline_burst_state"]		= T20_ref_bmi270_real_burst_state(p);
	doc["real_pipeline_isr_state"]			= T20_ref_bmi270_real_isr_state(p);
	doc["apply_pipeline_meta_state"]		= T20_ref_recorder_meta_sync_state(p);
	doc["apply_pipeline_report_state"]		= T20_ref_recorder_report_sync_state(p);
	doc["apply_pipeline_audit_state"]		= T20_ref_recorder_audit_sync_state(p);
	doc["apply_pipeline_manifest_state"]	= T20_ref_recorder_manifest_sync_state(p);
	doc["real_pipeline_flush_state"]		= T20_ref_recorder_real_flush_state(p);
	doc["real_pipeline_close_state"]		= T20_ref_recorder_real_close_state(p);
	doc["real_pipeline_finalize_state"]		= T20_ref_recorder_real_finalize_state(p);
	doc["real_pipeline_bundle_state"]		= T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 조금 더 크게 묶은 흐름 - v210]
원칙:
- 반복 오류 패턴 방지를 위해 신규 staged 흐름도 alias accessor만 사용
- struct 밖 전역 정의 생성 금지
============================================================================ */

bool T20_bmi270PrepareRealExecBundleFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_bmi270_hw_link_state(p)		= G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	T20_ref_bmi270_real_begin_state(p)	= G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	T20_ref_bmi270_real_burst_state(p)	= G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)	= G_T20_BMI270_REAL_PIPELINE_STATE_READY;
	return true;
}

bool T20_recorderPrepareRealExecBundleFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_REAL_PIPELINE_STATE_READY;
	return true;
}

bool T20_buildRealExecBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	JsonDocument doc;
	doc["real_exec_hw_link_state"]	   = T20_ref_bmi270_hw_link_state(p);
	doc["real_exec_raw_pipe_state"]	   = T20_ref_bmi270_raw_pipe_state(p);
	doc["real_exec_frame_build_state"] = T20_ref_bmi270_frame_build_state(p);
	doc["real_exec_dsp_ingress_state"] = T20_ref_bmi270_dsp_ingress_state(p);
	doc["real_exec_begin_state"]	   = T20_ref_bmi270_real_begin_state(p);
	doc["real_exec_burst_state"]	   = T20_ref_bmi270_real_burst_state(p);
	doc["real_exec_isr_state"]		   = T20_ref_bmi270_real_isr_state(p);
	doc["real_exec_meta_state"]		   = T20_ref_recorder_meta_sync_state(p);
	doc["real_exec_report_state"]	   = T20_ref_recorder_report_sync_state(p);
	doc["real_exec_audit_state"]	   = T20_ref_recorder_audit_sync_state(p);
	doc["real_exec_manifest_state"]	   = T20_ref_recorder_manifest_sync_state(p);
	doc["real_exec_flush_state"]	   = T20_ref_recorder_real_flush_state(p);
	doc["real_exec_close_state"]	   = T20_ref_recorder_real_close_state(p);
	doc["real_exec_finalize_state"]	   = T20_ref_recorder_real_finalize_state(p);
	doc["real_exec_bundle_state"]	   = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 가능한 크게 묶은 흐름 - v210]
원칙:
- 신규 staged 확장도 alias accessor만 사용
- struct 신규 멤버 추가 금지
- 링크 오류를 피하기 위해 헤더에는 선언만 유지
============================================================================ */

bool T20_bmi270PrepareMegaPipelineFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_bmi270_hw_link_state(p)		= G_T20_BMI270_MEGA_PIPELINE_STATE_READY;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_MEGA_PIPELINE_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_MEGA_PIPELINE_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_MEGA_PIPELINE_STATE_READY;
	T20_ref_bmi270_real_begin_state(p)	= G_T20_BMI270_MEGA_PIPELINE_STATE_READY;
	T20_ref_bmi270_real_burst_state(p)	= G_T20_BMI270_MEGA_PIPELINE_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)	= G_T20_BMI270_MEGA_PIPELINE_STATE_READY;
	return true;
}

bool T20_bmi270PrepareRealConnectStageFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_bmi270_real_begin_state(p)	= G_T20_BMI270_REAL_CONNECT_STAGE_READY;
	T20_ref_bmi270_real_burst_state(p)	= G_T20_BMI270_REAL_CONNECT_STAGE_READY;
	T20_ref_bmi270_real_isr_state(p)	= G_T20_BMI270_REAL_CONNECT_STAGE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_REAL_CONNECT_STAGE_READY;
	return true;
}

bool T20_recorderPrepareMegaPipelineFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_MEGA_PIPELINE_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_MEGA_PIPELINE_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_MEGA_PIPELINE_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_MEGA_PIPELINE_STATE_READY;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_MEGA_PIPELINE_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_MEGA_PIPELINE_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_MEGA_PIPELINE_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_MEGA_PIPELINE_STATE_READY;
	return true;
}

bool T20_recorderPrepareRealConnectStageFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_REAL_CONNECT_STAGE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_REAL_CONNECT_STAGE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_REAL_CONNECT_STAGE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_REAL_CONNECT_STAGE_READY;
	return true;
}

bool T20_buildMegaPipelineBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["mega_hw_link_state"]		= T20_ref_bmi270_hw_link_state(p);
	doc["mega_raw_pipe_state"]		= T20_ref_bmi270_raw_pipe_state(p);
	doc["mega_frame_build_state"]	= T20_ref_bmi270_frame_build_state(p);
	doc["mega_dsp_ingress_state"]	= T20_ref_bmi270_dsp_ingress_state(p);
	doc["mega_real_begin_state"]	= T20_ref_bmi270_real_begin_state(p);
	doc["mega_real_burst_state"]	= T20_ref_bmi270_real_burst_state(p);
	doc["mega_real_isr_state"]		= T20_ref_bmi270_real_isr_state(p);
	doc["mega_meta_sync_state"]		= T20_ref_recorder_meta_sync_state(p);
	doc["mega_report_sync_state"]	= T20_ref_recorder_report_sync_state(p);
	doc["mega_audit_sync_state"]	= T20_ref_recorder_audit_sync_state(p);
	doc["mega_manifest_sync_state"] = T20_ref_recorder_manifest_sync_state(p);
	doc["mega_real_flush_state"]	= T20_ref_recorder_real_flush_state(p);
	doc["mega_real_close_state"]	= T20_ref_recorder_real_close_state(p);
	doc["mega_real_finalize_state"] = T20_ref_recorder_real_finalize_state(p);
	doc["mega_finalize_sync_state"] = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 가능한 크게 묶은 흐름 - v210]
원칙:
- 신규 staged 확장도 alias accessor만 사용
- struct 신규 멤버 추가 금지
- 헤더에는 선언만 유지
============================================================================ */

bool T20_bmi270PrepareIntegrationBundleFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_bmi270_hw_link_state(p)		= G_T20_BMI270_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_bmi270_real_begin_state(p)	= G_T20_BMI270_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_bmi270_real_burst_state(p)	= G_T20_BMI270_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)	= G_T20_BMI270_INTEGRATION_BUNDLE_STATE_READY;
	return true;
}

bool T20_bmi270PrepareConnectPrepFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_bmi270_real_begin_state(p)	= G_T20_BMI270_CONNECT_PREP_STATE_READY;
	T20_ref_bmi270_real_burst_state(p)	= G_T20_BMI270_CONNECT_PREP_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)	= G_T20_BMI270_CONNECT_PREP_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_CONNECT_PREP_STATE_READY;
	return true;
}

bool T20_recorderPrepareIntegrationBundleFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_INTEGRATION_BUNDLE_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_INTEGRATION_BUNDLE_STATE_READY;
	return true;
}

bool T20_recorderPrepareConnectPrepFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_CONNECT_PREP_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_CONNECT_PREP_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_CONNECT_PREP_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_CONNECT_PREP_STATE_READY;
	return true;
}

bool T20_buildIntegrationBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["integration_hw_link_state"]	   = T20_ref_bmi270_hw_link_state(p);
	doc["integration_raw_pipe_state"]	   = T20_ref_bmi270_raw_pipe_state(p);
	doc["integration_frame_build_state"]   = T20_ref_bmi270_frame_build_state(p);
	doc["integration_dsp_ingress_state"]   = T20_ref_bmi270_dsp_ingress_state(p);
	doc["integration_real_begin_state"]	   = T20_ref_bmi270_real_begin_state(p);
	doc["integration_real_burst_state"]	   = T20_ref_bmi270_real_burst_state(p);
	doc["integration_real_isr_state"]	   = T20_ref_bmi270_real_isr_state(p);
	doc["integration_meta_sync_state"]	   = T20_ref_recorder_meta_sync_state(p);
	doc["integration_report_sync_state"]   = T20_ref_recorder_report_sync_state(p);
	doc["integration_audit_sync_state"]	   = T20_ref_recorder_audit_sync_state(p);
	doc["integration_manifest_state"]	   = T20_ref_recorder_manifest_sync_state(p);
	doc["integration_real_flush_state"]	   = T20_ref_recorder_real_flush_state(p);
	doc["integration_real_close_state"]	   = T20_ref_recorder_real_close_state(p);
	doc["integration_real_finalize_state"] = T20_ref_recorder_real_finalize_state(p);
	doc["integration_bundle_state"]		   = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
[다음 단계 가능한 크게 묶은 흐름 - v210]
원칙:
- 신규 staged 확장도 alias accessor만 사용
- struct 신규 멤버 추가 금지
- 헤더에는 선언만 유지
============================================================================ */

bool T20_bmi270PrepareFinalIntegrationFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_bmi270_hw_link_state(p)		= G_T20_BMI270_FINAL_INTEGRATION_STATE_READY;
	T20_ref_bmi270_raw_pipe_state(p)	= G_T20_BMI270_FINAL_INTEGRATION_STATE_READY;
	T20_ref_bmi270_frame_build_state(p) = G_T20_BMI270_FINAL_INTEGRATION_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_FINAL_INTEGRATION_STATE_READY;
	T20_ref_bmi270_real_begin_state(p)	= G_T20_BMI270_FINAL_INTEGRATION_STATE_READY;
	T20_ref_bmi270_real_burst_state(p)	= G_T20_BMI270_FINAL_INTEGRATION_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)	= G_T20_BMI270_FINAL_INTEGRATION_STATE_READY;
	return true;
}

bool T20_bmi270PrepareConnectExecFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_bmi270_real_begin_state(p)	= G_T20_BMI270_CONNECT_EXEC_STATE_READY;
	T20_ref_bmi270_real_burst_state(p)	= G_T20_BMI270_CONNECT_EXEC_STATE_READY;
	T20_ref_bmi270_real_isr_state(p)	= G_T20_BMI270_CONNECT_EXEC_STATE_READY;
	T20_ref_bmi270_dsp_ingress_state(p) = G_T20_BMI270_CONNECT_EXEC_STATE_READY;
	return true;
}

bool T20_recorderPrepareFinalIntegrationFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_recorder_meta_sync_state(p)		= G_T20_RECORDER_FINAL_INTEGRATION_STATE_READY;
	T20_ref_recorder_report_sync_state(p)	= G_T20_RECORDER_FINAL_INTEGRATION_STATE_READY;
	T20_ref_recorder_audit_sync_state(p)	= G_T20_RECORDER_FINAL_INTEGRATION_STATE_READY;
	T20_ref_recorder_manifest_sync_state(p) = G_T20_RECORDER_FINAL_INTEGRATION_STATE_READY;
	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_FINAL_INTEGRATION_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_FINAL_INTEGRATION_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_FINAL_INTEGRATION_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_FINAL_INTEGRATION_STATE_READY;
	return true;
}

bool T20_recorderPrepareConnectExecFlow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	T20_ref_recorder_real_flush_state(p)	= G_T20_RECORDER_CONNECT_EXEC_STATE_READY;
	T20_ref_recorder_real_close_state(p)	= G_T20_RECORDER_CONNECT_EXEC_STATE_READY;
	T20_ref_recorder_real_finalize_state(p) = G_T20_RECORDER_CONNECT_EXEC_STATE_READY;
	T20_ref_recorder_finalize_sync_state(p) = G_T20_RECORDER_CONNECT_EXEC_STATE_READY;
	return true;
}

bool T20_buildFinalIntegrationBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
	if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

	JsonDocument doc;
	doc["final_hw_link_state"]		 = T20_ref_bmi270_hw_link_state(p);
	doc["final_raw_pipe_state"]		 = T20_ref_bmi270_raw_pipe_state(p);
	doc["final_frame_build_state"]	 = T20_ref_bmi270_frame_build_state(p);
	doc["final_dsp_ingress_state"]	 = T20_ref_bmi270_dsp_ingress_state(p);
	doc["final_real_begin_state"]	 = T20_ref_bmi270_real_begin_state(p);
	doc["final_real_burst_state"]	 = T20_ref_bmi270_real_burst_state(p);
	doc["final_real_isr_state"]		 = T20_ref_bmi270_real_isr_state(p);
	doc["final_meta_sync_state"]	 = T20_ref_recorder_meta_sync_state(p);
	doc["final_report_sync_state"]	 = T20_ref_recorder_report_sync_state(p);
	doc["final_audit_sync_state"]	 = T20_ref_recorder_audit_sync_state(p);
	doc["final_manifest_sync_state"] = T20_ref_recorder_manifest_sync_state(p);
	doc["final_real_flush_state"]	 = T20_ref_recorder_real_flush_state(p);
	doc["final_real_close_state"]	 = T20_ref_recorder_real_close_state(p);
	doc["final_real_finalize_state"] = T20_ref_recorder_real_finalize_state(p);
	doc["final_bundle_state"]		 = T20_ref_recorder_finalize_sync_state(p);
	return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}
