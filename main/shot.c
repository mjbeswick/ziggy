#include "shot.h"

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "lvgl.h"

void shot_take(void)
{
    lv_obj_t *obj = lv_screen_active();
    printf("\nDBG create\n"); fflush(stdout);
    lv_draw_buf_t *buf = lv_snapshot_create_draw_buf(obj, LV_COLOR_FORMAT_RGB565);
    printf("DBG buf %p\n", (void *)buf); fflush(stdout);
    if (!buf) {
        printf("\nSHOT FAILED\n");
        fflush(stdout);
        return;
    }

    memset(buf->data, 0, buf->data_size);
    printf("DBG lock\n"); fflush(stdout);
    esp_err_t lk = bsp_display_lock(2000);
    printf("DBG locked %d\n", (int)lk); fflush(stdout);
    lv_result_t rc = lv_snapshot_take_to_draw_buf(obj, LV_COLOR_FORMAT_RGB565, buf);
    bsp_display_unlock();
    printf("DBG taken %d\n", (int)rc); fflush(stdout);

    if (rc != LV_RESULT_OK) {
        printf("\nSHOT FAILED\n");
        fflush(stdout);
        lv_draw_buf_destroy(buf);
        return;
    }

    const uint32_t w = buf->header.w;
    const uint32_t h = buf->header.h;
    const uint32_t stride = buf->header.stride;
    printf("\nSHOT %u %u RGB565 %u\n", (unsigned)w, (unsigned)h, (unsigned)stride);
    for (uint32_t y = 0; y < h; y++) {
        fwrite(buf->data + (size_t)y * stride, 1, (size_t)w * 2, stdout);
    }
    printf("\nEND\n");
    fflush(stdout);

    lv_draw_buf_destroy(buf);
}
