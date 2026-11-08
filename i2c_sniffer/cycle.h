#pragma once
#include "decoder.h"
#include "record.h"

typedef struct {
    bool     have_sh, have_hum;            // SHTC3 data / humidity write seen in this cycle
    float    t_c, rh_pct, ah_calc_gm3, ah_sent_gm3;
    uint16_t rh_raw, hum_word;
    bool     hum_is_raw_rh;                // the written word equals the raw SHTC3 RH word
    uint16_t eco2, tvoc;
} cycle_sample_t;

void     cycle_init(void);
bool     cycle_handle(const txn_t *t, cycle_sample_t *out);   // true when a complete SGP30 reading was decoded
uint32_t cycle_errors(void);

// aggregation of cycles into one record per logging interval (peaks included)
void     agg_add(const cycle_sample_t *s);
bool     agg_flush(sdlog_record_t *rec, uint32_t uptime_s);   // false if no cycle was seen in the interval
