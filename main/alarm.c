#include "alarm.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "rtc.h"

#define TAG "alarm"

typedef struct {
    int y, mo, d, h, mi;
} when_t;

static bool s_on;
static bool s_daily;
static bool s_timer;
static when_t s_when;

static bool now_when(when_t *w)
{
    int sec;
    return board_rtc_read(&w->y, &w->mo, &w->d, &w->h, &w->mi, &sec);
}

static int cmp(const when_t *a, const when_t *b)
{
    if (a->y != b->y) return a->y < b->y ? -1 : 1;
    if (a->mo != b->mo) return a->mo < b->mo ? -1 : 1;
    if (a->d != b->d) return a->d < b->d ? -1 : 1;
    if (a->h != b->h) return a->h < b->h ? -1 : 1;
    if (a->mi != b->mi) return a->mi < b->mi ? -1 : 1;
    return 0;
}

static int days_in_month(int y, int m)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[(m - 1) % 12];
}

static void add_day(when_t *w)
{
    if (++w->d > days_in_month(w->y, w->mo)) {
        w->d = 1;
        if (++w->mo > 12) {
            w->mo = 1;
            w->y++;
        }
    }
}

static void add_minutes(when_t *w, int minutes)
{
    w->mi += minutes;
    while (w->mi >= 60) {
        w->mi -= 60;
        if (++w->h >= 24) {
            w->h = 0;
            add_day(w);
        }
    }
}

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("bumble", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "al_on", s_on ? 1 : 0);
    nvs_set_u8(h, "al_daily", s_daily ? 1 : 0);
    nvs_set_u8(h, "al_timer", s_timer ? 1 : 0);
    if (s_on) {
        nvs_set_i16(h, "al_y", (int16_t)s_when.y);
        nvs_set_u8(h, "al_mo", (uint8_t)s_when.mo);
        nvs_set_u8(h, "al_d", (uint8_t)s_when.d);
        nvs_set_u8(h, "al_h", (uint8_t)s_when.h);
        nvs_set_u8(h, "al_mi", (uint8_t)s_when.mi);
    }
    nvs_commit(h);
    nvs_close(h);
}

void alarm_init(void)
{
    nvs_handle_t h;
    s_on = false;
    s_daily = false;
    s_timer = false;
    if (nvs_open("bumble", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t on = 0, daily = 0, timer = 0;
    int16_t y = 0;
    uint8_t mo = 0, d = 0, hh = 0, mm = 0;
    if (nvs_get_u8(h, "al_on", &on) == ESP_OK && on &&
        nvs_get_i16(h, "al_y", &y) == ESP_OK &&
        nvs_get_u8(h, "al_mo", &mo) == ESP_OK &&
        nvs_get_u8(h, "al_d", &d) == ESP_OK &&
        nvs_get_u8(h, "al_h", &hh) == ESP_OK &&
        nvs_get_u8(h, "al_mi", &mm) == ESP_OK) {
        nvs_get_u8(h, "al_daily", &daily);
        nvs_get_u8(h, "al_timer", &timer);
        s_when = (when_t){y, mo, d, hh, mm};
        s_on = true;
        s_daily = daily != 0;
        s_timer = timer != 0;
        when_t now;
        if (now_when(&now) && cmp(&now, &s_when) > 0) {
            if (s_daily) {
                while (cmp(&now, &s_when) >= 0) add_day(&s_when);
                save();
            } else {
                ESP_LOGI(TAG, "alarm was in the past, clearing");
                s_on = false;
                save();
            }
        } else {
            ESP_LOGI(TAG, "alarm restored: %04d-%02d-%02d %02d:%02d",
                     s_when.y, s_when.mo, s_when.d, s_when.h, s_when.mi);
        }
    }
    nvs_close(h);
}

bool alarm_set_at(int hour, int minute)
{
    return alarm_set_at_ex(hour, minute, false);
}

bool alarm_set_at_ex(int hour, int minute, bool daily)
{
    when_t now;
    if (!now_when(&now)) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;

    when_t t = now;
    t.h = hour;
    t.mi = minute;
    // No am/pm given: a small hour that has passed but whose 12h twin has not
    // is much more likely to mean the afternoon.
    if (hour < 12 && cmp(&now, &t) > 0 && now.h >= 12 && hour + 12 > now.h) {
        t.h = hour + 12;
    }
    if (cmp(&now, &t) >= 0) add_day(&t);

    s_when = t;
    s_on = true;
    s_daily = daily;
    s_timer = false;
    save();
    ESP_LOGI(TAG, "alarm set%s: %04d-%02d-%02d %02d:%02d", daily ? " daily" : "",
             s_when.y, s_when.mo, s_when.d, s_when.h, s_when.mi);
    return true;
}

bool alarm_set_in(int seconds)
{
    when_t now;
    if (!now_when(&now)) return false;
    if (seconds < 5) seconds = 5;
    if (seconds > 24 * 3600) seconds = 24 * 3600;
    s_when = now;
    add_minutes(&s_when, (seconds + 59) / 60);
    s_on = true;
    s_daily = false;
    s_timer = true;
    save();
    ESP_LOGI(TAG, "timer set: %02d:%02d", s_when.h, s_when.mi);
    return true;
}

void alarm_cancel(void)
{
    s_on = false;
    s_daily = false;
    s_timer = false;
    save();
}

void alarm_snooze(void)
{
    if (!s_on) return;
    when_t now;
    if (!now_when(&now)) return;
    s_when = now;
    add_minutes(&s_when, 5);
    save();
    ESP_LOGI(TAG, "snoozed to %02d:%02d", s_when.h, s_when.mi);
}

bool alarm_active(void) { return s_on; }
bool alarm_is_timer(void) { return s_timer; }
bool alarm_is_daily(void) { return s_daily; }

bool alarm_get(int *hour, int *minute)
{
    if (!s_on) return false;
    *hour = s_when.h;
    *minute = s_when.mi;
    return true;
}

const char *alarm_describe(void)
{
    static char buf[64];
    if (!s_on) return "";
    int h = s_when.h % 12;
    if (h == 0) h = 12;
    snprintf(buf, sizeof buf, "%s%d:%02d %s", s_daily ? "every day at " : "",
             h, s_when.mi, s_when.h >= 12 ? "pm" : "am");
    return buf;
}

bool alarm_poll(void)
{
    if (!s_on) return false;
    when_t now;
    if (!now_when(&now)) return false;

    int c = cmp(&now, &s_when);
    if (c == 0) {
        if (s_daily) {
            add_day(&s_when);
            save();
        } else {
            s_on = false;
            save();
        }
        return true;
    }
    if (c > 0) {
        // More than a minute late: it was missed while we were off.
        int late = (now.h * 60 + now.mi) - (s_when.h * 60 + s_when.mi);
        if (!s_daily && (now.d != s_when.d || late > 1)) {
            ESP_LOGW(TAG, "missed alarm, clearing");
            s_on = false;
            save();
        }
    }
    return false;
}
