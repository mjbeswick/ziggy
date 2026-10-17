// mojitalk -- GERTY on a Waveshare ESP32-S3 1.75" round AMOLED.
//
// Offline speech in, offline speech out, and a vector emoticon face that
// reacts.  Built on BabyTalk (Citrinet-256 on MMRT, sanoTTS nano voice).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alarm.h"
#include "audio.h"
#include "display.h"
#include "brain.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "math_eval.h"
#include "moji_face.h"
#include "rtc.h"
#include "shot.h"
#include "stt_engine.h"
#include "tts_engine.h"
#include "ui.h"

#define TAG "bumble"

#define RATE 16000
#define CHUNK 320           // 20 ms
#define PREROLL 15          // 300 ms before speech
#define MAX_SECONDS 10
#define MAX_SAMPLES (RATE * MAX_SECONDS)

static volatile bool s_tap;
static volatile bool s_say_req;
static char s_say_text[220];
static volatile int s_say_emo = EMO_HAPPY;
static volatile int s_emo_req = -1;
static volatile bool s_hold_emo;
static volatile int s_rec_req;      // seconds, 0 = none

static void tap_cb(void) { s_tap = true; }

static float chunk_rms(const int16_t *pcm, int n)
{
    int64_t acc = 0;
    for (int i = 0; i < n; i++) acc += (int32_t)pcm[i] * pcm[i];
    return n ? sqrtf((float)acc / n) / 32768.0f : 0;
}

static void speak_text(const char *text, emo_t emo, float intensity, int hold_ms);

static void chime(void)
{
    const int rate = 24000;
    const int beep = rate * 15 / 100;     // 150 ms
    const int gap = rate * 7 / 100;       // 70 ms
    int n = 3 * (beep + gap);
    int16_t *pcm = heap_caps_malloc((size_t)n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!pcm) return;
    int off = 0;
    for (int b = 0; b < 3; b++) {
        float f = b == 2 ? 1320.0f : 880.0f;
        for (int i = 0; i < beep; i++) {
            pcm[off + i] = (int16_t)(11000 * sinf(2.0f * (float)M_PI * f * i / rate));
        }
        off += beep;
        memset(pcm + off, 0, (size_t)gap * sizeof(int16_t));
        off += gap;
    }
    audio_play(pcm, n);
    heap_caps_free(pcm);
}

static void ring_alarm(void)
{
    audio_mic_stop();
    ui_status("ALARM");
    face_set_emotion(EMO_EXCITED, 1.0f);
    chime();
    int y, mo, d, h, mi, sec;
    char msg[96];
    if (alarm_is_timer()) {
        snprintf(msg, sizeof msg, "Your timer is done.");
    } else if (board_rtc_read(&y, &mo, &d, &h, &mi, &sec)) {
        int hh = h % 12;
        if (hh == 0) hh = 12;
        snprintf(msg, sizeof msg, "It's %d:%02d %s. Time to get up, Michael.",
                 hh, mi, h >= 12 ? "pm" : "am");
    } else {
        snprintf(msg, sizeof msg, "Your alarm is going off.");
    }
    speak_text(msg, EMO_EXCITED, 1.0f, 1200);
    ui_caption("");
}

static void speak_text(const char *text, emo_t emo, float intensity, int hold_ms)
{
    face_set_emotion(emo, intensity);
    ui_status("SPEAKING");
    ui_caption(text);

    int16_t *pcm = NULL;
    int n = 0;
    tts_stats_t st = {0};
    int rc = tts_say(text, 0.9f, &pcm, &n, &st);
    if (rc == 0 && pcm && n > 0) {
        float seconds = (float)n / TTS_SAMPLE_RATE;
        ESP_LOGI(TAG, "tts %d samples (%.2fs) synth %.0f ms rtf %.2f", n, seconds,
                 st.synth_ms, seconds > 0 ? st.synth_ms / 1000.0f / seconds : 0);
        audio_play(pcm, n);
        heap_caps_free(pcm);
    } else {
        ESP_LOGW(TAG, "tts failed rc=%d", rc);
        vTaskDelay(pdMS_TO_TICKS(hold_ms));
    }
    face_set_talking(0);
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
}

static void console_handle(char *line)
{
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    ESP_LOGI(TAG, "cmd: %s", line);
    if (!strcmp(line, "shot")) {
        shot_take();
    } else if (!strcmp(line, "rec")) {
        s_rec_req = 4;
    } else if (!strncmp(line, "math ", 5)) {
        char out[400];
        if (math_say(line + 5, out, sizeof out)) {
            printf("math: %s\n", out);
            snprintf(s_say_text, sizeof s_say_text, "%.*s", (int)sizeof(s_say_text) - 1, out);
            s_say_emo = EMO_HAPPY;
            s_say_req = true;
        } else {
            printf("math: not a sum\n");
        }
        fflush(stdout);
    } else if (!strncmp(line, "think ", 6)) {
        brain_reply_t r = brain_think(line + 6);
        printf("reply [%s]: %s\n", emo_name(r.emotion), r.reply ? r.reply : "");
        fflush(stdout);
        if (r.reply && r.reply[0]) {
            snprintf(s_say_text, sizeof s_say_text, "%.*s", (int)sizeof(s_say_text) - 1, r.reply);
            s_say_emo = r.emotion;
            s_say_req = true;
        }
    } else if (!strncmp(line, "say ", 4)) {
        snprintf(s_say_text, sizeof s_say_text, "%.*s", (int)sizeof(s_say_text) - 1, line + 4);
        s_say_req = true;
    } else if (!strncmp(line, "emo ", 4)) {
        s_emo_req = atoi(line + 4);
    } else if (!strncmp(line, "hold ", 5)) {
        int e = atoi(line + 5);
        if (e >= 0 && e < EMO_COUNT) {
            s_hold_emo = true;
            face_set_emotion((emo_t)e, 1.0f);
        } else {
            s_hold_emo = false;
        }
    } else if (!strncmp(line, "vol ", 4)) {
        ui_set_volume(atoi(line + 4));
    } else if (!strncmp(line, "lum ", 4)) {
        ui_set_brightness(atoi(line + 4));
    } else if (!strcmp(line, "panel")) {
        bsp_display_lock(-1);
        ui_show_controls(!ui_controls_visible());
        bsp_display_unlock();
    } else if (!strcmp(line, "info")) {
        stt_info_t si;
        stt_engine_info(&si);
        printf("internal free %u, psram free %u, model %u bytes, cache %u\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               (unsigned)si.model_bytes, (unsigned)si.cache_bytes);
        fflush(stdout);
    } else if (!strcmp(line, "help")) {
        printf("shot | panel | rec | math <sum> | think <text> | say <text> | hold <0-9> | vol <0-100> | lum <15-100> | info\n");
        fflush(stdout);
    }
}

// The VFS stdin for the USB Serial/JTAG console reports EOF on this build, so
// read the peripheral directly and assemble lines ourselves.
static void console_task(void *arg)
{
    (void)arg;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&cfg);

    printf("\nbumble console: help | shot | panel | rec | say <text> | emo <0-9> | vol <0-100> | info\n");
    fflush(stdout);

    char line[256];
    size_t len = 0;
    uint8_t buf[64];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (len) {
                    line[len] = 0;
                    console_handle(line);
                    len = 0;
                }
            } else if (len + 1 < sizeof line) {
                line[len++] = c;
            }
        }
    }
}

static const char *WAKE_ALIASES[] = {"bumble", "bumbel", "bumbl", "bumbo", "bunble", "bamble", "bomble", "bimbel", NULL};

static bool has_wake(const char *text)
{
    for (int i = 0; WAKE_ALIASES[i]; i++) {
        if (strstr(text, WAKE_ALIASES[i])) return true;
    }
    return false;
}

// Copy `text` with the wake words removed.  "hey bumble, what time is it"
// becomes "what time is it"; "what time is it, bumble" works too.
static void strip_wake(const char *text, char *out, size_t n)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%.1023s", text);
    for (int i = 0; WAKE_ALIASES[i]; i++) {
        char *p;
        while ((p = strstr(tmp, WAKE_ALIASES[i])) != NULL) {
            memset(p, ' ', strlen(WAKE_ALIASES[i]));
        }
    }
    char *p;
    while ((p = strstr(tmp, "hey")) != NULL) memset(p, ' ', 3);
    // Speech-to-text often drops the y: "he bumble".
    if (!strncmp(tmp, "he ", 3)) memset(tmp, ' ', 3);
    size_t j = 0;
    bool space = true;
    for (size_t i = 0; tmp[i] && j + 1 < n; i++) {
        char c = tmp[i];
        if (c == ' ' || c == ',' || c == '.' || c == '?' || c == '!') {
            if (!space) out[j++] = ' ';
            space = true;
        } else {
            out[j++] = c;
            space = false;
        }
    }
    while (j && out[j - 1] == ' ') j--;
    out[j] = 0;
}

static void app_task(void *arg)
{
    (void)arg;

    int16_t *cap = heap_caps_malloc(MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    static int16_t pre[PREROLL][CHUNK];
    if (!cap) {
        ESP_LOGE(TAG, "no PSRAM for the capture buffer");
        vTaskDelete(NULL);
    }

    ui_status("BOOT");
    face_set_emotion(EMO_THINKING, 1.0f);
    vTaskDelay(pdMS_TO_TICKS(600));

    int y, mo, d, h, mi, sec;
    bool have_clock = board_rtc_read(&y, &mo, &d, &h, &mi, &sec);
    const char *greet = !have_clock ? "Bumble online. Good to see you, Michael."
                        : (h < 12 ? "Good morning, Michael. Bumble online."
                                  : (h < 18 ? "Good afternoon, Michael. Bumble online."
                                            : "Good evening, Michael. Bumble online."));
    speak_text(greet, EMO_HAPPY, 1.0f, 900);
    face_set_emotion(EMO_NEUTRAL, 1.0f);
    ui_caption("");

    int pre_i = 0;
    int64_t last_voice = esp_timer_get_time();
    int64_t followup_until = 0;
    int64_t next_idle_comment = esp_timer_get_time() + 150000000LL;
    int64_t next_alarm_check = 0;

    for (;;) {
        if (s_hold_emo) {
            ui_status(emo_name(face_emotion()));
            last_voice = esp_timer_get_time();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (s_emo_req >= 0) {
            int e = s_emo_req;
            s_emo_req = -1;
            if (e >= 0 && e < EMO_COUNT) face_set_emotion((emo_t)e, 1.0f);
        }
        if (s_say_req) {
            s_say_req = false;
            audio_mic_stop();
            speak_text(s_say_text, (emo_t)s_say_emo, 1.0f, 400);
        }

        bool sleeping = (esp_timer_get_time() - last_voice) > 300000000LL;
        ui_status(sleeping ? "SLEEPING" : "STANDBY");
        if (!s_hold_emo) face_set_emotion(sleeping ? EMO_SLEEPY : EMO_NEUTRAL, 1.0f);
        face_set_level(0);
        if (audio_mic_start() != 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // --- wait for speech -------------------------------------------------
        float noise = 0.003f;
        int above = 0;
        bool go = false;
        bool forced = false;
        int force_frames = 0;
        while (!go) {
            if (s_hold_emo) break;
            if (audio_mic_read(pre[pre_i], CHUNK) != 0) break;
            float rms = chunk_rms(pre[pre_i], CHUNK);
            pre_i = (pre_i + 1) % PREROLL;

            if (rms < noise) noise = noise * 0.92f + rms * 0.08f;
            else noise = noise * 0.9995f + 0.00002f;
            if (noise < 0.0005f) noise = 0.0005f;
            if (noise > 0.02f) noise = 0.02f;
            float thr = noise * 3.0f + 0.008f;

            int64_t now_us = esp_timer_get_time();
            if (now_us > next_alarm_check) {
                next_alarm_check = now_us + 500000;
                if (alarm_poll()) {
                    ring_alarm();
                    last_voice = esp_timer_get_time();
                    break;
                }
            }
            int64_t idle_us = now_us - last_voice;
            if (now_us > next_idle_comment && idle_us > 150000000LL && idle_us < 290000000LL) {
                next_idle_comment = now_us + 150000000LL;
                brain_reply_t ir = brain_idle_line((int)(idle_us / 1000000));
                if (ir.reply && ir.reply[0]) {
                    audio_mic_stop();
                    speak_text(ir.reply, ir.emotion, 1.0f, 600);
                    last_voice = esp_timer_get_time();
                    break;
                }
            }

            if (s_rec_req) {
                forced = true;
                force_frames = s_rec_req * RATE;
                s_rec_req = 0;
                go = true;
            } else if (rms > thr) {
                if (++above >= 3) go = true;
            } else {
                above = 0;
            }
            if (s_tap) {
                s_tap = false;
                if (sleeping) {
                    face_set_emotion(EMO_SURPRISED, 1.0f);
                    vTaskDelay(pdMS_TO_TICKS(250));
                    speak_text("I'm awake. I'm awake.", EMO_HAPPY, 1.0f, 500);
                    last_voice = esp_timer_get_time();
                } else {
                    face_poke();
                }
            }
        }
        if (!go) {
            audio_mic_stop();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // --- capture ---------------------------------------------------------
        ui_status("LISTENING");
        face_set_emotion(EMO_LISTENING, 1.0f);
        int n = 0;
        for (int i = 0; i < PREROLL; i++) {
            memcpy(cap + n, pre[(pre_i + i) % PREROLL], CHUNK * sizeof(int16_t));
            n += CHUNK;
        }
        int silence = 0;
        while (n < MAX_SAMPLES) {
            if (s_hold_emo) break;
            if (forced && n >= force_frames) break;
            int64_t cap_us = esp_timer_get_time();
            if (cap_us > next_alarm_check) {
                next_alarm_check = cap_us + 500000;
                if (alarm_poll()) {
                    audio_mic_stop();
                    ring_alarm();
                    last_voice = esp_timer_get_time();
                    n = 0;
                    break;
                }
            }
            if (audio_mic_read(cap + n, CHUNK) != 0) break;
            float rms = chunk_rms(cap + n, CHUNK);
            face_set_level(fminf(rms * 9.0f, 1.0f));
            n += CHUNK;
            float thr = 0.010f;
            if (rms < thr) silence += CHUNK;
            else silence = 0;
            if (!forced && silence > RATE * 9 / 10 && n > RATE / 2) break;  // 900 ms quiet
            if (s_tap) {                                                // tap ends it early
                s_tap = false;
                break;
            }
        }
        audio_mic_stop();
        face_set_level(0);

        if (n < RATE / 3) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // --- transcribe ------------------------------------------------------
        ui_status("PROCESSING");
        face_set_emotion(EMO_THINKING, 1.0f);
        int64_t t0 = esp_timer_get_time();
        stt_result_t r = {0};
        int rc = stt_engine_run(cap, n, &r);
        int64_t dt = (esp_timer_get_time() - t0) / 1000;
        ESP_LOGI(TAG, "stt %d ms rc=%d: \"%s\"", (int)dt, rc, rc == 0 ? r.text : "");

        if (rc != 0) {
            speak_text("My ears are playing up. Try again.", EMO_CONFUSED, 1.0f, 500);
            continue;
        }
        if (!r.text[0]) {
            speak_text("I didn't catch that. Say it again.", EMO_CONFUSED, 1.0f, 600);
            continue;
        }

        bool woke = has_wake(r.text);
        if (!woke && r.score > -12.0f && r.score < 0) woke = true;
        bool follow = esp_timer_get_time() < followup_until;

        if (!woke && !follow) {
            // Just background chatter.  Stay quiet.
            ui_caption("");
            face_set_emotion(EMO_NEUTRAL, 1.0f);
            continue;
        }

        char cmd[640];
        strip_wake(r.text, cmd, sizeof cmd);
        ui_caption(r.text);
        if (!cmd[0]) {
            face_set_emotion(EMO_HAPPY, 1.0f);
            speak_text("Yes?", EMO_HAPPY, 1.0f, 300);
            followup_until = esp_timer_get_time() + 6000000;
            last_voice = esp_timer_get_time();
            ui_caption("");
            continue;
        }
        followup_until = 0;
        brain_reply_t reply = brain_think(cmd);
        vTaskDelay(pdMS_TO_TICKS(220));
        if (reply.reply && reply.reply[0]) {
            speak_text(reply.reply, reply.emotion, 1.0f, 1500);
        } else {
            face_set_emotion(reply.emotion, 1.0f);
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
        last_voice = esp_timer_get_time();
        next_idle_comment = last_voice + 150000000LL;
        ui_caption("");
    }
}

void app_main(void)
{
    // Init outranks the LVGL renderer so bring-up is not starved by animation.
    vTaskPrioritySet(NULL, 9);

    esp_err_t nerr = nvs_flash_init();
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "booting Bumble");
    ESP_LOGI(TAG, "internal free at start: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // The TTS arena wants one contiguous 84 KB block; take it before anything
    // else fragments internal RAM.  STT evicts it when it needs the pool.
    int rc = tts_reserve();
    ESP_LOGI(TAG, "tts_reserve: %d", rc);

    ESP_LOGI(TAG, "display start");
    lv_display_t *disp = display_start();
    ESP_LOGI(TAG, "display returned %p", disp);
    if (!disp) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    bsp_display_brightness_set(90);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x060606), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    ui_init();
    ui_set_tap_cb(tap_cb);
    ESP_LOGI(TAG, "ui ready");

    if (audio_init() != 0) ESP_LOGE(TAG, "audio init failed");
    ESP_LOGI(TAG, "audio ready");
    if (touch_start(disp) != 0) ESP_LOGW(TAG, "touch unavailable");
    board_rtc_init();
    alarm_init();
    brain_init();
    ESP_LOGI(TAG, "rtc ready");

    rc = stt_engine_open();
    ESP_LOGI(TAG, "stt_engine_open: %d", rc);
    static const char *wake[] = {"bumble", "bumbel", "bumbl", "hey bumble"};
    stt_engine_set_phrase(wake, 4);
    ESP_LOGI(TAG, "internal free after init: %u, psram free: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    xTaskCreatePinnedToCore(console_task, "console", 4096, NULL, 7, NULL, 1);
    xTaskCreatePinnedToCore(app_task, "bumble", 12288, NULL, 6, NULL, 1);
}
