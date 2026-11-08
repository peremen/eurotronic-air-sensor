#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint32_t uptime_s;
    float    t_c, rh_pct;                  // SHTC3
    float    ah_calc_gm3;                  // correct absolute humidity from T and RH
    float    ah_sent_gm3;                  // what was written to the SGP30 (word / 256), 0 if not seen
    uint16_t eco2_last, eco2_max, tvoc_last, tvoc_max;
    uint8_t  co2_level, voc_level;         // 0 green .. 4 dark red (manual thresholds, from the interval peaks)
    uint16_t cycles;                       // sensor cycles seen in the interval
    uint16_t errors;                       // CRC / protocol errors seen in the interval
    char     source;                       // 'S' = sniffed from the original STM8, 'E' = emulator
} sdlog_record_t;

const char *record_csv_header(void);
int record_format(char *buf, size_t n, const sdlog_record_t *r);       // one CSV line including the newline
