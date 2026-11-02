#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TXN_MAX_DATA 32

typedef struct {
    uint64_t t_start;        // sample index of the START condition
    uint64_t t_end;          // sample index of STOP / repeated START
    uint64_t ack_mask;       // bit n set = data byte n was ACKed
    uint32_t max_scl_low;    // longest SCL-low period inside the transaction (samples)
    uint8_t  addr;           // 7-bit address
    bool     rd;             // true = read
    bool     addr_ack;
    bool     stop;           // ended by STOP (false = repeated START)
    bool     trunc;          // more than TXN_MAX_DATA bytes
    uint8_t  len;
    uint8_t  data[TXN_MAX_DATA];
} txn_t;

void     dec_init(void);
void     dec_reset(void);                 // drop the transaction in progress (e.g. after a buffer overrun)
void     dec_push_word(uint32_t w);       // 16 samples: bit0 = SDA, bit1 = SCL, oldest sample in the LSBs
bool     dec_pop(txn_t *out);             // next finished transaction
uint32_t dec_dropped(void);               // transactions lost because the queue was full
