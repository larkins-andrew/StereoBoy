/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/interp.h"

#include "main.pio.h"
#include "raspberry_256x256_rgb565.h"

// Tested with the parts that have the height of 240 and 320
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define IMAGE_SIZE 256
#define LOG_IMAGE_SIZE 8

#define PIN_DIN 0
#define PIN_CLK 1
#define PIN_CS 2
#define PIN_DC 3
#define PIN_RESET 4
#define PIN_BL 5

// Define some 16-bit RGB565 colors
#define BLACK   0x0000
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define WHITE   0xFFFF
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F

// Define ST7789 commands
#define ST7789_CMD_CASET 0x2A
#define ST7789_CMD_RASET 0x2B
#define ST7789_CMD_RAMWR 0x2C

#define SERIAL_CLK_DIV 1.f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Format: cmd length (including cmd byte), post delay in units of 5 ms, then cmd payload
// Note the delays have been shortened a little
static const uint8_t st7789_init_seq[] = {
        1, 20, 0x01,                        // Software reset
        1, 10, 0x11,                        // Exit sleep mode
        2, 2, 0x3a, 0x55,                   // Set colour mode to 16 bit
        2, 0, 0x36, 0x00,                   // Set MADCTL: row then column, refresh is bottom to top ????
        5, 0, 0x2a, 0x00, 0x00, SCREEN_WIDTH >> 8, SCREEN_WIDTH & 0xff,   // CASET: column addresses
        5, 0, 0x2b, 0x00, 0x00, SCREEN_HEIGHT >> 8, SCREEN_HEIGHT & 0xff, // RASET: row addresses
        1, 2, 0x21,                         // Inversion on, then 10 ms delay (supposedly a hack?)
        1, 2, 0x13,                         // Normal display on, then 10 ms delay
        1, 2, 0x29,                         // Main screen turn on, then wait 500 ms
        0                                   // Terminate list
};

static inline void lcd_set_dc_cs(bool dc, bool cs) {
    sleep_us(1);
    gpio_put_masked((1u << PIN_DC) | (1u << PIN_CS), !!dc << PIN_DC | !!cs << PIN_CS);
    sleep_us(1);
}

static inline void lcd_write_cmd(PIO pio, uint sm, const uint8_t *cmd, size_t count) {
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(0, 0);
    st7789_lcd_put(pio, sm, *cmd++);
    if (count >= 2) {
        st7789_lcd_wait_idle(pio, sm);
        lcd_set_dc_cs(1, 0);
        for (size_t i = 0; i < count - 1; ++i)
            st7789_lcd_put(pio, sm, *cmd++);
    }
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(1, 1);
}

static inline void lcd_init(PIO pio, uint sm, const uint8_t *init_seq) {
    const uint8_t *cmd = init_seq;
    while (*cmd) {
        lcd_write_cmd(pio, sm, cmd + 2, *cmd);
        sleep_ms(*(cmd + 1) * 5);
        cmd += *cmd + 2;
    }
}


// Tells controller done sending command data, start writing the pixels
static inline void st7789_start_pixels(PIO pio, uint sm) {
    uint8_t cmd = 0x2c; // RAMWR
    lcd_write_cmd(pio, sm, &cmd, 1);
    lcd_set_dc_cs(1, 0);
}


// Helper to send a 16-bit color to the PIO since lcd expect colors as 2 8-bit chunks
static inline void st7789_lcd_put16(PIO pio, uint sm, uint16_t color) {
    st7789_lcd_put(pio, sm, color >> 8);
    st7789_lcd_put(pio, sm, color & 0xFF);
}

/**
 * Defines a drawing window based on starting (x,y) location and width of rectangle. This allows the micro to stream pixel 
 * packets one after another to the LCD without having to give it specific locations
 * 
 * 
 * @param pio
 * @param sm
 * @param x Starting X location
 * @param y Starting y location
 * @param w Width of window
 * @param h height of window
 * 
 */
static inline void lcd_set_window(PIO pio, uint sm, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t x_end = x + w - 1;
    uint16_t y_end = y + h - 1;

    // CASET (Column Address Set)
    uint8_t caset_cmd[] = {
        ST7789_CMD_CASET,
        (uint8_t)(x >> 8),
        (uint8_t)(x & 0xFF),
        (uint8_t)(x_end >> 8),
        (uint8_t)(x_end & 0xFF)
    };
    lcd_write_cmd(pio, sm, caset_cmd, sizeof(caset_cmd));

    // RASET (Row Address Set)
    uint8_t raset_cmd[] = {
        ST7789_CMD_RASET,
        (uint8_t)(y >> 8),
        (uint8_t)(y & 0xFF),
        (uint8_t)(y_end >> 8),
        (uint8_t)(y_end & 0xFF)
    };
    lcd_write_cmd(pio, sm, raset_cmd, sizeof(raset_cmd));
}

/**
 * Draws a rectangle starting (x,y) location and width of rectangle.
 * 
 * @param pio
 * @param sm
 * @param x starting x location 
 * @param y starting y location
 * @param w width of rectangle
 * @param h height of rectangle
 * @param color 16-bit RGB565 colors i.e. 0xF81F
 * 
 */
void lcd_draw_rect(PIO pio, uint sm, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    // set the window
    lcd_set_window(pio, sm, x, y, w, h);

    //start pixel write
    st7789_start_pixels(pio, sm);

    //stream pixels one after the other
    uint32_t num_pixels = (uint32_t)w * h;
    for (uint32_t i = 0; i < num_pixels; ++i) {
        st7789_lcd_put16(pio, sm, color);
    }

    //End command
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(1, 1); // Deselect CS
}

/**
 * Draws a pixel at location (x,y).
 * 
 * @param pio
 * @param sm
 * @param x x location 
 * @param y  y location
 * @param color 16-bit RGB565 colors i.e. 0xF81F
 * 
 */
void lcd_draw_pixel(PIO pio, uint sm, uint16_t x, uint16_t y, uint16_t color){
    lcd_draw_rect(pio, sm, x, y, 1, 1, color);
}

/**
    * Draws a string
    * 
    * @param pio
    * @param sm
    * @param x x starting locaiton
    * @param y  y starting location
    * @param squareSize size of square string is in
    * @param string array of 16-bit color values
    * 
*/
void lcd_draw_string(PIO pio, uint sm, uint16_t x, uint16_t y, uint16_t squareSize, uint16_t* string){
    // set the window
    lcd_set_window(pio, sm, x, y, squareSize, squareSize);

    //start pixel write
    st7789_start_pixels(pio, sm);

    //stream pixels one after the other
    uint32_t num_pixels = (uint32_t)squareSize * squareSize;
    for (uint32_t i = 0; i < num_pixels; ++i) {
        st7789_lcd_put16(pio, sm, string[i]);
    }

    //End command
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(1, 1); // Deselect CS
}


//given a string with


int main() {
    stdio_init_all();

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &st7789_lcd_program);
    st7789_lcd_program_init(pio, sm, offset, PIN_DIN, PIN_CLK, SERIAL_CLK_DIV);

    gpio_init(PIN_CS);
    gpio_init(PIN_DC);
    gpio_init(PIN_RESET);
    gpio_init(PIN_BL);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_set_dir(PIN_BL, GPIO_OUT);

    gpio_put(PIN_CS, 1);
    gpio_put(PIN_RESET, 1);
    lcd_init(pio, sm, st7789_init_seq);
    gpio_put(PIN_BL, 1);

    
    lcd_draw_rect(pio, sm, 0, 0, 240, 240, BLACK);
    lcd_draw_rect(pio, sm, 0, 0, 24, 24, GREEN);
    lcd_draw_rect(pio, sm, 60, 60, 100, 50, RED);
    lcd_draw_pixel(pio, sm, 0, 0, MAGENTA);
   
    while (1) {
        
    }
}
