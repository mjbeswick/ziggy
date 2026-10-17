// One-shot alarm / timer, backed by the PCF85063 and NVS.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void alarm_init(void);

// Absolute time: the next occurrence of hour:minute.  Returns false if the
// RTC is unavailable.
bool alarm_set_at(int hour, int minute);
bool alarm_set_at_ex(int hour, int minute, bool daily);
bool alarm_set_in(int seconds);
void alarm_cancel(void);
void alarm_snooze(void);

bool alarm_active(void);
bool alarm_is_timer(void);
bool alarm_is_daily(void);
bool alarm_get(int *hour, int *minute);
// "in 4 minutes" / "tomorrow at 7:30", for the spoken answer.  Empty when off.
const char *alarm_describe(void);

// True exactly once, in the minute the alarm is due.  Clears the alarm.
bool alarm_poll(void);

#ifdef __cplusplus
}
#endif
