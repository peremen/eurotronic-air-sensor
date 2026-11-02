#pragma once
#include "decoder.h"

void interp_init(double sample_rate_hz);
void interp_set_raw(bool on);          // print every raw transaction (default on)
void interp_handle(const txn_t *t);    // decode + print one transaction
void interp_summary(void);             // per-command counts and observed intervals
void interp_clear_stats(void);
