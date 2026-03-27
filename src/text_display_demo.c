#include "lib/sb_util/sb_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hw_config.h"
#include "hardware/i2c.h"
#include "lib/display/display.h"

#define MAX_FILENAME_LEN 256 // max filaname character length
#define MAX_TRACKS 64 // max number of mp3 files in sd card

// SPI1 configuration for codec & sd card
#define PIN_SCK  30
#define PIN_MOSI 28
#define PIN_MISO 31
#define PIN_CS   32

// Codec control signals
#define PIN_DCS  33
#define PIN_DREQ 29
#define PIN_RST  27

// I2C0 for DAC
#define PIN_I2C0_SCL 21
#define PIN_I2C0_SDA 20

vs1053_t player = {
    .spi = spi1,
    .cs = PIN_CS,
    .dcs = PIN_DCS,
    .dreq = PIN_DREQ,
    .rst = PIN_RST
};

struct st7789_t display = {
    .spi      = spi0,
    .gpio_din = 35,
    .gpio_clk = 34,
    .gpio_cs  = 37,
    .gpio_dc  = 39,
    .gpio_rst = 4,
    .gpio_bl  = 5,
};

#define LCD_WIDTH  240
#define LCD_HEIGHT 240
int main()
{

    stdio_init_all();

    sleep_ms(3000);

    sb_hw_init(&player, &display);
    set_visualizer(5);
    // --- Print menu ---
    dprint("Main Print 1");
    dprint("Main Print %d", 2);
    sleep_ms(1000);
    dprint("hihi, %s", "thing");

    while(true){
        sleep_ms(10);
    }
