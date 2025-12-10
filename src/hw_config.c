/* hw_config.c */
#include "hw_config.h"

// 1. SPI CONFIGURATION
static spi_t spis[] = {
    {
        .hw_inst = spi1,          // SPI1 (Pins 26-29)
        .miso_gpio = 28,
        .mosi_gpio = 27,
        .sck_gpio = 26,
        .baud_rate = 100 * 1000, 
        
        // --- REMOVED dma_channel --- 
        .set_drive_strength = true,
        .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .sck_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
    }
};

// 2. SPI INTERFACE CONFIGURATION 
static sd_spi_if_t spi_if0 = {
    .spi = &spis[0],             
    .ss_gpio = 29                
};

// 3. SD CARD CONFIGURATION
static sd_card_t sd_cards[] = {
    {
        // --- REMOVED pcName (Your library version doesn't use it) ---
        
        .type = SD_IF_SPI,        // Interface Type
        .spi_if_p = &spi_if0,     // Pointer to the spi_if_t struct
        
        // CARD DETECT CONFIGURATION
        .use_card_detect = false, 
        .card_detect_gpio = -1    
    }
};

// Standard library functions
size_t sd_get_num() { return count_of(sd_cards); }

sd_card_t *sd_get_by_num(size_t num) {
    if (num <= sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}