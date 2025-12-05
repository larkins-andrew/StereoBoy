/* hw_config.c */
/* Hardware configuration for the SD Card library (Newer Version) */

#include "hw_config.h"

// 1. Define the SPI Configuration
// Note: We do not define 'dma_isr' here as it is handled internally or not required in your version.
static spi_t my_spi = {
    .hw_inst = spi1,      // RP2350 SPI1 peripheral
    .miso_gpio = 28,      // MISO (RX)
    .mosi_gpio = 27,      // MOSI (TX)
    .sck_gpio = 26,       // SCK
    .baud_rate = 1000 * 1000, // 1 MHz (Start slow)
    // .dma_isr is removed because your library version doesn't use it here
};

// 2. Define the SPI Interface Wrapper
// The CS pin (ss_gpio) has moved HERE in the new library version.
static sd_spi_if_t spi_if = {
    .spi = &my_spi,       // Pointer to the SPI config defined above
    .ss_gpio = 29         // Your Chip Select (CS) pin matches your wiring
};

// 3. Define the SD Card Configuration
static sd_card_t sd_cards[] = {
    {
        // pcName is removed; the index in this array defines the drive number (0 -> "0:")
        .type = SD_IF_SPI,    // Must specify interface type
        .spi_if_p = &spi_if,  // Pointer to the interface wrapper defined in step 2
        .use_card_detect = false
    }
};

// 4. Helper functions required by the library
size_t sd_get_num() { return count_of(sd_cards); }

sd_card_t *sd_get_by_num(size_t num) {
    if (num < sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}