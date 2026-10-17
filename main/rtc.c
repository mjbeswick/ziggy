#include "rtc.h"

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define TAG "rtc"
#define ADDR 0x51

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static int reg_write(uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t buf[16];
    if (n + 1 > sizeof buf) return -1;
    buf[0] = reg;
    memcpy(buf + 1, data, n);
    return i2c_master_transmit(s_dev, buf, n + 1, 100) == ESP_OK ? 0 : -1;
}

static int reg_read(uint8_t reg, uint8_t *data, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, n, 100) == ESP_OK ? 0 : -1;
}

static void rtc_set(int year, int mon, int day, int hour, int min, int sec)
{
    uint8_t t[7] = {
        bin2bcd((uint8_t)sec), bin2bcd((uint8_t)min), bin2bcd((uint8_t)hour),
        bin2bcd((uint8_t)day), 0, bin2bcd((uint8_t)mon), bin2bcd((uint8_t)(year % 100))};
    // Seconds register bit 7 must be 0 for a running oscillator; control 1 STOP=0.
    uint8_t ctrl = 0x00;
    reg_write(0x00, &ctrl, 1);
    if (reg_write(0x04, t, 7) != 0) ESP_LOGW(TAG, "could not set time");
}

bool board_rtc_read(int *year, int *mon, int *day, int *hour, int *min, int *sec)
{
    if (!s_dev) return false;
    uint8_t t[7];
    if (reg_read(0x04, t, sizeof t) != 0) return false;
    int y = 2000 + bcd2bin(t[6] & 0x7F);
    int mo = bcd2bin(t[5] & 0x1F);
    int d = bcd2bin(t[3] & 0x3F);
    int h = bcd2bin(t[2] & 0x3F);
    int mi = bcd2bin(t[1] & 0x7F);
    int s = bcd2bin(t[0] & 0x7F);
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) return false;
    *year = y;
    *mon = mo;
    *day = d;
    *hour = h;
    *min = mi;
    *sec = s;
    return true;
}

void board_rtc_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(TAG, "no I2C bus");
        return;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063 not found");
        s_dev = NULL;
        return;
    }

    int y, mo, d, h, mi, s;
    if (!board_rtc_read(&y, &mo, &d, &h, &mi, &s) || y < 2024) {
        // Seed from the build timestamp: wrong by the age of the firmware,
        // but a sane starting point for a board with no network.
        static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        char mon[4] = {0};
        int day = 1, year = 2025, hour = 12, min = 0, sec = 0;
        if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) == 3) {
            const char *p = strstr(months, mon);
            int m = p ? (int)((p - months) / 3) + 1 : 1;
            sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);
            rtc_set(year, m, day, hour, min, sec);
            ESP_LOGI(TAG, "clock seeded: %04d-%02d-%02d %02d:%02d", year, m, day, hour, min);
        }
    } else {
        ESP_LOGI(TAG, "clock: %04d-%02d-%02d %02d:%02d:%02d", y, mo, d, h, mi, s);
    }
}
