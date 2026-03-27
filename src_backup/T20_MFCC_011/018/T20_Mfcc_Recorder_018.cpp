#include "T20_Mfcc_Inter_018.h"

#include <SD_MMC.h>
#include <FS.h>

/*
===============================================================================
소스명: T20_Mfcc_Recorder_018.cpp
버전: v018

[기능 스펙]
- SD_MMC recorder 전용 queue/task
- session 디렉터리 생성
- binary raw header 설계/구현
- event/raw/feature 비동기 기록
- batching drain + 주기 flush
- metadata heartbeat 기록

[이번 단계 큰 다음 진행]
- recorder queue/task 구조를 유지하면서 파일 핸들을 세션 동안 유지
- queue를 한 번 깨울 때 여러 건(batch) 처리
- flush 주기와 metadata 주기를 설정값으로 제어
- AsyncWeb 다운로드와 바로 연결 가능한 세션 파일 경로 유지

[향후 단계 TODO]
- reopen / retry / 오류 복구
- 파일 rotate
- 압축/요약 저장
- AsyncWeb 다운로드/상태 노출
===============================================================================
*/

static bool T20_writeTextLineToOpenFile(File& p_file, const char* p_line)
{
    if (!p_file || p_line == nullptr) {
        return false;
    }

    p_file.println(p_line);
    return true;
}

static bool T20_writeBinaryRawRecordToOpenFile(File& p_file,
                                               uint32_t p_timestamp_ms,
                                               const float* p_samples,
                                               uint16_t p_sample_count)
{
    if (!p_file || p_samples == nullptr || p_sample_count == 0) {
        return false;
    }

    ST_T20_RecorderRecordHeader_t hdr;
    hdr.magic = G_T20_RECORDER_RECORD_MAGIC;
    hdr.record_type = (uint16_t)EN_T20_REC_RECORD_RAW_FRAME;
    hdr.payload_bytes = (uint16_t)(sizeof(uint16_t) + sizeof(float) * p_sample_count);
    hdr.timestamp_ms = p_timestamp_ms;

    size_t w1 = p_file.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    size_t w2 = p_file.write(reinterpret_cast<const uint8_t*>(&p_sample_count), sizeof(p_sample_count));
    size_t w3 = p_file.write(reinterpret_cast<const uint8_t*>(p_samples), sizeof(float) * p_sample_count);

    return (w1 == sizeof(hdr) &&
            w2 == sizeof(p_sample_count) &&
            w3 == sizeof(float) * p_sample_count);
}

bool T20_recorderBegin(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

    if (!p->cfg.recorder.enable) {
        p->recorder.mounted = false;
        return true;
    }

    if (p->recorder.mounted) {
        return true;
    }

    if (!SD_MMC.begin()) {
        p->recorder.mounted = false;
        return false;
    }

    p->recorder.mounted = true;

    if (!SD_MMC.exists(p->cfg.recorder.root_dir)) {
        SD_MMC.mkdir(p->cfg.recorder.root_dir);
    }

    return true;
}

void T20_recorderEnd(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    if (p->recorder.session_open) {
        T20_recorderCloseSession(p);
    }

    p->recorder.mounted = false;
}

bool T20_recorderOpenSession(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

    if (!p->cfg.recorder.enable) {
        return true;
    }

    if (!p->recorder.mounted) {
        if (!T20_recorderBegin(p)) {
            return false;
        }
    }

    if (p->recorder.session_open) {
        return true;
    }

    p->recorder.session_index++;

    snprintf(p->recorder.session_dir,
             sizeof(p->recorder.session_dir),
             "%s/%s_%06lu",
             p->cfg.recorder.root_dir,
             p->cfg.recorder.session_prefix,
             (unsigned long)p->recorder.session_index);

    SD_MMC.mkdir(p->recorder.session_dir);

    snprintf(p->recorder.raw_path, sizeof(p->recorder.raw_path), "%s/raw.%s",
             p->recorder.session_dir,
             (p->cfg.recorder.raw_format == EN_T20_REC_RAW_CSV) ? "csv" : "bin");

    snprintf(p->recorder.cfg_path, sizeof(p->recorder.cfg_path), "%s/config.json",
             p->recorder.session_dir);

    snprintf(p->recorder.meta_path, sizeof(p->recorder.meta_path), "%s/metadata.%s",
             p->recorder.session_dir,
             (p->cfg.recorder.metadata_format == EN_T20_REC_META_CSV) ? "csv" : "jsonl");

    snprintf(p->recorder.event_path, sizeof(p->recorder.event_path), "%s/event.%s",
             p->recorder.session_dir,
             (p->cfg.recorder.event_format == EN_T20_REC_EVENT_CSV) ? "csv" : "jsonl");

    snprintf(p->recorder.feature_path, sizeof(p->recorder.feature_path), "%s/feature.csv",
             p->recorder.session_dir);

    memset(&p->recorder.raw_header, 0, sizeof(p->recorder.raw_header));
    p->recorder.raw_header.magic = G_T20_RAW_BINARY_MAGIC;
    p->recorder.raw_header.header_version = G_T20_RAW_BINARY_HEADER_VERSION;
    strncpy(p->recorder.raw_header.module_version, G_T20_VERSION_STR, sizeof(p->recorder.raw_header.module_version) - 1);
    p->recorder.raw_header.frame_size = p->cfg.feature.frame_size;
    p->recorder.raw_header.hop_size = p->cfg.feature.hop_size;
    p->recorder.raw_header.sample_rate_hz = p->cfg.feature.sample_rate_hz;
    p->recorder.raw_header.axis = (uint16_t)p->cfg.preprocess.axis;
    p->recorder.raw_header.mfcc_coeffs = p->cfg.feature.mfcc_coeffs;
    p->recorder.raw_header.mel_filters = p->cfg.feature.mel_filters;
    p->recorder.raw_header.delta_window = p->cfg.feature.delta_window;
    p->recorder.raw_header.session_start_ms = millis();

    p->recorder.raw_file = File();
    p->recorder.meta_file = File();
    p->recorder.event_file = File();
    p->recorder.feature_file = File();

    if (p->cfg.recorder.raw_format == EN_T20_REC_RAW_BINARY) {
        p->recorder.raw_file = SD_MMC.open(p->recorder.raw_path, FILE_WRITE);
    } else if (p->cfg.recorder.raw_format == EN_T20_REC_RAW_CSV) {
        p->recorder.raw_file = SD_MMC.open(p->recorder.raw_path, FILE_APPEND);
    }

    if (p->cfg.recorder.metadata_format != EN_T20_REC_META_NONE) {
        p->recorder.meta_file = SD_MMC.open(p->recorder.meta_path, FILE_APPEND);
    }

    if (p->cfg.recorder.event_format != EN_T20_REC_EVENT_NONE) {
        p->recorder.event_file = SD_MMC.open(p->recorder.event_path, FILE_APPEND);
    }

    if (p->cfg.recorder.write_feature_vector_csv) {
        p->recorder.feature_file = SD_MMC.open(p->recorder.feature_path, FILE_APPEND);
    }

    p->recorder.raw_dirty = false;
    p->recorder.meta_dirty = false;
    p->recorder.event_dirty = false;
    p->recorder.feature_dirty = false;
    p->recorder.queued_record_count = 0;
    p->recorder.written_record_count = 0;
    p->recorder.last_flush_ms = millis();
    p->recorder.last_metadata_ms = millis();

    p->recorder.session_open = true;

    if (p->cfg.recorder.raw_format == EN_T20_REC_RAW_BINARY) {
        if (!T20_recorderWriteBinaryHeader(p)) {
            T20_recorderCloseSession(p);
            return false;
        }
    }

    return true;
}

void T20_recorderCloseSession(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    T20_recorderFlushIfNeeded(p, true);

    if (p->recorder.raw_file) {
        p->recorder.raw_file.close();
    }
    if (p->recorder.meta_file) {
        p->recorder.meta_file.close();
    }
    if (p->recorder.event_file) {
        p->recorder.event_file.close();
    }
    if (p->recorder.feature_file) {
        p->recorder.feature_file.close();
    }

    p->recorder.session_open = false;
    p->recorder.raw_dirty = false;
    p->recorder.meta_dirty = false;
    p->recorder.event_dirty = false;
    p->recorder.feature_dirty = false;
}

bool T20_recorderWriteBinaryHeader(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->recorder.session_open || !p->recorder.raw_file) {
        return false;
    }

    size_t wrote = p->recorder.raw_file.write(
        reinterpret_cast<const uint8_t*>(&p->recorder.raw_header),
        sizeof(p->recorder.raw_header)
    );

    p->recorder.raw_dirty = true;
    return (wrote == sizeof(p->recorder.raw_header));
}

bool T20_recorderWriteConfig(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg)
{
    if (p == nullptr || p_cfg == nullptr || !p->cfg.recorder.enable || !p->recorder.session_open) {
        return false;
    }

    File f = SD_MMC.open(p->recorder.cfg_path, FILE_WRITE);
    if (!f) {
        return false;
    }

    f.println("{");
    f.printf("  \"version\": \"%s\",\n", G_T20_VERSION_STR);
    f.printf("  \"frame_size\": %u,\n", p_cfg->feature.frame_size);
    f.printf("  \"hop_size\": %u,\n", p_cfg->feature.hop_size);
    f.printf("  \"sample_rate_hz\": %.1f,\n", p_cfg->feature.sample_rate_hz);
    f.printf("  \"mel_filters\": %u,\n", p_cfg->feature.mel_filters);
    f.printf("  \"mfcc_coeffs\": %u,\n", p_cfg->feature.mfcc_coeffs);
    f.printf("  \"delta_window\": %u,\n", p_cfg->feature.delta_window);
    f.printf("  \"batch_count\": %u,\n", p_cfg->recorder.batch_count);
    f.printf("  \"flush_interval_ms\": %lu,\n", (unsigned long)p_cfg->recorder.flush_interval_ms);
    f.printf("  \"metadata_interval_ms\": %lu\n", (unsigned long)p_cfg->recorder.metadata_interval_ms);
    f.println("}");
    f.close();
    return true;
}

bool T20_recorderWriteMetadataHeartbeat(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->cfg.recorder.enable || !p->recorder.session_open) {
        return false;
    }

    if (!p->recorder.meta_file) {
        return false;
    }

    char line[256];
    if (p->cfg.recorder.metadata_format == EN_T20_REC_META_CSV) {
        snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu,%u,%u",
                 (unsigned long)millis(),
                 (unsigned long)p->recorder.queued_record_count,
                 (unsigned long)p->recorder.written_record_count,
                 (unsigned long)p->recorder.dropped_record_count,
                 (unsigned)p->latest_feature.vector_len,
                 (unsigned)p->seq_rb.feature_dim);
    } else {
        snprintf(line, sizeof(line),
                 "{\"ts_ms\":%lu,\"queued\":%lu,\"written\":%lu,\"dropped\":%lu,\"vector_len\":%u,\"seq_dim\":%u}",
                 (unsigned long)millis(),
                 (unsigned long)p->recorder.queued_record_count,
                 (unsigned long)p->recorder.written_record_count,
                 (unsigned long)p->recorder.dropped_record_count,
                 (unsigned)p->latest_feature.vector_len,
                 (unsigned)p->seq_rb.feature_dim);
    }

    bool ok = T20_writeTextLineToOpenFile(p->recorder.meta_file, line);
    if (ok) {
        p->recorder.meta_dirty = true;
    }
    return ok;
}

bool T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_event_name)
{
    if (p == nullptr || p_event_name == nullptr || !p->cfg.recorder.enable || !p->recorder.session_open) {
        return false;
    }

    if (!p->recorder.event_file) {
        return false;
    }

    char line[192];
    if (p->cfg.recorder.event_format == EN_T20_REC_EVENT_CSV) {
        snprintf(line, sizeof(line), "%lu,%s", (unsigned long)millis(), p_event_name);
    } else {
        snprintf(line, sizeof(line), "{\"ts_ms\":%lu,\"event\":\"%s\"}",
                 (unsigned long)millis(), p_event_name);
    }

    bool ok = T20_writeTextLineToOpenFile(p->recorder.event_file, line);
    if (ok) {
        p->recorder.event_dirty = true;
    }
    return ok;
}

bool T20_recorderWriteRawFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len)
{
    if (p == nullptr || p_frame == nullptr || !p->cfg.recorder.enable || !p->recorder.session_open) {
        return false;
    }

    if (p->cfg.recorder.raw_format == EN_T20_REC_RAW_NONE) {
        return true;
    }

    if (!p->recorder.raw_file) {
        return false;
    }

    bool ok = false;

    if (p->cfg.recorder.raw_format == EN_T20_REC_RAW_BINARY) {
        ok = T20_writeBinaryRawRecordToOpenFile(p->recorder.raw_file, millis(), p_frame, p_len);
    } else {
        for (uint16_t i = 0; i < p_len; ++i) {
            p->recorder.raw_file.print(p_frame[i], 6);
            if (i + 1U < p_len) {
                p->recorder.raw_file.print(',');
            }
        }
        p->recorder.raw_file.println();
        ok = true;
    }

    if (ok) {
        p->recorder.raw_dirty = true;
    }
    return ok;
}

bool T20_recorderWriteFeature(CL_T20_Mfcc::ST_Impl* p, const ST_T20_FeatureVector_t* p_feature)
{
    if (p == nullptr || p_feature == nullptr || !p->cfg.recorder.enable || !p->recorder.session_open) {
        return false;
    }

    if (!p->cfg.recorder.write_feature_vector_csv) {
        return true;
    }

    if (!p->recorder.feature_file) {
        return false;
    }

    for (uint16_t i = 0; i < p_feature->vector_len; ++i) {
        p->recorder.feature_file.print(p_feature->vector[i], 6);
        if (i + 1U < p_feature->vector_len) {
            p->recorder.feature_file.print(',');
        }
    }
    p->recorder.feature_file.println();
    p->recorder.feature_dirty = true;
    return true;
}

void T20_recorderFlushIfNeeded(CL_T20_Mfcc::ST_Impl* p, bool p_force)
{
    if (p == nullptr || !p->recorder.session_open) {
        return;
    }

    uint32_t now_ms = millis();
    bool do_flush = p_force;

    if (!do_flush) {
        uint32_t interval_ms = p->cfg.recorder.flush_interval_ms;
        if (interval_ms == 0U) {
            interval_ms = G_T20_RECORDER_FLUSH_INTERVAL_MS;
        }
        do_flush = ((now_ms - p->recorder.last_flush_ms) >= interval_ms);
    }

    if (!do_flush) {
        return;
    }

    if (p->recorder.raw_dirty && p->recorder.raw_file) {
        p->recorder.raw_file.flush();
        p->recorder.raw_dirty = false;
    }
    if (p->recorder.meta_dirty && p->recorder.meta_file) {
        p->recorder.meta_file.flush();
        p->recorder.meta_dirty = false;
    }
    if (p->recorder.event_dirty && p->recorder.event_file) {
        p->recorder.event_file.flush();
        p->recorder.event_dirty = false;
    }
    if (p->recorder.feature_dirty && p->recorder.feature_file) {
        p->recorder.feature_file.flush();
        p->recorder.feature_dirty = false;
    }

    p->recorder.last_flush_ms = now_ms;
}

bool T20_recorderEnqueueEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_event_name)
{
    if (p == nullptr || p_event_name == nullptr || p->recorder_queue == nullptr || !p->cfg.recorder.enable) {
        return false;
    }

    ST_T20_RecorderQueueItem_t item;
    memset(&item, 0, sizeof(item));
    item.record_type = EN_T20_REC_RECORD_EVENT;
    item.timestamp_ms = millis();
    strncpy(item.data.event_rec.text, p_event_name, sizeof(item.data.event_rec.text) - 1);

    if (xQueueSend(p->recorder_queue, &item, 0) == pdTRUE) {
        p->recorder.queued_record_count++;
        return true;
    }

    p->recorder.dropped_record_count++;
    return false;
}

bool T20_recorderEnqueueRawFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len)
{
    if (p == nullptr || p_frame == nullptr || p->recorder_queue == nullptr || !p->cfg.recorder.enable) {
        return false;
    }

    if (p_len == 0 || p_len > G_T20_RECORDER_RAW_MAX_SAMPLES) {
        return false;
    }

    ST_T20_RecorderQueueItem_t item;
    memset(&item, 0, sizeof(item));
    item.record_type = EN_T20_REC_RECORD_RAW_FRAME;
    item.timestamp_ms = millis();
    item.data.raw_rec.sample_count = p_len;
    memcpy(item.data.raw_rec.samples, p_frame, sizeof(float) * p_len);

    if (xQueueSend(p->recorder_queue, &item, 0) == pdTRUE) {
        p->recorder.queued_record_count++;
        return true;
    }

    p->recorder.dropped_record_count++;
    return false;
}

bool T20_recorderEnqueueFeature(CL_T20_Mfcc::ST_Impl* p, const ST_T20_FeatureVector_t* p_feature)
{
    if (p == nullptr || p_feature == nullptr || p->recorder_queue == nullptr || !p->cfg.recorder.enable) {
        return false;
    }

    ST_T20_RecorderQueueItem_t item;
    memset(&item, 0, sizeof(item));
    item.record_type = EN_T20_REC_RECORD_FEATURE;
    item.timestamp_ms = millis();
    item.data.feature_rec.feature = *p_feature;

    if (xQueueSend(p->recorder_queue, &item, 0) == pdTRUE) {
        p->recorder.queued_record_count++;
        return true;
    }

    p->recorder.dropped_record_count++;
    return false;
}

void T20_recorderTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    ST_T20_RecorderQueueItem_t item;

    for (;;) {
        if (p == nullptr || p->recorder_queue == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint32_t batch_count = p->cfg.recorder.batch_count;
        if (batch_count == 0U) {
            batch_count = G_T20_RECORDER_BATCH_COUNT;
        }

        if (xQueueReceive(p->recorder_queue, &item, pdMS_TO_TICKS(200)) == pdTRUE) {
            uint32_t processed = 0;

            do {
                if (p->cfg.recorder.enable && p->recorder.session_open) {
                    bool ok = false;

                    switch (item.record_type) {
                        case EN_T20_REC_RECORD_EVENT:
                            ok = T20_recorderWriteEvent(p, item.data.event_rec.text);
                            break;

                        case EN_T20_REC_RECORD_RAW_FRAME:
                            ok = T20_recorderWriteRawFrame(p,
                                                           item.data.raw_rec.samples,
                                                           item.data.raw_rec.sample_count);
                            break;

                        case EN_T20_REC_RECORD_FEATURE:
                            ok = T20_recorderWriteFeature(p, &item.data.feature_rec.feature);
                            break;

                        default:
                            ok = false;
                            break;
                    }

                    if (ok) {
                        p->recorder.written_record_count++;
                    }
                }

                processed++;
                if (processed >= batch_count) {
                    break;
                }
            } while (xQueueReceive(p->recorder_queue, &item, 0) == pdTRUE);
        }

        uint32_t meta_interval = p->cfg.recorder.metadata_interval_ms;
        if (meta_interval == 0U) {
            meta_interval = G_T20_RECORDER_METADATA_INTERVAL_MS;
        }

        if (p->cfg.recorder.enable &&
            p->recorder.session_open &&
            (millis() - p->recorder.last_metadata_ms) >= meta_interval) {
            if (T20_recorderWriteMetadataHeartbeat(p)) {
                p->recorder.last_metadata_ms = millis();
            }
        }

        T20_recorderFlushIfNeeded(p, false);
    }
}


bool T20_recorderEnsureOpen(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->recorder.session_open) {
        return false;
    }

    if (!p->recorder.raw_file) {
        p->recorder.raw_file = SD_MMC.open(p->recorder.raw_path, FILE_APPEND);
        if (!p->recorder.raw_file) {
            return false;
        }
    }
    return true;
}

bool T20_recorderMaybeRotate(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->recorder.session_open) {
        return false;
    }

    if (!SD_MMC.exists(p->recorder.raw_path)) {
        return true;
    }

    File v_file = SD_MMC.open(p->recorder.raw_path, FILE_READ);
    if (!v_file) {
        return false;
    }
    size_t v_size = v_file.size();
    v_file.close();

    if (v_size < G_T20_RECORDER_ROTATE_SIZE_BYTES) {
        return true;
    }

    // 안정성 우선 단계:
    // - raw 파일만 단순 rotate
    // - 향후 단계 TODO:
    //   - meta/event/feature 연동 rotate
    //   - rotate index 관리 고도화
    char v_new_path[192] = {0};
    snprintf(v_new_path, sizeof(v_new_path), "%s.rot", p->recorder.raw_path);

    if (p->recorder.raw_file) {
        p->recorder.raw_file.flush();
        p->recorder.raw_file.close();
    }

    SD_MMC.remove(v_new_path);
    SD_MMC.rename(p->recorder.raw_path, v_new_path);

    p->recorder.raw_file = SD_MMC.open(p->recorder.raw_path, FILE_WRITE);
    return (bool)p->recorder.raw_file;
}
