#include "display.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"

#define TAG "display"

#define TOUCH_RST GPIO_NUM_40
#define TOUCH_INT GPIO_NUM_11

static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    if (!area) return;
    // The panel writes 2-pixel aligned spans fastest; keep the area even.
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
    if (area->x1 < 0) area->x1 = 0;
    if (area->y1 < 0) area->y1 = 0;
    if (area->x2 > BSP_LCD_H_RES - 1) area->x2 = BSP_LCD_H_RES - 1;
    if (area->y2 > BSP_LCD_V_RES - 1) area->y2 = BSP_LCD_V_RES - 1;
}

lv_display_t *display_start(void)
{
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    // Pin the renderer to core 0; the audio/STT task owns core 1.
    adapter_cfg.task_core_id = 0;
    adapter_cfg.task_priority = 5;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "lv adapter init failed");
        return NULL;
    }

    const bsp_display_config_t disp_config = {
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * BSP_LCD_BITS_PER_PIXEL / 8,
    };
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    if (bsp_display_new(&disp_config, &panel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed");
        return NULL;
    }

    esp_lv_adapter_display_config_t cfg = {
        .panel = panel,
        .panel_io = io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = 50,
            .use_psram = true,
            .enable_ppa_accel = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&cfg);
    if (!disp) {
        ESP_LOGE(TAG, "display register failed");
        return NULL;
    }
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    if (esp_lv_adapter_start() != ESP_OK) {
        ESP_LOGE(TAG, "lv adapter start failed");
        return NULL;
    }
    bsp_display_brightness_set(90);
    ESP_LOGI(TAG, "panel up");
    return disp;
}

int touch_start(lv_display_t *disp)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "touch: no I2C bus");
        return -1;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = 100000;
    esp_err_t err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch: panel io: %s", esp_err_to_name(err));
        return -2;
    }

    esp_lcd_touch_config_t cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1},
    };
    esp_lcd_touch_handle_t tp = NULL;
    err = esp_lcd_touch_new_i2c_cst9217(io, &cfg, &tp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch: CST9217 init: %s", esp_err_to_name(err));
        return -3;
    }

    esp_lv_adapter_touch_config_t tcfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);
    if (!esp_lv_adapter_register_touch(&tcfg)) {
        ESP_LOGE(TAG, "touch: LVGL register failed");
        return -4;
    }
    ESP_LOGI(TAG, "touch ready");
    return 0;
}
