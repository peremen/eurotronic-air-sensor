#pragma once
// ---- I2C sniffer input (PIO, passive): SDA = GP4, SCL = GP5 (must be consecutive) --------------------------
#define PIN_SDA            4
#define SAMPLE_RATE_HZ     2000000u      // 20 samples per bit at 100 kHz
#define BUF_WORDS          2048          // 16 samples per word
#define NBUF               12            // DMA ring: ~200 ms of slack at 2 MS/s

// ---- SD card on SPI0 (Catalex-style module, 5 V on VCC) ------------------------------------------------------
#define SD_PIN_MISO        0
#define SD_PIN_CS          1
#define SD_PIN_SCK         2
#define SD_PIN_MOSI        3
#define SD_SPI_BAUD        4000000

// ---- logging -----------------------------------------------------------------------------------------------------
#define LOG_INTERVAL_S     10            // one CSV line per interval (peak values included), no assumption about the sensor cadence
#define SD_RETRY_S         30            // remount attempt interval after a card error
#define SD_LOG_DIR         "LOG"
#define SD_LOG_PREFIX      "aq_"
#define RECORD_QUEUE_LEN   360           // records buffered in RAM while the card is missing (1 hour at 10 s)
#define SUMMARY_PERIOD_S   600           // automatic command summary on the serial port (display only), 0 = off
#define USB_WAIT_MS        5000          // wait this long for a serial terminal at start, then run unattended
