// Hardware description for the no-OS-FatFS-SD-SDIO-SPI-RPi-Pico library: one SD card on SPI0.
#include "hw_config.h"
#include "config.h"

static spi_t spi = {
    .hw_inst   = spi0,
    .sck_gpio  = SD_PIN_SCK,
    .mosi_gpio = SD_PIN_MOSI,
    .miso_gpio = SD_PIN_MISO,
    .baud_rate = SD_SPI_BAUD,
};

static sd_spi_if_t spi_if = {
    .spi    = &spi,
    .ss_gpio = SD_PIN_CS,
};

static sd_card_t sd_card = {
    .type     = SD_IF_SPI,
    .spi_if_p = &spi_if,
};

size_t sd_get_num() { return 1; }

sd_card_t *sd_get_by_num(size_t num) { return num == 0 ? &sd_card : NULL; }
