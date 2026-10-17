#include "ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "moji_face.h"

#define PANEL_X 83
#define PANEL_Y 200
#define PANEL_W 300
#define PANEL_H 160
#define BAR_X (PANEL_X + 30)
#define BAR_W 240
#define BAR_VOL_Y (PANEL_Y + 62)
#define BAR_LUM_Y (PANEL_Y + 112)

static lv_obj_t *s_brand, *s_status, *s_caption, *s_alarm;
static lv_obj_t *s_panel;
static int s_volume = 72;
static int s_brightness = 90;
static bool s_prefs_dirty;
static int s_drag;                 // 0 none, 1 volume, 2 light
static bool s_dimmed;
static char s_alarm_last[64];
static uint32_t s_touch_ms;
static uint32_t s_change_ms;

static void upper_ascii(char *dst, size_t n, const char *src);

static void prefs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("bumble", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 0;
    if (nvs_get_u8(h, "vol", &v) == ESP_OK && v <= 100) s_volume = v;
    if (nvs_get_u8(h, "lum", &v) == ESP_OK && v >= 15 && v <= 100) s_brightness = v;
    nvs_close(h);
}

static void prefs_save(void)
{
    if (!s_prefs_dirty) return;
    nvs_handle_t h;
    if (nvs_open("bumble", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "vol", (uint8_t)s_volume);
    nvs_set_u8(h, "lum", (uint8_t)s_brightness);
    nvs_commit(h);
    nvs_close(h);
    s_prefs_dirty = false;
    ESP_LOGI("ui", "saved volume %d, light %d", s_volume, s_brightness);
}

void ui_set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_volume = v;
    audio_set_volume(v);
    s_prefs_dirty = true;
    prefs_save();
}

void ui_set_brightness(int v)
{
    if (v < 15) v = 15;
    if (v > 100) v = 100;
    s_brightness = v;
    if (!s_dimmed) bsp_display_brightness_set(v);
    s_prefs_dirty = true;
    prefs_save();
}

void ui_dim(bool dim)
{
    if (dim == s_dimmed) return;
    s_dimmed = dim;
    int b = dim ? (s_brightness * 25 / 100) : s_brightness;
    if (b < 5) b = 5;
    bsp_display_brightness_set(b);
}

void ui_set_alarm(const char *text)
{
    if (!s_alarm) return;
    char buf[64];
    if (text && text[0]) snprintf(buf, sizeof buf, "ALARM %s", text);
    else buf[0] = 0;
    char up[64];
    upper_ascii(up, sizeof up, buf);
    if (!strcmp(up, s_alarm_last)) return;
    snprintf(s_alarm_last, sizeof s_alarm_last, "%s", up);
    bsp_display_lock(-1);
    lv_label_set_text(s_alarm, up);
    bsp_display_unlock();
}
static void upper_ascii(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < n; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : (char)c;
    }
    dst[i] = 0;
}

static void style_label(lv_obj_t *l, const lv_font_t *font, lv_color_t color, int letter_space)
{
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, letter_space, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_scrollable(l, false);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    face_tick(lv_tick_get());
    if (ui_controls_visible() && lv_tick_elaps(s_touch_ms) > 8000) {
        ui_show_controls(false);
    }
    // Persist even if the RELEASED event is missed: save 600 ms after the
    // last change, once the finger is off the fader.
    if (s_prefs_dirty && !s_drag && lv_tick_elaps(s_change_ms) > 600) {
        prefs_save();
    }
}

/* ------------------------------------------------------------- controls */

static void panel_draw(lv_event_t *e)
{
    lv_layer_t *L = lv_event_get_layer(e);
    if (!L) return;

    lv_color_t amber = lv_color_hex(0xFFC24A);
    lv_color_t dim = lv_color_hex(0x8A6A2A);
    lv_color_t ink = lv_color_hex(0x060606);

    lv_area_t box = {PANEL_X, PANEL_Y, PANEL_X + PANEL_W - 1, PANEL_Y + PANEL_H - 1};
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 22;
    d.bg_color = lv_color_hex(0x0B0B0B);
    d.bg_opa = 242;
    d.border_color = dim;
    d.border_width = 1;
    d.border_opa = 220;
    lv_draw_rect(L, &d, &box);

    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = &lv_font_unscii_8;
    ld.color = dim;
    ld.letter_space = 3;
    ld.text_local = 1;

    ld.text = "CONTROLS";
    lv_area_t a = {PANEL_X, PANEL_Y + 14, PANEL_X + PANEL_W, PANEL_Y + 30};
    ld.align = LV_TEXT_ALIGN_CENTER;
    lv_draw_label(L, &ld, &a);

    ld.align = LV_TEXT_ALIGN_LEFT;
    ld.letter_space = 1;
    ld.color = amber;
    char buf[32];
    snprintf(buf, sizeof buf, "VOLUME %d", s_volume);
    ld.text = buf;
    a = (lv_area_t){BAR_X, PANEL_Y + 40, BAR_X + 200, PANEL_Y + 54};
    lv_draw_label(L, &ld, &a);

    snprintf(buf, sizeof buf, "LIGHT %d", s_brightness);
    ld.text = buf;
    a = (lv_area_t){BAR_X, PANEL_Y + 90, BAR_X + 200, PANEL_Y + 104};
    lv_draw_label(L, &ld, &a);

    ld.align = LV_TEXT_ALIGN_CENTER;
    ld.color = lv_color_hex(0x6E5524);
    ld.text = "TAP OUTSIDE TO CLOSE";
    a = (lv_area_t){PANEL_X, PANEL_Y + PANEL_H - 24, PANEL_X + PANEL_W, PANEL_Y + PANEL_H - 10};
    lv_draw_label(L, &ld, &a);

    // Two faders.
    for (int i = 0; i < 2; i++) {
        int y = i ? BAR_LUM_Y : BAR_VOL_Y;
        int value = i ? s_brightness : s_volume;
        int fill = (value * BAR_W) / 100;
        lv_area_t track = {BAR_X, y - 4, BAR_X + BAR_W - 1, y + 4};
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_color = lv_color_hex(0x33260F);
        d.bg_opa = LV_OPA_COVER;
        d.border_opa = LV_OPA_TRANSP;
        lv_draw_rect(L, &d, &track);

        lv_area_t on = {BAR_X, y - 4, BAR_X + fill, y + 4};
        d.bg_color = (s_drag == i + 1) ? lv_color_hex(0xFFE08A) : amber;
        lv_draw_rect(L, &d, &on);

        lv_area_t knob = {BAR_X + fill - 8, y - 9, BAR_X + fill + 8, y + 9};
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_color = ink;
        d.bg_opa = LV_OPA_COVER;
        d.border_color = amber;
        d.border_width = 2;
        d.border_opa = LV_OPA_COVER;
        lv_draw_rect(L, &d, &knob);
    }
}

void ui_show_controls(bool show)
{
    if (!s_panel) return;
    if (show) {
        ui_dim(false);
        lv_obj_set_hidden(s_panel, false);
        lv_obj_move_foreground(s_panel);
        if (s_caption) lv_obj_set_hidden(s_caption, true);
        if (s_status) lv_obj_set_hidden(s_status, true);
        s_touch_ms = lv_tick_get();
        lv_obj_invalidate(s_panel);
    } else {
        lv_obj_set_hidden(s_panel, true);
        if (s_caption) lv_obj_set_hidden(s_caption, false);
        if (s_status) lv_obj_set_hidden(s_status, false);
        s_drag = 0;
        prefs_save();
    }
}

bool ui_controls_visible(void)
{
    return s_panel && !lv_obj_is_hidden(s_panel);
}

static int x_to_value(int x, int lo)
{
    int v = (x - BAR_X) * 100 / BAR_W;
    if (v < lo) v = lo;
    if (v > 100) v = 100;
    return v;
}

static void apply_drag(int x)
{
    if (s_drag == 1) {
        s_volume = x_to_value(x, 0);
        audio_set_volume(s_volume);
    } else if (s_drag == 2) {
        s_brightness = x_to_value(x, 15);
        bsp_display_brightness_set(s_brightness);
    } else {
        return;
    }
    s_touch_ms = lv_tick_get();
    s_prefs_dirty = true;
    lv_obj_invalidate(s_panel);
}

static void screen_pressed(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    if (!ui_controls_visible()) {
        ui_show_controls(true);
        return;
    }

    s_touch_ms = lv_tick_get();
    if (p.x >= BAR_X - 12 && p.x <= BAR_X + BAR_W + 12) {
        if (p.y >= BAR_VOL_Y - 22 && p.y <= BAR_VOL_Y + 22) {
            s_drag = 1;
            apply_drag(p.x);
            return;
        }
        if (p.y >= BAR_LUM_Y - 22 && p.y <= BAR_LUM_Y + 22) {
            s_drag = 2;
            apply_drag(p.x);
            return;
        }
    }
    ui_show_controls(false);
}

static void screen_pressing(lv_event_t *e)
{
    if (!s_drag) return;
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);
    apply_drag(p.x);
}

static void screen_released(lv_event_t *e)
{
    (void)e;
    if (s_drag) prefs_save();
    s_drag = 0;
}

/* ------------------------------------------------------------------ ui */

static void build_panel(lv_obj_t *scr)
{
    s_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, PANEL_W, PANEL_H);
    lv_obj_set_pos(s_panel, PANEL_X, PANEL_Y);
    lv_obj_set_scrollable(s_panel, false);
    lv_obj_set_clickable(s_panel, false);
    lv_obj_add_event_cb(s_panel, panel_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_set_hidden(s_panel, true);
}

void ui_init(void)
{
    prefs_load();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x060606), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_clickable(scr, true);
    lv_obj_add_event_cb(scr, screen_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, screen_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, screen_released, LV_EVENT_RELEASED, NULL);

    face_create(scr);

    s_brand = lv_label_create(scr);
    lv_label_set_text(s_brand, "BUMBLE");
    style_label(s_brand, &lv_font_unscii_16, lv_color_hex(0x8A6A2A), 6);
    lv_obj_align(s_brand, LV_ALIGN_TOP_MID, 0, 34);

    s_caption = lv_label_create(scr);
    lv_label_set_text(s_caption, "");
    style_label(s_caption, &lv_font_unscii_8, lv_color_hex(0x8A6A2A), 1);
    lv_label_set_long_mode(s_caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_caption, 320);
    lv_obj_align(s_caption, LV_ALIGN_TOP_MID, 0, 374);

    s_alarm = lv_label_create(scr);
    lv_label_set_text(s_alarm, "");
    style_label(s_alarm, &lv_font_unscii_8, lv_color_hex(0x8A6A2A), 2);
    lv_obj_align(s_alarm, LV_ALIGN_TOP_MID, 0, 58);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "");
    style_label(s_status, &lv_font_unscii_16, lv_color_hex(0xC79A46), 3);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 400);

    build_panel(scr);
    lv_timer_create(tick_cb, 33, NULL);
    audio_set_volume(s_volume);
    bsp_display_brightness_set(s_brightness);
    ESP_LOGI("ui", "restored volume %d, light %d", s_volume, s_brightness);
}

void ui_status(const char *text)
{
    if (!s_status) return;
    char buf[48];
    upper_ascii(buf, sizeof buf, text);
    bsp_display_lock(-1);
    lv_label_set_text(s_status, buf);
    bsp_display_unlock();
}

void ui_caption(const char *text)
{
    if (!s_caption) return;
    char buf[128];
    upper_ascii(buf, sizeof buf, text ? text : "");
    bsp_display_lock(-1);
    lv_label_set_text(s_caption, buf);
    bsp_display_unlock();
}

void ui_set_tap_cb(void (*cb)(void))
{
    (void)cb;  // taps are handled by the control overlay
}
