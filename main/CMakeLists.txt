idf_component_register(
    SRCS "app_main.c" "alarm.c" "audio.c"  "display.c" "brain.c" "moji_face.c" "rtc.c" "shot.c" "ui.c"
    INCLUDE_DIRS "."
    REQUIRES waveshare__esp32_s3_touch_amoled_1_75 stt_engine sanotts
             esp_codec_dev nvs_flash waveshare__esp_lcd_touch_cst9217 espressif__esp_lcd_touch driver esp_timer esp_partition esp_lcd lvgl
)
