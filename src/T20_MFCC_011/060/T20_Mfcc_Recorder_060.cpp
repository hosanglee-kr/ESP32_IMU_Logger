#include "T20_Mfcc_Inter_060.h"

uint8_t T20_selectRecorderWriteBufferSlot(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return 0;
    uint8_t slot = p->recorder_zero_copy_slot_index;
    p->recorder_zero_copy_slot_index = (uint8_t)((p->recorder_zero_copy_slot_index + 1U) % G_T20_RECORDER_ZERO_COPY_SLOT_MAX);
    return slot;
}

void T20_recorderFlushIfNeeded(CL_T20_Mfcc::ST_Impl* p, bool p_force)
{
    if (p == nullptr) return;
    if (p_force) p->recorder_flush_requested = true;
    if (p->recorder_batch_count >= G_T20_RECORDER_BATCH_FLUSH_RECORDS) {
        p->recorder_flush_requested = true;
    }
}

void T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text)
{
    if (p == nullptr) return;
    if (p_text == nullptr) p_text = "";
    strlcpy(p->recorder_last_error, p_text, sizeof(p->recorder_last_error));
}

void T20_initSdmmcProfiles(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    memset(p->sdmmc_profiles, 0, sizeof(p->sdmmc_profiles));

    strlcpy(p->sdmmc_profiles[0].profile_name, "default-1bit", sizeof(p->sdmmc_profiles[0].profile_name));
    p->sdmmc_profiles[0].enabled = true;
    p->sdmmc_profiles[0].use_1bit_mode = true;

    strlcpy(p->sdmmc_profiles[1].profile_name, "esp32s3-slot-a", sizeof(p->sdmmc_profiles[1].profile_name));
    p->sdmmc_profiles[1].enabled = true;
    p->sdmmc_profiles[1].use_1bit_mode = true;
    p->sdmmc_profiles[1].clk_pin = 36;
    p->sdmmc_profiles[1].cmd_pin = 35;
    p->sdmmc_profiles[1].d0_pin = 37;

    strlcpy(p->sdmmc_profiles[2].profile_name, "esp32s3-slot-b", sizeof(p->sdmmc_profiles[2].profile_name));
    p->sdmmc_profiles[2].enabled = true;
    p->sdmmc_profiles[2].use_1bit_mode = false;
    p->sdmmc_profiles[2].clk_pin = 14;
    p->sdmmc_profiles[2].cmd_pin = 15;
    p->sdmmc_profiles[2].d0_pin = 2;
    p->sdmmc_profiles[2].d1_pin = 4;
    p->sdmmc_profiles[2].d2_pin = 12;
    p->sdmmc_profiles[2].d3_pin = 13;

    p->sdmmc_profile = p->sdmmc_profiles[0];
}

bool T20_applySdmmcProfileByName(CL_T20_Mfcc::ST_Impl* p, const char* p_name)
{
    if (p == nullptr || p_name == nullptr) return false;
    for (uint16_t i = 0; i < G_T20_SDMMC_PROFILE_PRESET_COUNT; ++i) {
        if (p->sdmmc_profiles[i].enabled && strcmp(p->sdmmc_profiles[i].profile_name, p_name) == 0) {
            p->sdmmc_profile = p->sdmmc_profiles[i];
            return true;
        }
    }
    return false;
}

bool T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    if (p->recorder_sdmmc_mounted) return true;

    bool one_bit = p->sdmmc_profile.use_1bit_mode;
    bool ok = SD_MMC.begin(G_T20_SDMMC_MOUNT_PATH_DEFAULT, one_bit);
    if (!ok) {
        T20_recorderSetLastError(p, "sdmmc mount failed");
        p->recorder_sdmmc_mounted = false;
        return false;
    }

    p->recorder_sdmmc_mounted = true;
    p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;
    p->recorder_enabled = true;
    strlcpy(p->recorder_sdmmc_mount_path, G_T20_SDMMC_MOUNT_PATH_DEFAULT, sizeof(p->recorder_sdmmc_mount_path));
    strlcpy(p->recorder_sdmmc_board_hint, p->sdmmc_profile.profile_name, sizeof(p->recorder_sdmmc_board_hint));
    return true;
}

void T20_unmountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    if (p->recorder_sdmmc_mounted) {
        SD_MMC.end();
        p->recorder_sdmmc_mounted = false;
        p->recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;
    }
}

File T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend, const char* p_path, const char* p_mode)
{
    if (p_path == nullptr || p_mode == nullptr) return File();
    if (p_backend == EN_T20_STORAGE_LITTLEFS) return LittleFS.open(p_path, p_mode);
    return SD_MMC.open(p_path, p_mode);
}

bool T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg)
{
    if (!p_file || p_cfg == nullptr) return false;

    ST_T20_RecorderBinaryHeader_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic           = G_T20_BINARY_MAGIC;
    hdr.version         = G_T20_BINARY_VERSION;
    hdr.header_size     = sizeof(ST_T20_RecorderBinaryHeader_t);
    hdr.sample_rate_hz  = (uint32_t)p_cfg->feature.sample_rate_hz;
    hdr.fft_size        = p_cfg->feature.fft_size;
    hdr.mfcc_dim        = p_cfg->feature.mfcc_coeffs;
    hdr.mel_filters     = p_cfg->feature.mel_filters;
    hdr.sequence_frames = p_cfg->output.sequence_frames;
    hdr.record_count    = 0;

    size_t written = p_file.write((const uint8_t*)&hdr, sizeof(hdr));
    return (written == sizeof(hdr));
}

bool T20_recorderOpenIfNeeded(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    if (p->recorder_file_opened) return true;

    if (p->recorder_file_path[0] == 0) {
        strlcpy(p->recorder_file_path, G_T20_RECORDER_DEFAULT_FILE_PATH, sizeof(p->recorder_file_path));
    }

    File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_file_path, "r");
    bool exists = (bool)file;
    if (file) file.close();

    file = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_file_path, exists ? "a" : "w");
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
    p->recorder_file_opened = true;
    return true;
}

bool T20_recorderAppendVector(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg)
{
    if (p == nullptr || p_msg == nullptr) return false;
    if (!T20_recorderOpenIfNeeded(p)) return false;

    File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_file_path, "a");
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

    p->recorder_record_count++;
    return true;
}

bool T20_recorderBatchPush(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg)
{
    if (p == nullptr || p_msg == nullptr) return false;
    if (p->recorder_batch_count >= G_T20_RECORDER_BATCH_VECTOR_MAX) {
        if (!T20_recorderBatchFlush(p)) return false;
    }

    p->recorder_batch_vectors[p->recorder_batch_count] = *p_msg;
    p->recorder_batch_count++;
    T20_recorderFlushIfNeeded(p, false);
    return true;
}

bool T20_recorderBatchFlush(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    for (uint16_t i = 0; i < p->recorder_batch_count; ++i) {
        if (!T20_recorderAppendVector(p, &p->recorder_batch_vectors[i])) {
            return false;
        }
    }
    p->recorder_batch_count = 0;
    return true;
}

bool T20_recorderFlushNow(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    bool ok = T20_recorderBatchFlush(p);
    p->recorder_flush_requested = false;
    return ok;
}

bool T20_saveRecorderIndex(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;

    File file = LittleFS.open(G_T20_RECORDER_INDEX_FILE_PATH, "w");
    if (!file) return false;

    JsonDocument doc;
    doc["count"] = p->recorder_index_count;
    doc["active_profile"] = p->sdmmc_profile.profile_name;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (uint16_t i = 0; i < p->recorder_index_count && i < G_T20_RECORDER_MAX_ROTATE_LIST; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["path"] = p->recorder_index_items[i].path;
        o["size_bytes"] = p->recorder_index_items[i].size_bytes;
        o["created_ms"] = p->recorder_index_items[i].created_ms;
        o["record_count"] = p->recorder_index_items[i].record_count;
    }

    serializeJson(doc, file);
    file.close();
    return true;
}

void T20_recorderTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
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
                strlcpy(p->recorder_index_items[0].path, p->recorder_file_path, sizeof(p->recorder_index_items[0].path));
                p->recorder_index_items[0].created_ms = millis();
            }

            p->recorder_index_items[0].record_count = p->recorder_record_count + p->recorder_batch_count;

            File f = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_file_path, "r");
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
                T20_recorderFlushNow(p);
                T20_saveRecorderIndex(p);
            }
        }
    }
}
