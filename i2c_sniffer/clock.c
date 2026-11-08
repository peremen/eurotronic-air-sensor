#include "clock.h"
#include <stdio.h>

static volatile bool g_set;
static volatile int64_t g_offset;       // epoch seconds (as typed) minus uptime seconds

static int64_t days_from_civil(int y, unsigned m, unsigned d) {    // H. Hinnant's algorithm
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yy + (*m <= 2));
}

bool clock_set_from_string(const char *s, uint32_t uptime_s) {
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return false;
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60) return false;
    int64_t epoch = days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + se;
    g_offset = epoch - (int64_t)uptime_s;
    g_set = true;
    return true;
}

bool clock_is_set(void) { return g_set; }

bool clock_format(uint32_t uptime_s, char *buf, size_t n) {
    if (!g_set) { if (n) buf[0] = 0; return false; }
    int64_t t = g_offset + (int64_t)uptime_s;
    int64_t days = t / 86400;
    int64_t rem = t - days * 86400;
    int y; unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    snprintf(buf, n, "%04d-%02u-%02u %02d:%02d:%02d", y, m, d, (int)(rem / 3600), (int)(rem % 3600 / 60), (int)(rem % 60));
    return true;
}
