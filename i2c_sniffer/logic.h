#pragma once
#include <stdint.h>
#include <stdbool.h>

uint8_t  crc8(const uint8_t *d, unsigned n);                   // Sensirion CRC-8 (0x31, init 0xFF)
float    abs_humidity(float rh_percent, float t_celsius);      // g/m3
uint16_t ah_to_sgp30_word(float ah);                           // 8.8 fixed point, never 0 (0 would disable)
int      level_from_value(int v, const int th[4]);             // 0 green .. 4 dark red
int      level_with_hysteresis(int cur, int v, const int th[4], int hyst);
uint8_t  led_mask(int level, bool fill);                       // bit0 = bottom LED of a bar
