#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "moji_face.h"

#define TAG "audio"

static esp_codec_dev_handle_t s_mic, s_spk;
static bool s_mic_open, s_spk_open;
static int s_mic_ch = 2;
static int s_volume = 72;
static int16_t s_tmp[2048];

int audio_init(void)
{
    ESP_LOGI(TAG, "speaker codec init");
    s_spk = bsp_audio_codec_speaker_init();
    ESP_LOGI(TAG, "mic codec init");
    s_mic = bsp_audio_codec_microphone_init();
    if (!s_spk || !s_mic) {
        ESP_LOGE(TAG, "codec init failed: spk=%p mic=%p", s_spk, s_mic);
        return -1;
    }
    esp_codec_dev_set_out_vol(s_spk, s_volume);
    return 0;
}

void audio_set_volume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume = percent;
    if (s_spk) esp_codec_dev_set_out_vol(s_spk, s_volume);
}

static void spk_close(void)
{
    if (s_spk_open) {
        esp_codec_dev_close(s_spk);
        s_spk_open = false;
    }
}

static void mic_close(void)
{
    if (s_mic_open) {
        esp_codec_dev_close(s_mic);
        s_mic_open = false;
    }
}

void audio_stop(void)
{
    face_set_talking(0);
    spk_close();
}

int audio_mic_start(void)
{
    if (s_mic_open) return 0;
    spk_close();
    vTaskDelay(pdMS_TO_TICKS(20));

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    esp_err_t rc = esp_codec_dev_open(s_mic, &fs);
    s_mic_ch = 2;
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "mic stereo open failed (%s), trying mono", esp_err_to_name(rc));
        fs.channel = 1;
        rc = esp_codec_dev_open(s_mic, &fs);
        s_mic_ch = 1;
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "mic open failed: %s", esp_err_to_name(rc));
        return -1;
    }
    esp_codec_dev_set_in_gain(s_mic, 30.0f);
    s_mic_open = true;

    // Let the ADC, HPF and DMA settle.
    int16_t junk[320 * 2];
    for (int i = 0; i < 8; i++) {
        esp_codec_dev_read(s_mic, junk, sizeof junk);
    }
    return 0;
}

void audio_mic_stop(void)
{
    mic_close();
}

int audio_mic_read(int16_t *mono, int frames)
{
    if (!s_mic_open) return -1;
    int done = 0;
    while (done < frames) {
        int want = frames - done;
        if (want > 512) want = 512;
        size_t bytes = (size_t)want * s_mic_ch * sizeof(int16_t);
        if (esp_codec_dev_read(s_mic, s_tmp, bytes) != ESP_OK) return -2;
        if (s_mic_ch == 1) {
            memcpy(mono + done, s_tmp, bytes);
        } else {
            for (int i = 0; i < want; i++) mono[done + i] = s_tmp[2 * i];
        }
        done += want;
    }
    return 0;
}

int audio_play(const int16_t *pcm, int n)
{
    if (!s_spk || !pcm || n <= 0) return -1;
    mic_close();
    vTaskDelay(pdMS_TO_TICKS(20));

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 24000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    esp_err_t rc = esp_codec_dev_open(s_spk, &fs);
    int ch = 1;
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "speaker mono open failed (%s), trying stereo", esp_err_to_name(rc));
        fs.channel = 2;
        rc = esp_codec_dev_open(s_spk, &fs);
        ch = 2;
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "speaker open failed: %s", esp_err_to_name(rc));
        return -2;
    }
    s_spk_open = true;
    esp_codec_dev_set_out_mute(s_spk, false);
    esp_codec_dev_set_out_vol(s_spk, s_volume);

    int16_t buf[512 * 2];
    for (int off = 0; off < n; off += 512) {
        int cnt = n - off > 512 ? 512 : n - off;
        int32_t acc = 0;
        if (ch == 1) {
            memcpy(buf, pcm + off, (size_t)cnt * 2);
            for (int i = 0; i < cnt; i++) acc += abs(pcm[off + i]);
        } else {
            for (int i = 0; i < cnt; i++) {
                buf[2 * i] = pcm[off + i];
                buf[2 * i + 1] = pcm[off + i];
                acc += abs(pcm[off + i]);
            }
        }
        float amp = cnt ? (float)acc / cnt / 32768.0f : 0;
        face_set_talking(fminf(amp * 3.2f, 1.0f));
        esp_codec_dev_write(s_spk, buf, (size_t)cnt * ch * 2);
    }
    face_set_talking(0);
    vTaskDelay(pdMS_TO_TICKS(60));
    spk_close();
    return 0;
}
