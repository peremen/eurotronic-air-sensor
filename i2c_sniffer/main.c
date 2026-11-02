// Passive I2C sniffer for Raspberry Pi Pico (RP2040), C SDK.
//
// Wiring (through ~100 ohm series resistors, GND shared, nothing else connected):
//     board SDA -> GP4 (PIN_SDA)      board SCL -> GP5 (PIN_SDA + 1)      board GND -> any Pico GND
//
// A PIO state machine oversamples both lines at SAMPLE_RATE_HZ, DMA (two chained channels, ping-pong)
// fills a ring of buffers, and decoder.c / interp.c turn the samples into transactions and values.
// Serial commands over USB CDC:  s = summary   r = toggle raw lines   c = clear statistics
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "i2c_sampler.pio.h"
#include "decoder.h"
#include "interp.h"

#ifndef PIN_SDA
#define PIN_SDA 4                   // SCL must be PIN_SDA + 1
#endif
#ifndef SAMPLE_RATE_HZ
#define SAMPLE_RATE_HZ 2000000u     // 20 samples per bit at 100 kHz
#endif
#ifndef BUF_WORDS
#define BUF_WORDS 2048              // 16 samples per word -> 32768 samples (16 ms at 2 MS/s) per buffer
#endif
#ifndef NBUF
#define NBUF 12                     // ring of buffers: ~98 kB RAM, ~200 ms of slack at 2 MS/s
#endif
#ifndef SUMMARY_PERIOD_S
#define SUMMARY_PERIOD_S 600        // display only; 0 disables the automatic summary
#endif
#ifndef WAIT_FOR_USB
#define WAIT_FOR_USB 1
#endif

static PIO  g_pio = pio0;
static uint g_sm;
static uint g_dma[2];
static uint32_t bufs[NBUF][BUF_WORDS];

static volatile uint32_t produced;   // buffers completed by DMA
static volatile uint32_t consumed;   // buffers fully decoded
static volatile uint32_t overruns;

static void dma_isr(void) {
    for (;;) {                       // buffers complete strictly alternately: seq k on channel k & 1
        uint32_t k = produced;
        uint c = k & 1u;
        if (!(dma_hw->ints0 & (1u << g_dma[c]))) break;
        dma_hw->ints0 = 1u << g_dma[c];
        produced = k + 1;
        uint32_t next = k + 2;       // this channel will write sequence k + 2 after the other one
        if (next >= consumed + NBUF) overruns++;
        dma_channel_set_write_addr(g_dma[c], bufs[next % NBUF], false);
        dma_channel_set_trans_count(g_dma[c], BUF_WORDS, false);
    }
}

static void start_capture(void) {
    uint offset = pio_add_program(g_pio, &i2c_sampler_program);
    g_sm = pio_claim_unused_sm(g_pio, true);

    uint32_t sys = clock_get_hz(clk_sys);
    uint32_t div_fixed = (uint32_t)(((uint64_t)sys * 256u + SAMPLE_RATE_HZ / 2) / SAMPLE_RATE_HZ);  // 16.8 fixed
    i2c_sampler_program_init(g_pio, g_sm, offset, PIN_SDA, (uint16_t)(div_fixed >> 8), (uint8_t)(div_fixed & 0xFF));
    double actual_rate = (double)sys * 256.0 / div_fixed;
    interp_init(actual_rate);

    g_dma[0] = dma_claim_unused_channel(true);
    g_dma[1] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; i++) {
        dma_channel_config c = dma_channel_get_default_config(g_dma[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, pio_get_dreq(g_pio, g_sm, false));
        channel_config_set_chain_to(&c, g_dma[1 - i]);
        dma_channel_configure(g_dma[i], &c, bufs[i], &g_pio->rxf[g_sm], BUF_WORDS, false);
        dma_channel_set_irq0_enabled(g_dma[i], true);
    }
    irq_set_exclusive_handler(DMA_IRQ_0, dma_isr);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(g_dma[0]);
    pio_sm_set_enabled(g_pio, g_sm, true);
    printf("sniffer running: SDA=GP%d SCL=GP%d, %.0f samples/s, %u buffers x %u samples\n",
           PIN_SDA, PIN_SDA + 1, actual_rate, NBUF, BUF_WORDS * 16u);
}

int main(void) {
    stdio_init_all();
#if WAIT_FOR_USB
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(200);
#endif
    dec_init();
    start_capture();

    uint32_t last_overruns = 0;
    absolute_time_t next_summary = make_timeout_time_ms(SUMMARY_PERIOD_S * 1000);

    for (;;) {
        // 1. decode every completed buffer
        while (consumed < produced) {
            const uint32_t *b = bufs[consumed % NBUF];
            for (uint i = 0; i < BUF_WORDS; i++) dec_push_word(b[i]);
            consumed++;
        }
        // 2. report finished transactions
        txn_t t;
        while (dec_pop(&t)) interp_handle(&t);

        // 3. health checks
        if (overruns != last_overruns) {
            printf("!! DMA ring overrun (%lu): decoding could not keep up, timestamps after this point are unreliable\n",
                   (unsigned long)overruns);
            last_overruns = overruns;
            dec_reset();
        }
        uint32_t stall_bit = 1u << (PIO_FDEBUG_RXSTALL_LSB + g_sm);
        if (g_pio->fdebug & stall_bit) {
            g_pio->fdebug = stall_bit;
            printf("!! PIO RX FIFO stalled: samples were lost\n");
        }
        if (dec_dropped()) {
            static uint32_t shown;
            if (dec_dropped() != shown) { shown = dec_dropped(); printf("!! %lu transactions dropped (output too slow)\n", (unsigned long)shown); }
        }

        // 4. serial commands
        int ch = getchar_timeout_us(0);
        if (ch == 's') interp_summary();
        else if (ch == 'r') { static bool raw = true; raw = !raw; interp_set_raw(raw); printf("raw lines %s\n", raw ? "on" : "off"); }
        else if (ch == 'c') { interp_clear_stats(); printf("statistics cleared\n"); }

#if SUMMARY_PERIOD_S > 0
        if (absolute_time_diff_us(get_absolute_time(), next_summary) <= 0) {
            interp_summary();
            next_summary = make_timeout_time_ms(SUMMARY_PERIOD_S * 1000);
        }
#endif
        sleep_us(500);
    }
}
