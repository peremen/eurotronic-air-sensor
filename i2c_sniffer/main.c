// Option A: passive sniffer + SD logger for the Eurotronic air quality sensor board (Raspberry Pi Pico, RP2040).
//   core 0: PIO/DMA I2C sniffer -> decoder -> measurement cycles -> one aggregated record per interval
//   core 1: owns the SD card (FatFs), takes records from a queue, so card trouble can never disturb the sniffer
//
// Wiring: board SDA -> GP4 and board SCL -> GP5 (through ~100 ohm), board GND -> Pico GND.
//         SD module: MISO GP0, CS GP1, SCK GP2, MOSI GP3, VCC = 5 V (VBUS), GND.
// Status LED: 1 flash = sensor reading decoded, 2 flashes = record written to the SD card, 3 slow flashes = SD problem,
//             5 rapid flashes = sniffer/bus problem, 1 long flash every 2 s = alive but no sensor traffic.
// Serial commands (line based):  t YYYY-MM-DD HH:MM:SS  set the clock | s  status + command summary
//                                v  toggle verbose transaction output | u  close the log file (safe to pull the card)
//                                n  open a new log file (after u, or to force a remount)
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "i2c_sampler.pio.h"
#include "config.h"
#include "decoder.h"
#include "interp.h"
#include "cycle.h"
#include "clock.h"
#include "sdlog.h"
#include "status_led.h"

// Status LED: the on-board LED of the Pico (GP25). Define STATUS_LED_PIN in config.h to use an external LED instead
// (with a series resistor). The Pico W has no GPIO LED (it sits on the wireless chip), then the LED is simply disabled.
#ifndef STATUS_LED_PIN
#ifdef PICO_DEFAULT_LED_PIN
#define STATUS_LED_PIN PICO_DEFAULT_LED_PIN
#endif
#endif

// ------------------------------------------------------------------------------------------ sniffer (core 0)
static PIO  g_pio = pio0;
static uint g_sm;
static uint g_dma[2];
static uint32_t bufs[NBUF][BUF_WORDS];
static volatile uint32_t produced, consumed, overruns;

static void dma_isr(void) {
    for (;;) {                                   // buffers complete strictly alternately: sequence k on channel k & 1
        uint32_t k = produced;
        uint c = k & 1u;
        if (!(dma_hw->ints0 & (1u << g_dma[c]))) break;
        dma_hw->ints0 = 1u << g_dma[c];
        produced = k + 1;
        uint32_t next = k + 2;
        if (next >= consumed + NBUF) overruns++;
        dma_channel_set_write_addr(g_dma[c], bufs[next % NBUF], false);
        dma_channel_set_trans_count(g_dma[c], BUF_WORDS, false);
    }
}

static void start_capture(void) {
    uint offset = pio_add_program(g_pio, &i2c_sampler_program);
    g_sm = pio_claim_unused_sm(g_pio, true);
    uint32_t sys = clock_get_hz(clk_sys);
    uint32_t div_fixed = (uint32_t)(((uint64_t)sys * 256u + SAMPLE_RATE_HZ / 2) / SAMPLE_RATE_HZ);
    i2c_sampler_program_init(g_pio, g_sm, offset, PIN_SDA, (uint16_t)(div_fixed >> 8), (uint8_t)(div_fixed & 0xFF));
    interp_init((double)sys * 256.0 / div_fixed);
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
}

// ------------------------------------------------------------------------------------------- SD writer (core 1)
static queue_t rec_q;
static volatile int sd_request;                  // 0 none, 1 close, 2 (re)open
static volatile uint32_t sd_written;

static void core1_entry(void) {
    sdlog_open();                                // everything touching the card runs on this core
    for (;;) {
        int req = sd_request;
        if (req == 1) { sdlog_close(); sd_request = 0; }
        else if (req == 2) { sdlog_open(); sd_request = 0; }
        sdlog_task();
        sdlog_record_t r;
        while (sdlog_state() == SDLOG_OK && queue_try_peek(&rec_q, &r)) {
            if (!sdlog_write(&r)) break;         // keep the record queued, it is written after the remount
            queue_try_remove(&rec_q, &r);
            sd_written++;
        }
        sleep_ms(50);
    }
}

// ------------------------------------------------------------------------------------------------ console
static bool verbose;
static uint32_t queue_dropped;

static uint32_t uptime_s(void) { return to_ms_since_boot(get_absolute_time()) / 1000u; }

static const char *sd_text(void) {
    switch (sdlog_state()) { case SDLOG_OK: return "logging"; case SDLOG_ERROR: return "ERROR (retrying)"; default: return "closed"; }
}

static void print_status(void) {
    char dt[24];
    clock_format(uptime_s(), dt, sizeof dt);
    printf("status: uptime %u s, clock %s, SD %s %s, records written %lu, queued %u, dropped %lu, sniffer errors %lu\n",
           (unsigned)uptime_s(), dt[0] ? dt : "not set", sd_text(), sdlog_filename(), (unsigned long)sd_written,
           (unsigned)queue_get_level(&rec_q), (unsigned long)queue_dropped, (unsigned long)cycle_errors());
    interp_summary();
}

static void handle_line(char *line) {
    if (line[0] == 't' && line[1] == ' ') {
        if (clock_set_from_string(line + 2, uptime_s())) printf("clock set\n");
        else printf("usage: t YYYY-MM-DD HH:MM:SS\n");
    } else if (!strcmp(line, "s")) print_status();
    else if (!strcmp(line, "v")) { verbose = !verbose; interp_set_raw(true); printf("verbose %s\n", verbose ? "on" : "off"); }
    else if (!strcmp(line, "u")) { sd_request = 1; printf("closing the log file...\n"); }
    else if (!strcmp(line, "n")) { sd_request = 2; printf("opening a new log file...\n"); }
    else if (line[0]) printf("commands: t YYYY-MM-DD HH:MM:SS | s | v | u | n\n");
}

static void poll_console(void) {
    static char line[48];
    static unsigned len;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') { line[len] = 0; handle_line(line); len = 0; }
        else if (len < sizeof line - 1) line[len++] = (char)c;
    }
}

static void led_set(bool on) {
#ifdef STATUS_LED_PIN
    gpio_put(STATUS_LED_PIN, on);
#else
    (void)on;
#endif
}

int main(void) {
    stdio_init_all();
#ifdef STATUS_LED_PIN
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
#endif
    status_led_init(led_set);
    for (int waited = 0; !stdio_usb_connected() && waited < USB_WAIT_MS; waited += 100) sleep_ms(100);
    printf("aq_sniff_logger: SDA=GP%d SCL=GP%d, log interval %d s\n", PIN_SDA, PIN_SDA + 1, LOG_INTERVAL_S);

    dec_init();
    cycle_init();
    queue_init(&rec_q, sizeof(sdlog_record_t), RECORD_QUEUE_LEN);
    multicore_launch_core1(core1_entry);
    start_capture();

    uint32_t last_overruns = 0, stalls = 0, last_bus_errors = 0, last_written = 0;
    uint32_t last_data_ms = 0, next_sd_err_ms = 0, next_idle_ms = 0;
    absolute_time_t next_flush = make_timeout_time_ms(LOG_INTERVAL_S * 1000);
    absolute_time_t next_summary = make_timeout_time_ms(SUMMARY_PERIOD_S * 1000);

    for (;;) {
        while (consumed < produced) {                                   // 1. decode completed sample buffers
            const uint32_t *b = bufs[consumed % NBUF];
            for (uint i = 0; i < BUF_WORDS; i++) dec_push_word(b[i]);
            consumed++;
        }
        txn_t t;                                                        // 2. transactions -> measurement cycles
        while (dec_pop(&t)) {
            if (verbose) interp_handle(&t);
            cycle_sample_t s;
            if (cycle_handle(&t, &s)) {
                agg_add(&s);
                status_led_event(LED_EV_DATA);
                last_data_ms = to_ms_since_boot(get_absolute_time());
            }
        }
        if (overruns != last_overruns) {                                // 3. health checks
            printf("!! DMA ring overrun (%lu): decoding could not keep up\n", (unsigned long)overruns);
            last_overruns = overruns;
            dec_reset();
        }
        uint32_t stall_bit = 1u << (PIO_FDEBUG_RXSTALL_LSB + g_sm);
        if (g_pio->fdebug & stall_bit) { g_pio->fdebug = stall_bit; stalls++; printf("!! PIO RX FIFO stalled: samples lost\n"); }

        if (absolute_time_diff_us(get_absolute_time(), next_flush) <= 0) {   // 4. one record per interval
            next_flush = delayed_by_ms(next_flush, LOG_INTERVAL_S * 1000);
            sdlog_record_t rec;
            if (agg_flush(&rec, uptime_s())) {
                if (!queue_try_add(&rec_q, &rec)) queue_dropped++;
                printf("%6u s  T %.2f C  RH %.1f %%  AH calc %.2f / sent %.1f g/m3  eCO2 %u (max %u)  TVOC %u (max %u)  cycles %u err %u  SD %s\n",
                       (unsigned)rec.uptime_s, rec.t_c, rec.rh_pct, rec.ah_calc_gm3, rec.ah_sent_gm3, rec.eco2_last, rec.eco2_max,
                       rec.tvoc_last, rec.tvoc_max, rec.cycles, rec.errors, sd_text());
            } else {
                printf("%6u s  no sensor traffic seen in this interval\n", (unsigned)uptime_s());
            }
        }
        {                                                               // 5. status LED
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if (sd_written != last_written) { last_written = sd_written; status_led_event(LED_EV_WRITE); }
            uint32_t bus_errors = cycle_errors() + overruns + stalls + queue_dropped + dec_dropped();
            if (bus_errors != last_bus_errors) { last_bus_errors = bus_errors; status_led_event(LED_EV_ERR_BUS); }
            if (sdlog_state() == SDLOG_ERROR && (int32_t)(now - next_sd_err_ms) >= 0) { status_led_event(LED_EV_ERR_SD); next_sd_err_ms = now + 1500; }
            if (now - last_data_ms > NO_DATA_TIMEOUT_S * 1000u && (int32_t)(now - next_idle_ms) >= 0) { status_led_event(LED_EV_IDLE); next_idle_ms = now + 2000; }
            status_led_task(now);
        }
        poll_console();
#if SUMMARY_PERIOD_S > 0
        if (absolute_time_diff_us(get_absolute_time(), next_summary) <= 0) {
            print_status();
            next_summary = make_timeout_time_ms(SUMMARY_PERIOD_S * 1000);
        }
#endif
        sleep_us(500);
    }
}
