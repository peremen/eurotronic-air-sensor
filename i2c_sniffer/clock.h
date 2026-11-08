#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
// Wall clock without an RTC: the user sets it once over the serial port; afterwards it follows the uptime.
// The time is kept exactly as typed (no time zone handling).
bool clock_set_from_string(const char *s, uint32_t uptime_s);      // "YYYY-MM-DD HH:MM:SS"
bool clock_is_set(void);
bool clock_format(uint32_t uptime_s, char *buf, size_t n);         // empty string + false while not set
