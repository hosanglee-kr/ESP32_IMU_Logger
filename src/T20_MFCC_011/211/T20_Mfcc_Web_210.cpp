/* ============================================================================
[잔여 구현 계획 재점검 - Web v210]

현재 web는 상태 번들을 확인하기 위한 endpoint와 버튼이 충분히 준비되어 있다.
이제부터는 새 endpoint 추가보다,
실제 begin/burst/isr/flush/close/finalize 결과가 기존 endpoint에 잘 반영되는지 점검하는 단계다.

우선순위
1. 실제 연결 후 값 변화가 보이는지 검증
2. 중복 bundle 정리
3. placeholder 성격이 강한 route를 실제 연결 검증용 route로 재정비

반복 오류 주의
- endpoint 추가 전 builder 존재 여부 점검
- builder가 direct member access로 회귀하지 않는지 점검
============================================================================ */

/* ============================================================================
[반복 오류 패턴 점검 체크리스트]
1. endpoint 추가 전 builder 함수 존재 여부 점검
2. 신규 이름은 기존 route/base naming과 충돌 여부 점검
============================================================================ */

/* ============================================================================
[향후 단계 구현 예정 정리 - Web]
1. recorder session 관리 endpoint 확장
2. preview parser 결과 노출 강화
3. live source 상태/제어 UI 연동 강화
4. 055 대비 필요한 진단 endpoint 선별 복구
============================================================================ */

#include "T20_Mfcc_Web_210.h"

uint32_t T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p) {
	if (p == nullptr) return 0;
	uint32_t h = 2166136261UL;
	h ^= p->recorder_record_count;
	h *= 16777619UL;
	h ^= p->dropped_frames;
	h *= 16777619UL;
	h ^= p->viewer_last_frame_id;
	h *= 16777619UL;
	return h;
}

bool T20_rotateListDeleteFile(const char* p_path) {
	if (p_path == nullptr || p_path[0] == 0) return false;
	if (LittleFS.exists(p_path)) return LittleFS.remove(p_path);
	return false;
}

void T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p) {
	(void)p;
}

bool T20_getQueryParamPath(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len) {
	if (request == nullptr || p_name == nullptr || p_out_buf == nullptr || p_len == 0) return false;
	if (!request->hasParam(p_name)) return false;
	const AsyncWebParameter* param = request->getParam(p_name);
	if (param == nullptr) return false;
	strlcpy(p_out_buf, param->value().c_str(), p_len);
	return true;
}

void T20_getQueryParamText(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len, const char* p_default) {
	if (p_out_buf == nullptr || p_len == 0) return;
	if (p_default == nullptr) p_default = "";
	strlcpy(p_out_buf, p_default, p_len);
	if (request != nullptr && p_name != nullptr && request->hasParam(p_name)) {
		const AsyncWebParameter* param = request->getParam(p_name);
		if (param != nullptr) strlcpy(p_out_buf, param->value().c_str(), p_len);
	}
}

uint32_t T20_getQueryParamUint32(AsyncWebServerRequest* request, const char* p_name, uint32_t p_default, uint32_t p_min, uint32_t p_max) {
	uint32_t v = p_default;
	if (request != nullptr && p_name != nullptr && request->hasParam(p_name)) {
		const AsyncWebParameter* param = request->getParam(p_name);
		if (param != nullptr) v = (uint32_t)param->value().toInt();
	}
	if (v < p_min) v = p_min;
	if (v > p_max) v = p_max;
	return v;
}

static void T20_sendJsonText(AsyncWebServerRequest* request, bool ok, const char* json_ok) {
	if (request == nullptr) return;
	if (ok && json_ok != nullptr)
		request->send(200, "application/json; charset=utf-8", json_ok);
	else
		request->send(500, "application/json; charset=utf-8", "{\"ok\":false}");
}

void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path) {
	if (p == nullptr || v_server == nullptr) return;
	const char* base						 = (p_base_path == nullptr || p_base_path[0] == 0) ? "/api/t20" : p_base_path;

	String		s_status					 = String(base) + "/status";
	String		s_config					 = String(base) + "/config";
	String		s_runtime_config			 = String(base) + "/runtime_config";
	String		s_runtime_config_apply		 = String(base) + "/runtime_config_apply";
	String		s_batch_tuning_apply		 = String(base) + "/batch_tuning_apply";
	String		s_sdmmc_profile_apply		 = String(base) + "/sdmmc/profile_apply";
	String		s_sdmmc_profiles			 = String(base) + "/sdmmc/profiles";
	String		s_selection_sync			 = String(base) + "/selection_sync";
	String		s_selection_sync_apply		 = String(base) + "/selection_sync_apply";
	String		s_type_meta					 = String(base) + "/type_meta";
	String		s_type_meta_apply			 = String(base) + "/type_meta_apply";
	String		s_type_preview_load			 = String(base) + "/type_preview_load";
	String		s_build_sanity				 = String(base) + "/build_sanity";
	String		s_unified_viewer_bundle		 = String(base) + "/viewer_bundle_unified";
	String		s_recorder_storage			 = String(base) + "/recorder_storage";
	String		s_recorder_rotate_apply		 = String(base) + "/recorder_rotate_apply";
	String		s_live_source				 = String(base) + "/live_source";
	String		s_live_source_apply			 = String(base) + "/live_source_apply";
	String		s_live_debug				 = String(base) + "/live_debug";
	String		s_bmi270_read_state			 = String(base) + "/bmi270_read_state";
	String		s_bmi270_actual_state		 = String(base) + "/bmi270_actual_state";
	String		s_bmi270_apply_state		 = String(base) + "/bmi270_apply_state";
	String		s_bmi270_session_state		 = String(base) + "/bmi270_session_state";
	String		s_bmi270_pipeline_state		 = String(base) + "/bmi270_pipeline_state";
	String		s_bmi270_readback_state		 = String(base) + "/bmi270_readback_state";
	String		s_bmi270_verify_state		 = String(base) + "/bmi270_verify_state";
	String		s_bmi270_bridge_state		 = String(base) + "/bmi270_bridge_state";
	String		s_recorder_session			 = String(base) + "/recorder_session";
	String		s_recorder_begin			 = String(base) + "/recorder_begin";
	String		s_recorder_end				 = String(base) + "/recorder_end";
	String		s_recorder_finalize			 = String(base) + "/recorder_finalize";
	String		s_recorder_finalize_save	 = String(base) + "/recorder_finalize_save";
	String		s_recorder_finalize_persist	 = String(base) + "/recorder_finalize_persist";
	String		s_recorder_finalize_result	 = String(base) + "/recorder_finalize_result";
	String		s_recorder_persist_state	 = String(base) + "/recorder_persist_state";
	String		s_recorder_commit_state		 = String(base) + "/recorder_commit_state";
	String		s_recorder_commit_result	 = String(base) + "/recorder_commit_result";
	String		s_recorder_finalize_pipeline = String(base) + "/recorder_finalize_pipeline";
	String		s_recorder_manifest_state	 = String(base) + "/recorder_manifest_state";
	String		s_recorder_summary_state	 = String(base) + "/recorder_summary_state";
	String		s_recorder_index_state		 = String(base) + "/recorder_index_state";
	String		s_recorder_artifact_state	 = String(base) + "/recorder_artifact_state";
	String		s_config_schema				 = String(base) + "/config_schema";
	String		s_viewer_data				 = String(base) + "/viewer/data";
	String		s_viewer_waveform			 = String(base) + "/viewer/waveform";
	String		s_viewer_spectrum			 = String(base) + "/viewer/spectrum";
	String		s_viewer_events				 = String(base) + "/viewer/events";
	String		s_viewer_sequence			 = String(base) + "/viewer/sequence";
	String		s_viewer_overview			 = String(base) + "/viewer/overview";
	String		s_viewer_multi_frame		 = String(base) + "/viewer/multi_frame";
	String		s_viewer_chart_bundle		 = String(base) + "/viewer/chart_bundle";
	String		s_rec_manifest				 = String(base) + "/recorder/manifest";
	String		s_rec_index					 = String(base) + "/recorder/index";
	String		s_rec_preview				 = String(base) + "/recorder/preview";
	String		s_rec_preview_parsed		 = String(base) + "/recorder/preview_parsed";
	String		s_rec_range					 = String(base) + "/recorder/file_range";
	String		s_rec_range_json			 = String(base) + "/recorder/file_range_json";
	String		s_rec_bin_header			 = String(base) + "/recorder/binary_header";
	String		s_rec_csv_table				 = String(base) + "/recorder/file_csv_table";
	String		s_rec_csv_schema			 = String(base) + "/recorder/file_csv_schema";
	String		s_rec_csv_type_meta			 = String(base) + "/recorder/file_csv_type_meta";
	String		s_rec_csv_table_adv			 = String(base) + "/recorder/file_csv_table_advanced";
	String		s_rec_bin_records			 = String(base) + "/recorder/binary_records";
	String		s_rec_bin_payload_schema	 = String(base) + "/recorder/binary_payload_schema";
	String		s_render_selection_sync		 = String(base) + "/render_selection_sync";
	String		s_type_meta_preview_link	 = String(base) + "/type_meta_preview_link";

	v_server->on(s_status.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		JsonDocument doc;
		doc["ok"]						   = true;
		doc["running"]					   = p->running;
		doc["initialized"]				   = p->initialized;
		doc["status_hash"]				   = T20_calcStatusHash(p);
		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		serializeJson(doc, json, sizeof(json));
		request->send(200, "application/json; charset=utf-8", json);
	});

	v_server->on(s_config.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildConfigJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_config_schema.c_str(), HTTP_GET, [](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildConfigSchemaJsonText(json, sizeof(json)), json);
	});

	v_server->on(s_runtime_config.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRuntimeConfigJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_runtime_config_apply.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
		char json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
		T20_getQueryParamText(request, "json", json, sizeof(json), "{}");
		bool ok = T20_applyRuntimeConfigJsonText(p, json);
		if (!ok) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"invalid runtime config\"}");
			return;
		}
		T20_saveRuntimeConfigFile(p);
		char out_json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
		T20_buildRuntimeConfigJsonText(p, out_json, sizeof(out_json));
		request->send(200, "application/json; charset=utf-8", out_json);
	});

	v_server->on(s_batch_tuning_apply.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
		uint32_t low					 = T20_getQueryParamUint32(request, "low", p->recorder_batch_watermark_low, 1, G_T20_RECORDER_BATCH_VECTOR_MAX);
		uint32_t high					 = T20_getQueryParamUint32(request, "high", p->recorder_batch_watermark_high, low, G_T20_RECORDER_BATCH_VECTOR_MAX);
		uint32_t idle_ms				 = T20_getQueryParamUint32(request, "idle_ms", p->recorder_batch_idle_flush_ms, 1, 10000);

		p->recorder_batch_watermark_low	 = (uint16_t)low;
		p->recorder_batch_watermark_high = (uint16_t)high;
		p->recorder_batch_idle_flush_ms	 = idle_ms;

		T20_saveRuntimeConfigFile(p);

		char out_json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
		T20_buildRuntimeConfigJsonText(p, out_json, sizeof(out_json));
		request->send(200, "application/json; charset=utf-8", out_json);
	});

	v_server->on(s_sdmmc_profile_apply.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
		char name[64] = {0};
		T20_getQueryParamText(request, "name", name, sizeof(name), "default-1bit");
		bool ok = T20_applySdmmcProfileByName(p, name);
		if (!ok) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"invalid profile\"}");
			return;
		}
		char json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
		T20_buildRuntimeConfigJsonText(p, json, sizeof(json));
		request->send(200, "application/json; charset=utf-8", json);
	});

	v_server->on(s_sdmmc_profiles.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		bool ok									 = T20_buildSdmmcProfilesJsonText(p, json, sizeof(json));
		T20_sendJsonText(request, ok, json);
	});

	v_server->on(s_selection_sync.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		bool ok							   = T20_buildSelectionSyncJsonText(p, json, sizeof(json));
		T20_sendJsonText(request, ok, json);
	});

	v_server->on(s_selection_sync_apply.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
		uint32_t frame_from							 = T20_getQueryParamUint32(request, "frame_from", p->selection_sync_frame_from, 0, 0xFFFFFFFFUL);
		uint32_t frame_to							 = T20_getQueryParamUint32(request, "frame_to", p->selection_sync_frame_to, 0, 0xFFFFFFFFUL);
		char	 name[G_T20_SELECTION_SYNC_NAME_MAX] = {0};
		T20_getQueryParamText(request, "name", name, sizeof(name), p->selection_sync_name);
		uint32_t enabled			 = T20_getQueryParamUint32(request, "enabled", p->selection_sync_enabled ? 1U : 0U, 0, 1);

		p->selection_sync_enabled	 = (enabled != 0);
		p->selection_sync_frame_from = frame_from;
		p->selection_sync_frame_to	 = frame_to;
		strlcpy(p->selection_sync_name, name, sizeof(p->selection_sync_name));
		T20_updateSelectionSyncState(p);
		T20_updateViewerSelectionProjection(p);
		T20_updateViewerOverlayProjection(p);
		T20_syncDerivedViewState(p);
		T20_saveRuntimeConfigFile(p);

		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_buildSelectionSyncJsonText(p, json, sizeof(json));
		request->send(200, "application/json; charset=utf-8", json);
	});

	v_server->on(s_type_meta.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		bool ok							   = T20_buildTypeMetaJsonText(p, json, sizeof(json));
		T20_sendJsonText(request, ok, json);
	});

	v_server->on(s_type_meta_apply.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
		char name[G_T20_TYPE_META_NAME_MAX] = {0};
		char kind[G_T20_TYPE_META_KIND_MAX] = {0};
		T20_getQueryParamText(request, "name", name, sizeof(name), p->type_meta_name);
		T20_getQueryParamText(request, "kind", kind, sizeof(kind), p->type_meta_kind);
		uint32_t enabled = T20_getQueryParamUint32(request, "enabled", p->type_meta_enabled ? 1U : 0U, 0, 1);

		strlcpy(p->type_meta_name, name, sizeof(p->type_meta_name));
		strlcpy(p->type_meta_kind, kind, sizeof(p->type_meta_kind));
		p->type_meta_enabled = (enabled != 0);
		T20_updateTypeMetaAutoClassify(p);
		T20_updateViewerSelectionProjection(p);
		T20_updateViewerOverlayProjection(p);
		T20_saveRuntimeConfigFile(p);

		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_buildTypeMetaJsonText(p, json, sizeof(json));
		request->send(200, "application/json; charset=utf-8", json);
	});

	v_server->on(s_viewer_data.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildViewerDataJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_viewer_waveform.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildViewerWaveformJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_viewer_spectrum.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildViewerSpectrumJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_viewer_events.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildViewerEventsJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_viewer_sequence.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildViewerSequenceJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_viewer_overview.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildViewerOverviewJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_viewer_multi_frame.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildViewerMultiFrameJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_viewer_chart_bundle.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		uint16_t points								 = (uint16_t)T20_getQueryParamUint32(request, "points", 128, 8, 2048);
		char	 json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildViewerChartBundleJsonText(p, json, sizeof(json), points), json);
	});

	v_server->on(s_rec_manifest.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderManifestJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_rec_index.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderIndexJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_rec_preview.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
			return;
		}
		uint32_t bytes								 = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
		char	 json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderPreviewJsonText(p, json, sizeof(json), path, bytes), json);
	});

	v_server->on(s_rec_preview_parsed.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
			return;
		}
		uint32_t bytes								 = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
		char	 json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderParsedPreviewJsonText(p, json, sizeof(json), path, bytes), json);
	});

	v_server->on(s_rec_range_json.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
			return;
		}
		uint32_t offset								 = T20_getQueryParamUint32(request, "offset", 0, 0, 0xFFFFFFFFUL);
		uint32_t length								 = T20_getQueryParamUint32(request, "length", 1024, 1, 0xFFFFFFFFUL);
		char	 json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderRangeJsonText(p, json, sizeof(json), path, offset, length), json);
	});

	v_server->on(s_rec_bin_header.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		T20_getQueryParamText(request, "path", path, sizeof(path), "");
		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderBinaryHeaderJsonText(p, json, sizeof(json), path), json);
	});

	v_server->on(s_rec_csv_table.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
			return;
		}
		uint32_t bytes								 = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
		char	 json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderCsvTableJsonText(p, json, sizeof(json), path, bytes), json);
	});

	v_server->on(s_rec_csv_schema.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
			return;
		}
		uint32_t bytes						   = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
		char	 json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderCsvSchemaJsonText(p, json, sizeof(json), path, bytes), json);
	});

	v_server->on(s_rec_csv_type_meta.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
			return;
		}
		uint32_t bytes						   = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
		char	 json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderCsvTypeMetaJsonText(p, json, sizeof(json), path, bytes), json);
	});

	v_server->on(s_rec_csv_table_adv.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE]					   = {0};
		char filter[G_T20_TEXT_PREVIEW_LINE_BUF_SIZE]		   = {0};
		char col_filters_csv[G_T20_TEXT_PREVIEW_LINE_BUF_SIZE] = {0};
		if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
			request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
			return;
		}
		T20_getQueryParamText(request, "filter", filter, sizeof(filter), "");
		T20_getQueryParamText(request, "col_filters", col_filters_csv, sizeof(col_filters_csv), "");
		uint32_t bytes								 = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
		uint16_t sort_col							 = (uint16_t)T20_getQueryParamUint32(request, "sort_col", 0, 0, 64);
		uint16_t sort_dir							 = (uint16_t)T20_getQueryParamUint32(request, "sort_dir", G_T20_CSV_SORT_ASC, G_T20_CSV_SORT_ASC, G_T20_CSV_SORT_DESC);
		uint16_t page								 = (uint16_t)T20_getQueryParamUint32(request, "page", 0, 0, 999);
		uint16_t page_size							 = (uint16_t)T20_getQueryParamUint32(request, "page_size", G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT, 1, G_T20_CSV_TABLE_PAGE_SIZE_MAX);
		char	 json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderCsvTableAdvancedJsonText(p, json, sizeof(json), path, bytes, filter, col_filters_csv, sort_col, sort_dir, page, page_size), json);
	});

	v_server->on(s_rec_bin_records.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		T20_getQueryParamText(request, "path", path, sizeof(path), "");
		uint32_t offset								 = T20_getQueryParamUint32(request, "offset", 0, 0, 0xFFFFFFFFUL);
		uint32_t limit								 = T20_getQueryParamUint32(request, "limit", 32, 1, 4096);
		char	 json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderBinaryRecordsJsonText(p, json, sizeof(json), path, offset, limit), json);
	});

	v_server->on(s_rec_bin_payload_schema.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		T20_getQueryParamText(request, "path", path, sizeof(path), "");
		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRecorderBinaryPayloadSchemaJsonText(p, json, sizeof(json), path), json);
	});

	v_server->on(s_render_selection_sync.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildRenderSelectionSyncJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_type_meta_preview_link.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
		char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
		T20_sendJsonText(request, T20_buildTypeMetaPreviewLinkJsonText(p, json, sizeof(json)), json);
	});

	v_server->on(s_rec_range.c_str(), HTTP_GET, [](AsyncWebServerRequest* request) {
		char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
		if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
			request->send(400, "text/plain; charset=utf-8", "path required");
			return;
		}
		if (!LittleFS.exists(path)) {
			request->send(404, "text/plain; charset=utf-8", "not found");
			return;
		}

		File file = LittleFS.open(path, "r");
		if (!file) {
			request->send(500, "text/plain; charset=utf-8", "open failed");
			return;
		}

		uint32_t size	= (uint32_t)file.size();
		uint32_t offset = 0;
		uint32_t length = size;

		if (request->hasHeader("Range")) {
			const AsyncWebHeader* header = request->getHeader("Range");
			if (header != nullptr) {
				uint32_t o = 0;
				uint32_t l = 0;
				if (T20_parseHttpRangeHeader(header->value(), size, &o, &l)) {
					offset = o;
					length = l;
				}
			}
		}

		file.seek(offset);
		std::unique_ptr<uint8_t[]> buf(new uint8_t[length]);
		if (!buf) {
			file.close();
			request->send(500, "text/plain; charset=utf-8", "alloc failed");
			return;
		}

		size_t read_len = file.read(buf.get(), length);
		file.close();

		AsyncWebServerResponse* resp = request->beginResponse(200, "application/octet-stream", (const uint8_t*)buf.get(), read_len);
		if (resp != nullptr) {
			resp->addHeader("Accept-Ranges", "bytes");
			request->send(resp);
		} else {
			request->send(500, "text/plain; charset=utf-8", "response failed");
		}
	});
}
