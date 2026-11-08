#include "record.h"
#include "clock.h"
#include <stdio.h>

const char *record_csv_header(void) {
    return "datetime,uptime_s,temp_c,rh_pct,abs_hum_calc_gm3,abs_hum_sent_gm3,eco2_ppm,eco2_max_ppm,"
           "tvoc_ppb,tvoc_max_ppb,co2_level,voc_level,cycles,errors,source\n";
}

int record_format(char *buf, size_t n, const sdlog_record_t *r) {
    char dt[24];
    clock_format(r->uptime_s, dt, sizeof dt);
    return snprintf(buf, n, "%s,%u,%.2f,%.1f,%.2f,%.2f,%u,%u,%u,%u,%u,%u,%u,%u,%c\n", dt, (unsigned)r->uptime_s,
                    r->t_c, r->rh_pct, r->ah_calc_gm3, r->ah_sent_gm3, r->eco2_last, r->eco2_max, r->tvoc_last,
                    r->tvoc_max, r->co2_level, r->voc_level, r->cycles, r->errors, r->source);
}
