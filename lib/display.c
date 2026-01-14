/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "display.h"

#include "font.h"

#include "main.pio.h"
// #include "raspberry_256x256_rgb565.h"

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


static inline uint16_t rgbto565(int RGB){
    uint8_t R = RGB>>16;
    uint8_t G = RGB>>8;
    uint8_t B = RGB;

    // Source - https://stackoverflow.com/a
// Posted by Paul R, modified by community. See post 'Timeline' for change history
// Retrieved 2025-11-09, License - CC BY-SA 4.0
    return (((R & 0b11111000) << 8) | ((G & 0b11111100) << 3) | (B >> 3));
}

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
// void lcd_draw_string(PIO pio, uint sm, uint16_t x, uint16_t y, uint16_t squareSize, uint16_t* string){
//     // set the window
//     lcd_set_window(pio, sm, x, y, squareSize, squareSize);

//     //start pixel write
//     st7789_start_pixels(pio, sm);

//     //stream pixels one after the other
//     uint32_t num_pixels = (uint32_t)squareSize * squareSize;
//     for (uint32_t i = 0; i < num_pixels; ++i) {
//         st7789_lcd_put16(pio, sm, string[i]);
//     }

//     //End command
//     st7789_lcd_wait_idle(pio, sm);
//     lcd_set_dc_cs(1, 1); // Deselect CS
// }


//given a string with

void lcd_draw_progress_bar(PIO pio, uint sm, int length, int progress){
    lcd_draw_rect(pio, sm, 32, 32, 176, 16, rgbto565(LIGHT_GRAY));
    uint16_t prog = (progress/(1.0*length))*176;
    lcd_draw_rect(pio, sm, 176+32-prog, 32, prog, 16, rgbto565(GRAY));
}

void set_pixel(uint16_t x, uint16_t y, uint16_t color) {
    framebuffer[y * SCREEN_WIDTH + x] = color;
}

void lcd_update(PIO pio, uint sm) {
    lcd_set_window(pio, sm, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    st7789_start_pixels(pio, sm);

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        st7789_lcd_put16(pio, sm, framebuffer[i]);
    }

    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(1, 1);
}

void lcd_draw_circle (uint16_t x, uint16_t y, uint8_t radius, uint16_t color){
    float theta = 0.5;
    uint16_t writex;
    uint16_t writey;
    for (int i = 0; i<360/theta; i++){
        writex = cosf(theta*i)*radius+x;
        writey = sinf(theta*i)*radius+y;
        if (0<=writex && 240>writex && 0<=writey && 240>writey){
            set_pixel(writex, writey, color);
        }
    }
}
void lcd_draw_circle_fill(uint16_t x, uint16_t y, uint8_t radius, uint16_t color){
    float theta = 0.5;
    uint16_t writex;
    uint16_t writey;
    for (int r=0; r<=radius; r++){
        for (int i = 0; i<360/theta; i++){
            writex = cosf(theta*i)*r+x;
            writey = sinf(theta*i)*r+y;
            if (0<=writex && 240>writex && 0<=writey && 240>writey){
                set_pixel(writex, writey, color);
            }
        }
    }
}


const struct Font* find_font_char(char c) {
    for (int i = 0; font[i].letter != 0; i++) {
        if (font[i].letter == c) return &font[i];
    }
    return NULL;
}

void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color) {
    const struct Font* f = find_font_char(c);
    if (!f) return;
    for (uint8_t row = 0; row < 7; row++) {
        for (uint8_t col = 0; col < 5; col++) {
            if (f->code[row][col] == '1') {
                set_pixel(x + col, y + row, color);
            }
        }
    }
}

void lcd_draw_string(uint16_t x, uint16_t y, const char *text, uint16_t color) {
    uint16_t start_x = x;
    for (int i = 0; text[i] != '\0'; i++) {
        lcd_draw_char(start_x, y, text[i], color);
        start_x += 6;
    }
}



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

    
    // lcd_draw_rect(pio, sm, 0, 0, 240, 240, WHITE);
    // lcd_draw_rect(pio, sm, 0, 0, 60, 60, rgbto565(GRAY));
    lcd_draw_circle(120,120, 16, GREEN);
    lcd_draw_circle_fill(120, 180, 33, rgbto565(0xFF3399));
    // lcd_draw_string(80, 80, "Shubham Was Here", BLUE);
    // lcd_draw_char(10, 10, 'B', CYAN);
    lcd_update(pio, sm);
    lcd_draw_progress_bar(pio, sm, 200, 46);
    
}
