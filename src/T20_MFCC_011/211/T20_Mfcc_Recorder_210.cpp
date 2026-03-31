/* ============================================================================
[잔여 구현 계획 재점검 - Recorder v210]

현재 recorder는 finalize/sync/manifest/audit/report 계층이 충분히 준비되어 있다.
이제부터의 최우선은 아래를 실제 코드로 치환하는 것이다.

1. flush 실제 호출
2. close 실제 호출
3. finalize 실제 호출
4. meta/report/audit/manifest/summary 실제 저장/동기화
5. LittleFS / SD 경로별 실제 저장 연결

구현 원칙
- direct member access 대신 alias accessor 우선
- 준비 단계 추가보다 실제 파일 처리 코드 우선
- 성공/실패 상태가 JSON/Web에 반영되게 유지

반복 오류 주의
- 헤더 stray global definition 금지
- multiple definition 재발 점검
- prepare 체인만 늘고 실제 저장 코드가 비어 있지 않게 점검
============================================================================ */

/* ============================================================================
[반복 오류 패턴 점검 체크리스트]
1. 신규 staged 상태를 직접 멤버로 쓰기 전에 ST_Impl 존재 여부 점검
2. 없으면 core alias accessor 사용
3. prepare 체인 추가 시 DONE 반영 지점도 같이 점검
============================================================================ */

/* ============================================================================
[향후 단계 구현 예정 정리 - Recorder 추가]
1. finalize bundle(json/file) 생성
2. publish/audit 결과를 export/report와 교차검증
3. 저장 순서 고정: summary -> manifest -> index -> artifact -> meta -> archive -> package -> export -> recover -> delivery -> report -> publish -> audit
============================================================================ */

/* ============================================================================
[향후 단계 구현 예정 정리 - Recorder]
1. raw/event/feature/config/metadata 분리 저장
2. binary finalize/recover 강화
3. session summary/manifest 정리
4. rotate/prune 실제 파일 처리
============================================================================ */

#include "T20_Mfcc_Inter_210.h"

bool T20_recorderBegin(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - 세션 begin 시 별도 manifest/config snapshot 저장
	   - raw/event/feature 파일 다중 경로 분리
	   - binary header rewrite/finalize 강화 */
	p->recorder_enabled = true;
	return T20_recorderOpenSession(p, p->recorder_session_name);
}

bool T20_recorderEnd(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 향후 단계 구현 예정:
	   - binary finalize
	   - session summary write
	   - rotate/prune 실제 파일 단위 마감 */
	bool ok				= T20_recorderCloseSession(p, "end");
	p->recorder_enabled = false;
	return ok;
}

bool T20_recorderOpenSession(CL_T20_Mfcc::ST_Impl* p, const char* p_name) {
	if (p == nullptr) return false;
	if (p_name == nullptr || p_name[0] == 0) p_name = "default-session";

	strlcpy(p->recorder_session_name, p_name, sizeof(p->recorder_session_name));
	p->recorder_session_open = true;
	p->recorder_session_id++;
	p->recorder_session_open_ms	 = millis();
	p->recorder_session_close_ms = 0;

	/* 향후 단계 구현 예정:
	   - 세션 open 시 파일명 규칙 자동 생성
	   - raw/event/feature/config/metadata 분리 저장 */
	T20_recorderWriteEvent(p, "session_open");
	T20_recorderWriteMetadataHeartbeat(p);
	T20_saveRuntimeConfigFile(p);
	return true;
}

bool T20_recorderCloseSession(CL_T20_Mfcc::ST_Impl* p, const char* p_reason) {
	if (p == nullptr) return false;
	if (!p->recorder_session_open) return true;

	if (p_reason == nullptr) p_reason = "close";
	T20_recorderWriteEvent(p, p_reason);
	T20_recorderFlushNow(p);

	p->recorder_session_open	 = false;
	p->recorder_session_close_ms = millis();

	/* 향후 단계 구현 예정:
	   - close reason 별 finalize 정책 분기
	   - recover/finalize/rewrite 연동 */
	T20_recorderPrepareFinalize(p);
	T20_recorderPreparePersistFinalize(p);
	T20_recorderFinalizeSaveSummary(p);
	T20_saveRecorderIndex(p);
	T20_saveRuntimeConfigFile(p);
	return true;
}

bool T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_text) {
	if (p == nullptr) return false;
	if (p_text == nullptr) p_text = "";

	/* 한글 주석:
	   현재 단계는 이벤트를 별도 파일이 아니라 recorder_last_error/상태 갱신 중심으로 단순 기록합니다. */
	strlcpy(p->recorder_last_error, p_text, sizeof(p->recorder_last_error));
	return true;
}

bool T20_recorderWriteMetadataHeartbeat(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* 한글 주석:
	   현재 단계는 heartbeat 메타를 별도 파일로 쓰지 않고 세션/flush 시각만 갱신합니다. */
	if (p->recorder_session_open && p->recorder_session_open_ms == 0) {
		p->recorder_session_open_ms = millis();
	}
	return true;
}

bool T20_recorderSelectActivePath(CL_T20_Mfcc::ST_Impl* p, char* p_out, uint16_t p_len) {
	if (p == nullptr || p_out == nullptr || p_len == 0) return false;

	if (p->recorder_file_path[0] != 0) {
		strlcpy(p_out, p->recorder_file_path, p_len);
	} else if (p->recorder_fallback_active) {
		strlcpy(p_out, G_T20_RECORDER_FALLBACK_PATH, p_len);
	} else {
		strlcpy(p_out, G_T20_RECORDER_DEFAULT_FILE_PATH, p_len);
	}

	strlcpy(p->recorder_active_path, p_out, sizeof(p->recorder_active_path));
	return true;
}

bool T20_recorderFallbackToLittleFs(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	p->recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;
	p->recorder_fallback_active = true;
	strlcpy(p->recorder_file_path, G_T20_RECORDER_FALLBACK_PATH, sizeof(p->recorder_file_path));
	T20_recorderSetLastError(p, "fallback littlefs active");
	T20_saveRuntimeConfigFile(p);
	return true;
}

bool T20_recorderRotateIfNeeded(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	if (p->recorder_index_count <= p->recorder_rotate_keep_max) return true;

	/* TODO:
	   - 실제 파일 삭제/이름변경까지 확장 가능
	   - 현재 단계는 index trim 중심 */
	while (p->recorder_index_count > p->recorder_rotate_keep_max) {
		for (uint16_t i = 1; i < p->recorder_index_count; ++i) {
			p->recorder_index_items[i - 1] = p->recorder_index_items[i];
		}
		p->recorder_index_count--;
	}

	return T20_saveRecorderIndex(p);
}

bool T20_applySdmmcProfilePins(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	/* TODO:
	   - 실제 보드별 pinMatrix / gpio matrix / host slot 상세 분기
	   - 현재 단계는 프로파일 유효성 검증과 적용 상태 기록 중심 */

	bool valid_basic =
		(p->sdmmc_profile.clk_pin == G_T20_SDMMC_PIN_UNASSIGNED &&
		 p->sdmmc_profile.cmd_pin == G_T20_SDMMC_PIN_UNASSIGNED &&
		 p->sdmmc_profile.d0_pin == G_T20_SDMMC_PIN_UNASSIGNED) ||
		(p->sdmmc_profile.clk_pin != G_T20_SDMMC_PIN_UNASSIGNED &&
		 p->sdmmc_profile.cmd_pin != G_T20_SDMMC_PIN_UNASSIGNED &&
		 p->sdmmc_profile.d0_pin != G_T20_SDMMC_PIN_UNASSIGNED);

	if (!valid_basic) {
		p->sdmmc_profile_applied = false;
		strlcpy(p->sdmmc_last_apply_reason, "invalid basic pin trio", sizeof(p->sdmmc_last_apply_reason));
		return false;
	}

	if (!p->sdmmc_profile.use_1bit_mode) {
		bool valid_4bit =
			(p->sdmmc_profile.d1_pin != G_T20_SDMMC_PIN_UNASSIGNED) &&
			(p->sdmmc_profile.d2_pin != G_T20_SDMMC_PIN_UNASSIGNED) &&
			(p->sdmmc_profile.d3_pin != G_T20_SDMMC_PIN_UNASSIGNED);
		if (!valid_4bit) {
			p->sdmmc_profile_applied = false;
			strlcpy(p->sdmmc_last_apply_reason, "4bit pins incomplete", sizeof(p->sdmmc_last_apply_reason));
			return false;
		}
	}

	p->sdmmc_profile_applied = true;
	strlcpy(p->sdmmc_last_apply_reason, "profile accepted", sizeof(p->sdmmc_last_apply_reason));
	return true;
}

uint8_t T20_selectRecorderWriteBufferSlot(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return 0;
	uint8_t slot					 = p->recorder_zero_copy_slot_index;
	p->recorder_zero_copy_slot_index = (uint8_t)((p->recorder_zero_copy_slot_index + 1U) % G_T20_RECORDER_ZERO_COPY_SLOT_MAX);
	return slot;
}

uint8_t T20_getActiveDmaSlotIndex(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return 0;
	return p->recorder_dma_active_slot % G_T20_ZERO_COPY_DMA_SLOT_COUNT;
}

bool T20_rotateDmaSlot(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	p->recorder_dma_active_slot = (uint8_t)((p->recorder_dma_active_slot + 1U) % G_T20_ZERO_COPY_DMA_SLOT_COUNT);
	return true;
}

void T20_recorderFlushIfNeeded(CL_T20_Mfcc::ST_Impl* p, bool p_force) {
	if (p == nullptr) return;

	if (p_force) {
		p->recorder_flush_requested = true;
		return;
	}

	if (p->recorder_batch_count >= p->recorder_batch_watermark_high) {
		p->recorder_flush_requested = true;
		return;
	}

	if (p->recorder_batch_count >= G_T20_RECORDER_BATCH_FLUSH_RECORDS) {
		p->recorder_flush_requested = true;
	}
}

void T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text) {
	if (p == nullptr) return;
	if (p_text == nullptr) p_text = "";
	strlcpy(p->recorder_last_error, p_text, sizeof(p->recorder_last_error));
}

void T20_initSdmmcProfiles(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;

	memset(p->sdmmc_profiles, 0, sizeof(p->sdmmc_profiles));

	for (uint16_t i = 0; i < G_T20_SDMMC_PROFILE_PRESET_COUNT; ++i) {
		p->sdmmc_profiles[i].clk_pin = G_T20_SDMMC_PIN_UNASSIGNED;
		p->sdmmc_profiles[i].cmd_pin = G_T20_SDMMC_PIN_UNASSIGNED;
		p->sdmmc_profiles[i].d0_pin	 = G_T20_SDMMC_PIN_UNASSIGNED;
		p->sdmmc_profiles[i].d1_pin	 = G_T20_SDMMC_PIN_UNASSIGNED;
		p->sdmmc_profiles[i].d2_pin	 = G_T20_SDMMC_PIN_UNASSIGNED;
		p->sdmmc_profiles[i].d3_pin	 = G_T20_SDMMC_PIN_UNASSIGNED;
	}

	strlcpy(p->sdmmc_profiles[0].profile_name, "default-1bit", sizeof(p->sdmmc_profiles[0].profile_name));
	p->sdmmc_profiles[0].enabled	   = true;
	p->sdmmc_profiles[0].use_1bit_mode = true;

	strlcpy(p->sdmmc_profiles[1].profile_name, "esp32s3-slot-a", sizeof(p->sdmmc_profiles[1].profile_name));
	p->sdmmc_profiles[1].enabled	   = true;
	p->sdmmc_profiles[1].use_1bit_mode = true;
	p->sdmmc_profiles[1].clk_pin	   = 36;
	p->sdmmc_profiles[1].cmd_pin	   = 35;
	p->sdmmc_profiles[1].d0_pin		   = 37;

	strlcpy(p->sdmmc_profiles[2].profile_name, "esp32s3-slot-b-4bit", sizeof(p->sdmmc_profiles[2].profile_name));
	p->sdmmc_profiles[2].enabled	   = true;
	p->sdmmc_profiles[2].use_1bit_mode = false;
	p->sdmmc_profiles[2].clk_pin	   = 14;
	p->sdmmc_profiles[2].cmd_pin	   = 15;
	p->sdmmc_profiles[2].d0_pin		   = 2;
	p->sdmmc_profiles[2].d1_pin		   = 4;
	p->sdmmc_profiles[2].d2_pin		   = 12;
	p->sdmmc_profiles[2].d3_pin		   = 13;

	p->sdmmc_profile				   = p->sdmmc_profiles[0];
	p->sdmmc_profile_applied		   = false;
	strlcpy(p->sdmmc_last_apply_reason, "init default", sizeof(p->sdmmc_last_apply_reason));
}

bool T20_applySdmmcProfileByName(CL_T20_Mfcc::ST_Impl* p, const char* p_name) {
	if (p == nullptr || p_name == nullptr) return false;
	for (uint16_t i = 0; i < G_T20_SDMMC_PROFILE_PRESET_COUNT; ++i) {
		if (p->sdmmc_profiles[i].enabled && strcmp(p->sdmmc_profiles[i].profile_name, p_name) == 0) {
			p->sdmmc_profile = p->sdmmc_profiles[i];
			bool ok			 = T20_applySdmmcProfilePins(p);
			T20_saveRuntimeConfigFile(p);
			return ok;
		}
	}
	p->sdmmc_profile_applied = false;
	strlcpy(p->sdmmc_last_apply_reason, "profile not found", sizeof(p->sdmmc_last_apply_reason));
	return false;
}

bool T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (p->recorder_sdmmc_mounted) return true;

	if (!T20_applySdmmcProfilePins(p)) {
		T20_recorderSetLastError(p, "sdmmc profile invalid");
		return false;
	}

	bool one_bit = p->sdmmc_profile.use_1bit_mode;
	bool ok		 = SD_MMC.begin(G_T20_SDMMC_MOUNT_PATH_DEFAULT, one_bit);
	if (!ok) {
		T20_recorderSetLastError(p, "sdmmc mount failed");
		p->recorder_sdmmc_mounted = false;
		p->sdmmc_profile_applied  = false;
		strlcpy(p->sdmmc_last_apply_reason, "mount failed", sizeof(p->sdmmc_last_apply_reason));
		T20_recorderFallbackToLittleFs(p);
		return false;
	}

	p->recorder_sdmmc_mounted	= true;
	p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;
	p->recorder_enabled			= true;
	strlcpy(p->recorder_sdmmc_mount_path, G_T20_SDMMC_MOUNT_PATH_DEFAULT, sizeof(p->recorder_sdmmc_mount_path));
	strlcpy(p->recorder_sdmmc_board_hint, p->sdmmc_profile.profile_name, sizeof(p->recorder_sdmmc_board_hint));
	strlcpy(p->sdmmc_last_apply_reason, "mounted", sizeof(p->sdmmc_last_apply_reason));
	T20_saveRuntimeConfigFile(p);
	return true;
}

void T20_unmountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return;
	if (p->recorder_sdmmc_mounted) {
		SD_MMC.end();
		p->recorder_sdmmc_mounted	= false;
		p->recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;
		T20_saveRuntimeConfigFile(p);
	}
}

File T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend, const char* p_path, const char* p_mode) {
	if (p_path == nullptr || p_mode == nullptr) return File();
	if (p_backend == EN_T20_STORAGE_LITTLEFS) return LittleFS.open(p_path, p_mode);
	return SD_MMC.open(p_path, p_mode);
}

bool T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg) {
	if (!p_file || p_cfg == nullptr) return false;

	ST_T20_RecorderBinaryHeader_t hdr;
	memset(&hdr, 0, sizeof(hdr));
	hdr.magic			= G_T20_BINARY_MAGIC;
	hdr.version			= G_T20_BINARY_VERSION;
	hdr.header_size		= sizeof(ST_T20_RecorderBinaryHeader_t);
	hdr.sample_rate_hz	= (uint32_t)p_cfg->feature.sample_rate_hz;
	hdr.fft_size		= p_cfg->feature.fft_size;
	hdr.mfcc_dim		= p_cfg->feature.mfcc_coeffs;
	hdr.mel_filters		= p_cfg->feature.mel_filters;
	hdr.sequence_frames = p_cfg->output.sequence_frames;
	hdr.record_count	= 0;

	size_t written		= p_file.write((const uint8_t*)&hdr, sizeof(hdr));
	return (written == sizeof(hdr));
}

bool T20_recorderOpenIfNeeded(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	if (p->recorder_file_opened) return true;

	char active_path[128] = {0};
	T20_recorderSelectActivePath(p, active_path, sizeof(active_path));

	File file	= T20_openRecorderFileByBackend(p->recorder_storage_backend, active_path, "r");
	bool exists = (bool)file;
	if (file) file.close();

	file = T20_openRecorderFileByBackend(p->recorder_storage_backend, active_path, exists ? "a" : "w");
	if (!file) {
		T20_recorderSetLastError(p, "recorder open failed");
		return false;
	}

	if (!exists) {
		if (!T20_writeRecorderBinaryHeader(file, &p->cfg)) {
			file.close();
			T20_recorderSetLastError(p, "binary header write failed");
			return false;
		}
	}

	file.close();
	strlcpy(p->recorder_active_path, active_path, sizeof(p->recorder_active_path));
	p->recorder_file_opened = true;
	if (!p->recorder_session_open) {
		T20_recorderOpenSession(p, p->recorder_session_name);
	}
	return true;
}

bool T20_recorderAppendVector(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg) {
	if (p == nullptr || p_msg == nullptr) return false;
	if (!T20_recorderOpenIfNeeded(p)) return false;

	File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_active_path[0] ? p->recorder_active_path : p->recorder_file_path, "a");
	if (!file) {
		T20_recorderSetLastError(p, "append open failed");
		p->recorder_file_opened = false;
		return false;
	}

	size_t w1 = file.write((const uint8_t*)&p_msg->frame_id, sizeof(p_msg->frame_id));
	size_t w2 = file.write((const uint8_t*)&p_msg->vector_len, sizeof(p_msg->vector_len));
	size_t w3 = file.write((const uint8_t*)p_msg->vector, sizeof(float) * p_msg->vector_len);
	file.close();

	if (w1 != sizeof(p_msg->frame_id) || w2 != sizeof(p_msg->vector_len) || w3 != sizeof(float) * p_msg->vector_len) {
		T20_recorderSetLastError(p, "append write failed");
		return false;
	}

	return true;
}

bool T20_commitDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p, uint8_t p_slot_index) {
	if (p == nullptr) return false;
	if (p_slot_index >= G_T20_ZERO_COPY_DMA_SLOT_COUNT) return false;

	uint16_t used = p->recorder_dma_slot_used[p_slot_index];
	if (used == 0) return true;

	if (!T20_recorderOpenIfNeeded(p)) return false;

	File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_active_path[0] ? p->recorder_active_path : p->recorder_file_path, "a");
	if (!file) {
		T20_recorderSetLastError(p, "dma commit open failed");
		p->recorder_file_opened = false;
		return false;
	}

	size_t written = file.write((const uint8_t*)p->recorder_dma_slots[p_slot_index], used);
	file.close();

	if (written != used) {
		T20_recorderSetLastError(p, "dma commit write failed");
		return false;
	}

	p->recorder_dma_slot_used[p_slot_index] = 0;
	return true;
}

bool T20_commitActiveDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	uint8_t last_filled = (uint8_t)((p->recorder_dma_active_slot + G_T20_ZERO_COPY_DMA_SLOT_COUNT - 1U) % G_T20_ZERO_COPY_DMA_SLOT_COUNT);
	return T20_commitDmaSlotToFile(p, last_filled);
}

bool T20_stageVectorToDmaSlot(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg) {
	if (p == nullptr || p_msg == nullptr) return false;

	uint16_t need = (uint16_t)(sizeof(p_msg->frame_id) + sizeof(p_msg->vector_len) + (sizeof(float) * p_msg->vector_len));
	if (need > G_T20_ZERO_COPY_DMA_SLOT_BYTES) {
		T20_recorderSetLastError(p, "dma slot overflow");
		return false;
	}

	uint8_t slot = T20_getActiveDmaSlotIndex(p);
	if ((uint16_t)(p->recorder_dma_slot_used[slot] + need) > G_T20_ZERO_COPY_DMA_SLOT_BYTES) {
		if (!T20_commitDmaSlotToFile(p, slot)) return false;
	}

	uint16_t ofs = p->recorder_dma_slot_used[slot];
	uint8_t* buf = p->recorder_dma_slots[slot];

	memcpy(&buf[ofs], &p_msg->frame_id, sizeof(p_msg->frame_id));
	ofs += sizeof(p_msg->frame_id);
	memcpy(&buf[ofs], &p_msg->vector_len, sizeof(p_msg->vector_len));
	ofs += sizeof(p_msg->vector_len);
	memcpy(&buf[ofs], p_msg->vector, sizeof(float) * p_msg->vector_len);
	ofs += (uint16_t)(sizeof(float) * p_msg->vector_len);

	p->recorder_dma_slot_used[slot] = ofs;

	if ((uint16_t)(G_T20_ZERO_COPY_DMA_SLOT_BYTES - p->recorder_dma_slot_used[slot]) < G_T20_ZERO_COPY_DMA_COMMIT_MIN_BYTES) {
		T20_rotateDmaSlot(p);
	}

	return true;
}

bool T20_recorderBatchPush(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg) {
	if (p == nullptr || p_msg == nullptr) return false;
	if (p->recorder_batch_count >= G_T20_RECORDER_BATCH_VECTOR_MAX) {
		if (!T20_recorderBatchFlush(p)) return false;
	}

	if (!T20_stageVectorToDmaSlot(p, p_msg)) return false;
	p->recorder_batch_vectors[p->recorder_batch_count] = *p_msg;
	p->recorder_batch_count++;
	p->recorder_batch_last_push_ms = millis();

	if (p->recorder_batch_count >= p->recorder_batch_watermark_low) {
		T20_rotateDmaSlot(p);
	}

	T20_recorderFlushIfNeeded(p, false);
	return true;
}

bool T20_recorderBatchFlush(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	uint16_t count_before = p->recorder_batch_count;
	if (count_before == 0) {
		p->recorder_batch_last_push_ms = 0;
		return true;
	}

	bool ok = true;
	for (uint8_t slot = 0; slot < G_T20_ZERO_COPY_DMA_SLOT_COUNT; ++slot) {
		if (!T20_commitDmaSlotToFile(p, slot)) {
			ok = false;
			break;
		}
	}

	if (ok) {
		p->recorder_record_count += count_before;
		p->recorder_batch_count		   = 0;
		p->recorder_batch_last_push_ms = 0;
		memset(p->recorder_batch_vectors, 0, sizeof(p->recorder_batch_vectors));
	}
	return ok;
}

bool T20_recorderFlushNow(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;
	bool ok = T20_recorderBatchFlush(p);
	memset(p->recorder_dma_slot_used, 0, sizeof(p->recorder_dma_slot_used));
	p->recorder_flush_requested = false;
	p->recorder_last_flush_ms	= millis();
	T20_recorderWriteMetadataHeartbeat(p);
	T20_recorderRotateIfNeeded(p);
	return ok;
}

bool T20_saveRecorderIndex(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return false;

	File file = LittleFS.open(G_T20_RECORDER_INDEX_FILE_PATH, "w");
	if (!file) return false;

	JsonDocument doc;
	doc["count"]		  = p->recorder_index_count;
	doc["active_profile"] = p->sdmmc_profile.profile_name;
	JsonArray arr		  = doc["items"].to<JsonArray>();
	for (uint16_t i = 0; i < p->recorder_index_count && i < G_T20_RECORDER_MAX_ROTATE_LIST; ++i) {
		JsonObject o	  = arr.add<JsonObject>();
		o["path"]		  = p->recorder_index_items[i].path;
		o["size_bytes"]	  = p->recorder_index_items[i].size_bytes;
		o["created_ms"]	  = p->recorder_index_items[i].created_ms;
		o["record_count"] = p->recorder_index_items[i].record_count;
	}

	serializeJson(doc, file);
	file.close();
	return true;
}

void T20_recorderTask(void* p_arg) {
	CL_T20_Mfcc::ST_Impl*		   p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
	ST_T20_RecorderVectorMessage_t msg;

	for (;;) {
		if (p == nullptr || p->recorder_queue == nullptr) {
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		if (!p->running) {
			vTaskDelay(pdMS_TO_TICKS(50));
			continue;
		}

		if (!p->recorder_enabled) {
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		if (xQueueReceive(p->recorder_queue, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
			T20_recorderBatchPush(p, &msg);

			if (p->recorder_index_count == 0) {
				p->recorder_index_count = 1;
				strlcpy(p->recorder_index_items[0].path, p->recorder_active_path[0] ? p->recorder_active_path : p->recorder_file_path, sizeof(p->recorder_index_items[0].path));
				p->recorder_index_items[0].created_ms = millis();
			}

			p->recorder_index_items[0].record_count = p->recorder_record_count + p->recorder_batch_count;

			File f									= T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_active_path[0] ? p->recorder_active_path : p->recorder_file_path, "r");
			if (f) {
				p->recorder_index_items[0].size_bytes = (uint32_t)f.size();
				f.close();
			}

			if (p->recorder_flush_requested) {
				T20_recorderFlushNow(p);
				T20_saveRecorderIndex(p);
			}
		} else {
			if (p->recorder_batch_count > 0) {
				uint32_t now_ms = millis();
				if ((now_ms - p->recorder_batch_last_push_ms) >= p->recorder_batch_idle_flush_ms) {
					T20_recorderFlushNow(p);
					T20_saveRecorderIndex(p);
				}
			}
		}
	}
}

/* 한글 주석:
   향후 단계 구현 예정 사항 정리:
   - 세션 종료 시 summary 파일 생성
   - raw/event/feature/config 분리 저장
   - binary finalize/recover 처리 강화 */
