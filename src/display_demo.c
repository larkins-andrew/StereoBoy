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

#include "lib/font/font.h"
#include "lib/display/display.h"

#include "main.pio.h"
#include "lib/images/raspberry_256x256_rgb565.h"

uint16_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT] = {0};  // Each element is one pixel (RGB565)




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
    lcd_draw_circle(120,120, 16, GREEN, framebuffer);
    lcd_draw_circle_fill(120, 180, 33, rgbto565(0xFF3399), framebuffer);
    lcd_draw_string(80, 80, "Shubham Was Here", BLUE, framebuffer);
    lcd_draw_char(10, 10, 'B', CYAN, framebuffer);
    lcd_update(pio, sm, framebuffer);
    lcd_draw_progress_bar(pio, sm, 200, 46, framebuffer);
    
    while (1) {
        
    }
}
