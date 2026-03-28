#include "T20_Mfcc_Inter_058.h"

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
}

void T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text)
{
    if (p == nullptr) return;
    if (p_text == nullptr) p_text = "";
    strlcpy(p->recorder_last_error, p_text, sizeof(p->recorder_last_error));
}

bool T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    if (p->recorder_sdmmc_mounted) return true;

    bool ok = SD_MMC.begin(G_T20_SDMMC_MOUNT_PATH_DEFAULT, true);
    if (!ok) {
        T20_recorderSetLastError(p, "sdmmc mount failed");
        p->recorder_sdmmc_mounted = false;
        return false;
    }

    p->recorder_sdmmc_mounted = true;
    p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;
    strlcpy(p->recorder_sdmmc_mount_path, G_T20_SDMMC_MOUNT_PATH_DEFAULT, sizeof(p->recorder_sdmmc_mount_path));
    strlcpy(p->recorder_sdmmc_board_hint, "default-1bit", sizeof(p->recorder_sdmmc_board_hint));
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
