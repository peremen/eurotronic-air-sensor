// Turns decoded I2C transactions of the original firmware into measurement cycles.
// No assumption about how often the host MCU polls the sensors.
#include "cycle.h"
#include "logic.h"
#include <string.h>

#define ADDR_SGP30 0x58
#define ADDR_SHTC3 0x70

static uint16_t last_cmd[128];
static bool     last_valid[128];
static struct { bool have; float rh, t; uint16_t rh_raw; } sh;
static struct { bool have; uint16_t word; } hum;
static uint32_t errors, errors_at_flush;

static int shtc3_rh_first(uint16_t cmd) {
    switch (cmd) {
    case 0x5C24: case 0x44DE: case 0x58E0: case 0x401A: return 1;
    case 0x7CA2: case 0x6458: case 0x7866: case 0x609C: return 0;
    default: return -1;
    }
}

void cycle_init(void) {
    memset(last_valid, 0, sizeof last_valid);
    sh.have = hum.have = false;
    errors = errors_at_flush = 0;
}

uint32_t cycle_errors(void) { return errors; }

bool cycle_handle(const txn_t *t, cycle_sample_t *out) {
    if (!t->addr_ack || (t->addr != ADDR_SGP30 && t->addr != ADDR_SHTC3)) return false;

    if (!t->rd) {
        if (t->len < 2) return false;
        uint16_t cmd = (uint16_t)(t->data[0] << 8 | t->data[1]);
        last_cmd[t->addr] = cmd;
        last_valid[t->addr] = true;
        if (t->addr == ADDR_SGP30 && cmd == 0x2061 && t->len >= 5) {
            if (crc8(&t->data[2], 2) == t->data[4]) { hum.word = (uint16_t)(t->data[2] << 8 | t->data[3]); hum.have = true; }
            else errors++;
        }
        return false;
    }

    if (!last_valid[t->addr]) return false;
    uint16_t cmd = last_cmd[t->addr];

    if (t->addr == ADDR_SHTC3) {
        int rh_first = shtc3_rh_first(cmd);
        if (rh_first >= 0 && t->len >= 6) {
            if (crc8(&t->data[0], 2) != t->data[2] || crc8(&t->data[3], 2) != t->data[5]) { errors++; return false; }
            uint16_t w0 = (uint16_t)(t->data[0] << 8 | t->data[1]);
            uint16_t w1 = (uint16_t)(t->data[3] << 8 | t->data[4]);
            uint16_t rh_raw = rh_first ? w0 : w1, t_raw = rh_first ? w1 : w0;
            sh.rh_raw = rh_raw;
            sh.rh = 100.0f * rh_raw / 65536.0f;
            sh.t = -45.0f + 175.0f * t_raw / 65536.0f;
            sh.have = true;
        }
        return false;
    }

    if (cmd == 0x2008 && t->len >= 6) {                      // SGP30 measure_air_quality reply
        if (crc8(&t->data[0], 2) != t->data[2] || crc8(&t->data[3], 2) != t->data[5]) { errors++; return false; }
        memset(out, 0, sizeof *out);
        out->eco2 = (uint16_t)(t->data[0] << 8 | t->data[1]);
        out->tvoc = (uint16_t)(t->data[3] << 8 | t->data[4]);
        out->have_sh = sh.have;
        out->have_hum = hum.have;
        if (sh.have) {
            out->t_c = sh.t; out->rh_pct = sh.rh; out->rh_raw = sh.rh_raw;
            out->ah_calc_gm3 = abs_humidity(sh.rh, sh.t);
        }
        if (hum.have) {
            out->hum_word = hum.word;
            out->ah_sent_gm3 = hum.word / 256.0f;
            out->hum_is_raw_rh = sh.have && hum.word == sh.rh_raw;
        }
        sh.have = hum.have = false;                          // never report stale data in the next cycle
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------- aggregation
static const int CO2_TH[4] = {600, 1000, 1800, 3000};
static const int VOC_TH[4] = {500, 1000, 1600, 2200};
static struct {
    uint32_t n;
    uint16_t eco2_last, eco2_max, tvoc_last, tvoc_max;
    bool have_env;
    float t, rh, ah_calc, ah_sent;
} agg;

void agg_add(const cycle_sample_t *s) {
    agg.n++;
    agg.eco2_last = s->eco2; agg.tvoc_last = s->tvoc;
    if (s->eco2 > agg.eco2_max) agg.eco2_max = s->eco2;
    if (s->tvoc > agg.tvoc_max) agg.tvoc_max = s->tvoc;
    if (s->have_sh)  { agg.have_env = true; agg.t = s->t_c; agg.rh = s->rh_pct; agg.ah_calc = s->ah_calc_gm3; }
    if (s->have_hum) agg.ah_sent = s->ah_sent_gm3;
}

bool agg_flush(sdlog_record_t *rec, uint32_t uptime_s) {
    if (agg.n == 0) return false;
    memset(rec, 0, sizeof *rec);
    rec->uptime_s = uptime_s;
    rec->t_c = agg.t; rec->rh_pct = agg.rh; rec->ah_calc_gm3 = agg.ah_calc; rec->ah_sent_gm3 = agg.ah_sent;
    rec->eco2_last = agg.eco2_last; rec->eco2_max = agg.eco2_max;
    rec->tvoc_last = agg.tvoc_last; rec->tvoc_max = agg.tvoc_max;
    rec->co2_level = (uint8_t)level_from_value(agg.eco2_max, CO2_TH);
    rec->voc_level = (uint8_t)level_from_value(agg.tvoc_max, VOC_TH);
    rec->cycles = (uint16_t)(agg.n > 65535 ? 65535 : agg.n);
    rec->errors = (uint16_t)(errors - errors_at_flush);
    rec->source = 'S';
    errors_at_flush = errors;
    memset(&agg, 0, sizeof agg);
    return true;
}
