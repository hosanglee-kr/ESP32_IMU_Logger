#include "T20_Mfcc_Inter_013.h"

#include <SD_MMC.h>
#include <FS.h>

/*
===============================================================================
소스명: T20_Mfcc_Recorder_013.cpp
버전: v013

[기능 스펙]
- SD_MMC recorder 대묶음 C 골격
- session 디렉터리 생성
- config / event / raw / feature 저장 기본형
- 포맷별 파일 경로 준비

[주의]
- 현재 단계는 구조 우선 단계
- 성능 최적화, 비동기 writer task, large buffer batching은 다음 단계 예정

[향후 단계 TODO]
- recorder 전용 queue/task
- binary raw header / chunk flush
- CSV batching
- metadata 주기 기록
- 오류 복구 / 파일 reopen / fs health check
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

    // SD_MMC.end()는 시스템 전체 정책 영향이 커서 여기서는 호출하지 않음.
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

    p->recorder.session_open = true;
    return true;
}

void T20_recorderCloseSession(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    p->recorder.session_open = false;
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
        snprintf(line, sizeof(line), "%lu,%s",
                 (unsigned long)millis(), p_event_name);
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
        File f = SD_MMC.open(p->recorder.raw_path, FILE_APPEND);
        if (!f) {
            return false;
        }
        size_t wrote = f.write(reinterpret_cast<const uint8_t*>(p_frame), sizeof(float) * p_len);
        f.close();
        return (wrote == sizeof(float) * p_len);
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
