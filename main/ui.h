#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);

// Bottom state line, e.g. "STANDBY".  Uppercased, ASCII only.
void ui_status(const char *text);

// One line of transcript / reply above the state line.
void ui_caption(const char *text);

// Called from the LVGL thread when the screen is tapped.
void ui_set_tap_cb(void (*cb)(void));

// Volume / brightness overlay.  Opens on a screen tap.
void ui_show_controls(bool show);
bool ui_controls_visible(void);
void ui_set_volume(int v);
void ui_set_brightness(int v);

// Dim the panel for idle sleep without changing the saved setting.
void ui_dim(bool dim);
// Small line under the brand: "ALARM 7:30 AM", empty for none.
void ui_set_alarm(const char *text);

#ifdef __cplusplus
}
#endif
