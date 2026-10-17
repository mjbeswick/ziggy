#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Panel + LVGL only.  The BSP's bsp_display_start() also brings up the
// capacitive touch, which stalls on this board revision; touch comes up
// separately (see touch.c) so the face can boot regardless.
lv_display_t *display_start(void);

// CST9217 touch, registered as an LVGL pointer device. 0 on success.
int touch_start(lv_display_t *disp);

#ifdef __cplusplus
}
#endif
