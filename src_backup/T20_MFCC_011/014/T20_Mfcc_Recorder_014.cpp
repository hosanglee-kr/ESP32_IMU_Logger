#include "T20_Mfcc_Inter_014.h"

#include <SD_MMC.h>
#include <FS.h>

/*
===============================================================================
소스명: T20_Mfcc_Recorder_014.cpp
버전: v014

[기능 스펙]
- SD_MMC recorder 전용 queue/task
- session 디렉터리 생성
- binary raw header 설계/구현
- event/raw/feature 비동기 기록
- config 즉시 기록 지원

[향후 단계 TODO]
- batch flush
- reopen / retry / error recovery
- metadata 주기 저장
- write latency profiling
===============================================================================
*/

static bool T20_writeTextLine(const char* p_path, const char* p_line)
{
    if (p_path == nullptr || p_line == nullptr) {
        return false;
    }

    File f = SD_MMC.open(p_path, FILE_APPEND);
    if (!f) {
        return false;
    }

    f.println(p_line);
    f.close();
    return true;
}

static bool T20_writeBinaryRawRecord(const char* p_path,
                                     uint32_t p_timestamp_ms,
                                     const float* p_samples,
                                     uint16_t p_sample_count)
{
    if (p_path == nullptr || p_samples == nullptr || p_sample_count == 0) {
        return false;
    }

    File f = SD_MMC.open(p_path, FILE_APPEND);
    if (!f) {
        return false;
    }

    ST_T20_RecorderRecordHeader_t hdr;
    hdr.magic = G_T20_RECORDER_RECORD_MAGIC;
    hdr.record_type = (uint16_t)EN_T20_REC_RECORD_RAW_FRAME;
    hdr.payload_bytes = (uint16_t)(sizeof(uint16_t) + sizeof(float) * p_sample_count);
    hdr.timestamp_ms = p_timestamp_ms;

    size_t w1 = f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    size_t w2 = f.write(reinterpret_cast<const uint8_t*>(&p_sample_count), sizeof(p_sample_count));
    size_t w3 = f.write(reinterpret_cast<const uint8_t*>(p_samples), sizeof(float) * p_sample_count);
    f.close();

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

    p->recorder.session_open = true;

    if (p->cfg.recorder.raw_format == EN_T20_REC_RAW_BINARY) {
        if (!T20_recorderWriteBinaryHeader(p)) {
            p->recorder.session_open = false;
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

    p->recorder.session_open = false;
}

bool T20_recorderWriteBinaryHeader(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->recorder.session_open) {
        return false;
    }

    File f = SD_MMC.open(p->recorder.raw_path, FILE_WRITE);
    if (!f) {
        return false;
    }

    size_t wrote = f.write(reinterpret_cast<const uint8_t*>(&p->recorder.raw_header),
                           sizeof(p->recorder.raw_header));
    f.close();

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
    f.printf("  \"delta_window\": %u\n", p_cfg->feature.delta_window);
    f.println("}");
    f.close();
    return true;
}

bool T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_event_name)
{
    if (p == nullptr || p_event_name == nullptr || !p->cfg.recorder.enable || !p->recorder.session_open) {
        return false;
    }

    char line[192];
    if (p->cfg.recorder.event_format == EN_T20_REC_EVENT_CSV) {
        snprintf(line, sizeof(line), "%lu,%s", (unsigned long)millis(), p_event_name);
    } else {
        snprintf(line, sizeof(line), "{\"ts_ms\":%lu,\"event\":\"%s\"}",
                 (unsigned long)millis(), p_event_name);
    }

    return T20_writeTextLine(p->recorder.event_path, line);
}

bool T20_recorderWriteRawFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len)
{
    if (p == nullptr || p_frame == nullptr || !p->cfg.recorder.enable || !p->recorder.session_open) {
        return false;
    }

    if (p->cfg.recorder.raw_format == EN_T20_REC_RAW_NONE) {
        return true;
    }

    if (p->cfg.recorder.raw_format == EN_T20_REC_RAW_BINARY) {
        return T20_writeBinaryRawRecord(p->recorder.raw_path, millis(), p_frame, p_len);
    }

    File f = SD_MMC.open(p->recorder.raw_path, FILE_APPEND);
    if (!f) {
        return false;
    }

    for (uint16_t i = 0; i < p_len; ++i) {
        f.print(p_frame[i], 6);
        if (i + 1U < p_len) {
            f.print(',');
        }
    }
    f.println();
    f.close();
    return true;
}

bool T20_recorderWriteFeature(CL_T20_Mfcc::ST_Impl* p, const ST_T20_FeatureVector_t* p_feature)
{
    if (p == nullptr || p_feature == nullptr || !p->cfg.recorder.enable || !p->recorder.session_open) {
        return false;
    }

    if (!p->cfg.recorder.write_feature_vector_csv) {
        return true;
    }

    File f = SD_MMC.open(p->recorder.feature_path, FILE_APPEND);
    if (!f) {
        return false;
    }

    for (uint16_t i = 0; i < p_feature->vector_len; ++i) {
        f.print(p_feature->vector[i], 6);
        if (i + 1U < p_feature->vector_len) {
            f.print(',');
        }
    }
    f.println();
    f.close();
    return true;
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

    return (xQueueSend(p->recorder_queue, &item, 0) == pdTRUE);
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

    return (xQueueSend(p->recorder_queue, &item, 0) == pdTRUE);
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

    return (xQueueSend(p->recorder_queue, &item, 0) == pdTRUE);
}

void T20_recorderTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    ST_T20_RecorderQueueItem_t item;

    for (;;) {
        if (xQueueReceive(p->recorder_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!p->cfg.recorder.enable || !p->recorder.session_open) {
            continue;
        }

        switch (item.record_type) {
            case EN_T20_REC_RECORD_EVENT:
                (void)T20_recorderWriteEvent(p, item.data.event_rec.text);
                break;

            case EN_T20_REC_RECORD_RAW_FRAME:
                (void)T20_recorderWriteRawFrame(p,
                                               item.data.raw_rec.samples,
                                               item.data.raw_rec.sample_count);
                break;

            case EN_T20_REC_RECORD_FEATURE:
                (void)T20_recorderWriteFeature(p, &item.data.feature_rec.feature);
                break;

            default:
                break;
        }
    }
}
