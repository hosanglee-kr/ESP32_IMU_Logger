/* ============================================================================
 * File: T330_AudioProc_001.h
 * Version: 1.0 (Full Version)
 * [기능 복원] Python noisereduce의 핵심인 Spectral Gating을 C++로 직접 구현
 * [유형 3 방어] STFT 및 ISTFT (Overlap-Add) 알고리즘 수학적 정합성 확보
 * ========================================================================== */
#pragma once
#include "T310_Config_001.h"
#include <esp_heap_caps.h>
#include <esp_dsp.h>
#include <string.h>
#include <math.h>

class AudioProcessor {
public:
    AudioProcessor() : _rawAudioBuffer(nullptr), _writeIdx(0), _dsp_buf(nullptr) {}

    bool begin() {
        _rawAudioBuffer = (float*)heap_caps_malloc(Config::PSRAM_CACHE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_rawAudioBuffer) return false;
        memset(_rawAudioBuffer, 0, Config::PSRAM_CACHE_SIZE);

        _dsp_buf = (float*)heap_caps_malloc(Config::ANC::N_FFT * 2 * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
        return true;
    }

    void writeToRingBuffer(const float* dma_buf, size_t num_samples) {
        for(size_t i=0; i<num_samples; i++) {
            _rawAudioBuffer[_writeIdx] = dma_buf[i];
            _writeIdx = (_writeIdx + 1) % Config::RAW_BUF_SAMP;
        }
    }

    void extractLinearBuffer(int start_idx, int len, float* out_buf) {
        int real_start = (start_idx % Config::RAW_BUF_SAMP + Config::RAW_BUF_SAMP) % Config::RAW_BUF_SAMP;
        int first_chunk = Config::RAW_BUF_SAMP - real_start;
        if (first_chunk >= len) {
            memcpy(out_buf, &_rawAudioBuffer[real_start], len * sizeof(float));
        } else {
            memcpy(out_buf, &_rawAudioBuffer[real_start], first_chunk * sizeof(float));
            memcpy(out_buf + first_chunk, &_rawAudioBuffer[0], (len - first_chunk) * sizeof(float));
        }
    }

    int getCurrentWriteIdx() const { return _writeIdx; }

    void applyPreEmphasis(float* buf, size_t len) {
        for (int n = len - 1; n > 0; n--) {
            buf[n] = buf[n] - Config::ANC::ALPHA * buf[n-1];
        }
    }

    // [완전 복원] Spectral Gating (noisereduce 알고리즘)
    void processANC(float* signal, float* noise_clip, int lid) {
        float prop_decrease = 1.0f;
        for (const auto& l : Config::LID_TABLE) {
            if (l.lid == lid) { prop_decrease = l.anc_prop; break; }
        }

        int half_fft = Config::ANC::N_FFT / 2;
        float* noise_mean = (float*)malloc(half_fft * sizeof(float));
        float* noise_std = (float*)malloc(half_fft * sizeof(float));
        
        if (!noise_mean || !noise_std) return; // 메모리 할당 실패 방어
        memset(noise_mean, 0, half_fft * sizeof(float));
        memset(noise_std, 0, half_fft * sizeof(float));

        // 1. Noise Profile 생성 (STFT)
        int num_noise_frames = (Config::NOISE_SAMP_LEN - Config::ANC::WIN_LEN) / Config::ANC::HOP_LEN + 1;
        float** noise_mags = (float**)malloc(num_noise_frames * sizeof(float*));
        
        for (int i = 0; i < num_noise_frames; i++) {
            noise_mags[i] = (float*)malloc(half_fft * sizeof(float));
            int offset = i * Config::ANC::HOP_LEN;
            
            memset(_dsp_buf, 0, Config::ANC::N_FFT * 2 * sizeof(float));
            for (int j = 0; j < Config::ANC::WIN_LEN; j++) {
                _dsp_buf[j * 2] = noise_clip[offset + j];
            }
            
            dsps_wind_hann_f32(_dsp_buf, Config::ANC::WIN_LEN);
            dsps_fft2r_fc32(_dsp_buf, Config::ANC::N_FFT);
            dsps_bit_rev_fc32(_dsp_buf, Config::ANC::N_FFT);
            
            for (int k = 0; k < half_fft; k++) {
                float re = _dsp_buf[k*2], im = _dsp_buf[k*2+1];
                noise_mags[i][k] = sqrtf(re*re + im*im);
                noise_mean[k] += noise_mags[i][k];
            }
        }

        for (int k = 0; k < half_fft; k++) noise_mean[k] /= num_noise_frames;
        
        for (int i = 0; i < num_noise_frames; i++) {
            for (int k = 0; k < half_fft; k++) {
                float diff = noise_mags[i][k] - noise_mean[k];
                noise_std[k] += (diff * diff);
            }
            free(noise_mags[i]);
        }
        free(noise_mags);
        for (int k = 0; k < half_fft; k++) noise_std[k] = sqrtf(noise_std[k] / num_noise_frames);

        // 2. Signal STFT -> Gating -> ISTFT (Overlap-Add)
        int num_sig_frames = (Config::VALID_SAMP_LEN - Config::ANC::WIN_LEN) / Config::ANC::HOP_LEN + 1;
        float* out_signal = (float*)calloc(Config::VALID_SAMP_LEN, sizeof(float));

        for (int i = 0; i < num_sig_frames; i++) {
            int offset = i * Config::ANC::HOP_LEN;
            memset(_dsp_buf, 0, Config::ANC::N_FFT * 2 * sizeof(float));
            
            for (int j = 0; j < Config::ANC::WIN_LEN; j++) {
                _dsp_buf[j * 2] = signal[offset + j];
            }
            dsps_wind_hann_f32(_dsp_buf, Config::ANC::WIN_LEN);
            dsps_fft2r_fc32(_dsp_buf, Config::ANC::N_FFT);
            dsps_bit_rev_fc32(_dsp_buf, Config::ANC::N_FFT);

            // Spectral Gating (Mask 적용)
            for (int k = 0; k < half_fft; k++) {
                float re = _dsp_buf[k*2], im = _dsp_buf[k*2+1];
                float mag = sqrtf(re*re + im*im);
                float threshold = noise_mean[k] + Config::ANC::N_STD_THRESH * noise_std[k];
                
                float mask = (mag > threshold) ? 1.0f : (1.0f - prop_decrease);
                
                _dsp_buf[k*2] *= mask;
                _dsp_buf[k*2+1] *= mask;
                // 대칭 복사 (Real IFFT를 위함)
                if (k > 0) {
                    _dsp_buf[(Config::ANC::N_FFT - k)*2] = _dsp_buf[k*2];
                    _dsp_buf[(Config::ANC::N_FFT - k)*2+1] = -_dsp_buf[k*2+1];
                }
            }

            // IFFT (Conjugate -> FFT -> Conjugate -> Scale)
            for (int k=0; k<Config::ANC::N_FFT; k++) _dsp_buf[k*2+1] = -_dsp_buf[k*2+1];
            dsps_fft2r_fc32(_dsp_buf, Config::ANC::N_FFT);
            dsps_bit_rev_fc32(_dsp_buf, Config::ANC::N_FFT);
            for (int k=0; k<Config::ANC::N_FFT; k++) _dsp_buf[k*2+1] = -_dsp_buf[k*2+1];

            // Overlap-Add
            float scale = 1.0f / Config::ANC::N_FFT;
            for (int j = 0; j < Config::ANC::WIN_LEN; j++) {
                if (offset + j < Config::VALID_SAMP_LEN) {
                    out_signal[offset + j] += (_dsp_buf[j * 2] * scale);
                }
            }
        }

        // 결과를 원본 신호에 덮어쓰기 (In-place 대체)
        memcpy(signal, out_signal, Config::VALID_SAMP_LEN * sizeof(float));

        free(out_signal);
        free(noise_mean);
        free(noise_std);
    }

private:
    float* _rawAudioBuffer;
    int _writeIdx;
    float* _dsp_buf;
};
