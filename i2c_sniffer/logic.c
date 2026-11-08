#include "logic.h"
#include <math.h>

uint8_t crc8(const uint8_t *d, unsigned n) {
    uint8_t c = 0xFF;
    while (n--) {
        c ^= *d++;
        for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
    }
    return c;
}

float abs_humidity(float rh, float t) {
    float ps = 6.112f * expf(17.62f * t / (243.12f + t));
    return 216.7f * (rh / 100.0f * ps) / (273.15f + t);
}

uint16_t ah_to_sgp30_word(float ah) {
    if (ah < 0.0f) ah = 0.0f;
    float w = ah * 256.0f + 0.5f;
    if (w > 65535.0f) w = 65535.0f;
    uint16_t r = (uint16_t)w;
    return r ? r : 1;
}

int level_from_value(int v, const int th[4]) {
    if (v < th[0])  return 0;   // green
    if (v < th[1])  return 1;   // yellow
    if (v < th[2])  return 2;   // orange
    if (v <= th[3]) return 3;   // bright red
    return 4;                   // dark red
}

int level_with_hysteresis(int cur, int v, const int th[4], int hyst) {
    int n = level_from_value(v, th);
    if (hyst > 0 && n < cur && cur >= 1 && v > th[cur - 1] - hyst) return cur;   // improving: need a margin
    return n;
}

uint8_t led_mask(int level, bool fill) {
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    return fill ? (uint8_t)((1u << (level + 1)) - 1u) : (uint8_t)(1u << level);
}
