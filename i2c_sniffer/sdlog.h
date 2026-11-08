#pragma once
#include "record.h"
// All functions must be called from ONE core only (here: core 1), the state is readable from anywhere.
typedef enum { SDLOG_CLOSED, SDLOG_OK, SDLOG_ERROR } sdlog_state_t;

bool           sdlog_open(void);                         // mount, create LOG/, start a new numbered file
void           sdlog_close(void);                        // flush + close + unmount (safe to pull the card)
bool           sdlog_write(const sdlog_record_t *r);     // append one CSV line and flush it to the card
void           sdlog_task(void);                         // retries after an error
sdlog_state_t  sdlog_state(void);
const char    *sdlog_filename(void);
