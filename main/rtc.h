// rtc -- the board's PCF85063, reached over the BSP I2C bus.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opens the RTC and, if it has never been set, seeds it from the build time.
void board_rtc_init(void);

// Returns false if the RTC is missing or its registers are nonsense.
bool board_rtc_read(int *year, int *mon, int *day, int *hour, int *min, int *sec);

#ifdef __cplusplus
}
#endif
