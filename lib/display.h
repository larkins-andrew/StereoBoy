#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/interp.h"

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

#define BLACK   0x0000
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define WHITE   0xFFFF
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F

#define GRAY        0x888888
#define LIGHT_GRAY  0x444444

#define ST7789_CMD_CASET 0x2A
#define ST7789_CMD_RASET 0x2B
#define ST7789_CMD_RAMWR 0x2C

#define SERIAL_CLK_DIV 1.f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern uint16_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
extern const uint8_t st7789_init_seq[];

struct Font; //???? TODO

uint16_t rgbto565(int RGB);

void lcd_init(PIO pio, uint sm, const uint8_t *init_seq);

void lcd_update(PIO pio, uint sm);

void set_pixel(uint16_t x, uint16_t y, uint16_t color);

void lcd_draw_pixel(PIO pio, uint sm, uint16_t x, uint16_t y, uint16_t color);
void lcd_draw_rect(PIO pio, uint sm, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_draw_circle(uint16_t x, uint16_t y, uint8_t radius, uint16_t color);
void lcd_draw_circle_fill(uint16_t x, uint16_t y, uint8_t radius, uint16_t color);
void lcd_draw_progress_bar(PIO pio, uint sm, int length, int progress);

void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color);
void lcd_draw_string(uint16_t x, uint16_t y, const char *text, uint16_t color);

void lcd_set_dc_cs(bool dc, bool cs);
void lcd_write_cmd(PIO pio, uint sm, const uint8_t *cmd, size_t count);
void lcd_set_window(PIO pio, uint sm, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

#endif // DISPLAY_H