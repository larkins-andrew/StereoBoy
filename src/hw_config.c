/* hw_config.c */
/* This file defines the hardware configuration for the SD Card library */

#include "hw_config.h"

// 1. Define the SPI Configuration
// We are using SPI1 on the pins you selected
static spi_t spis[] = {
    {
        .hw_inst = spi1,      // RP2350 SPI1 peripheral
        .miso_gpio = 28,      // MISO (RX)
        .mosi_gpio = 27,      // MOSI (TX)
        .sck_gpio = 26,       // SCK
        .baud_rate = 1000 * 1000, // 1 MHz (Start slow for stability)
        
        // These are standard defaults for the library, usually required:
        .dma_isr = spi_dma_isr
    }
};

// 2. Define the SD Card Configuration
static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",           // The drive name used in f_mount
        .spi_if_p = &spis[0],     // Pointer to the SPI config defined above
        .ss_gpio = 22,            // Your Chip Select (CS) pin
        .use_card_detect = false, // Assuming you don't use a CD pin
        .card_detect_gpio = 0,    // Unused
        .card_detected_true = 0,  // Unused
        .m_Status = STA_NOINIT
    }
};

// 3. Helper functions required by the library to read these arrays
size_t sd_get_num() { return count_of(sd_cards); }

sd_card_t *sd_get_by_num(size_t num) {
    if (num <= sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}