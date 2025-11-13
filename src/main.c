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
#include "codec.h"
#include "driverInterface.h"
#include "font_13_24.hh"
#include "source.h"

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
#define BLACK 0x0000
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define WHITE 0xFFFF
#define YELLOW 0xFFE0
#define CYAN 0x07FF
#define MAGENTA 0xF81F

// Define ST7789 commands
#define ST7789_CMD_CASET 0x2A
#define ST7789_CMD_RASET 0x2B
#define ST7789_CMD_RAMWR 0x2C

#define SERIAL_CLK_DIV 1.f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint8_t res;
volatile uint8_t gs_flag = 0;
volatile uint8_t gs_mode = 0;

static void a_callback(uint8_t type, uint32_t cur_pos)
{
    switch (type)
    {
    case VS1053B_TYPE_PLAY_READ:
    {
        break;
    }
    case VS1053B_TYPE_PLAY_WRITE:
    {
        break;
    }
    case VS1053B_TYPE_PLAY_END:
    {
        gs_flag = 1;
        vs1053b_interface_debug_print("vs1053b: play end.\n");

        break;
    }
    case VS1053B_TYPE_RECORD_READ:
    {
        break;
    }
    case VS1053B_TYPE_RECORD_WRITE:
    {
        break;
    }
    case VS1053B_TYPE_RECORD_END:
    {
        vs1053b_interface_debug_print("vs1053b: irq record end with %d.\n", cur_pos);

        break;
    }
    case VS1053B_TYPE_RECORD_OVER:
    {
        vs1053b_interface_debug_print("vs1053b: irq record over buffer with %d.\n", cur_pos);

        break;
    }
    case VS1053B_TYPE_PERIOD:
    {
        if (gs_mode == 1)
        {
            uint8_t res;
            uint16_t decode_time;
            uint16_t rate;
            vs1053b_channel_t channel;
            vs1053b_audio_info_t info;

            /* get decode time */
            res = vs1053b_basic_get_decode_time(&decode_time);
            if (res == 0)
            {
                vs1053b_interface_debug_print("\nvs1053b: play time is %02d:%02d:%02d.\n",
                                              (decode_time % 86400) / 3600, (decode_time % 3600) / 60, decode_time % 60);
            }

            /* get byte rate */
            res = vs1053b_basic_get_bytes_rate(&rate);
            if (res == 0)
            {
                vs1053b_interface_debug_print("vs1053b: byte rate is %d bytes/sec.\n", rate);
            }

            /* get sample rate */
            res = vs1053b_basic_get_sample_rate(&rate, &channel);
            if (res == 0)
            {
                vs1053b_interface_debug_print("vs1053b: sample rate is %dHz, channel is %d.\n", rate, (uint8_t)(channel + 1));
            }

            /* get info */
            res = vs1053b_basic_get_info(&info);
            if (res == 0)
            {
                vs1053b_interface_debug_print("vs1053b: format is %s, rate is %0.0fkbps.\n", info.format_name, info.kbps);
            }
        }
        if (gs_mode == 2)
        {
            (void)vs1053b_basic_stop();
            gs_flag = 1;
            vs1053b_interface_debug_print("vs1053b: up to record time.\n");
        }

        break;
    }
    default:
    {
        vs1053b_interface_debug_print("vs1053b: unknown type.\n");

        break;
    }
    }
}

int main()
{
    stdio_init_all();

    PIO pio = pio1;
    uint sm = 0;
    // uint offset = pio_add_program(pio, &st7789_lcd_program);
    // st7789_lcd_program_init(pio, sm, offset, PIN_DIN, PIN_CLK, SERIAL_CLK_DIV);

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
    // lcd_init(pio, sm, st7789_init_seq);
    gpio_put(PIN_BL, 1);

    /* play init */
    res = vs1053b_basic_init(VS1053B_MODE_PLAY, VS1053B_RECORD_FORMAT_WAV, a_callback);
    if (res != 0)
    {
        return 1;
    }

    /* set timeout */
    res = vs1053b_basic_set_callback_period(5);
    if (res != 0)
    {
        (void)vs1053b_basic_deinit();

        return 1;
    }

    /* play audio */
    res = vs1053b_basic_play("0:test.mp3");
    if (res != 0)
    {
        (void)vs1053b_basic_deinit();

        return 1;
    }

    /* clear flag */
    gs_flag = 0;

    /* play */
    gs_mode = 1;

    /* run the server and wait for the end */
    while (gs_flag == 0)
    {
        (void)vs1053b_basic_service();
    }

    /* deinit */
    (void)vs1053b_basic_deinit();

    return 0;

    while (1)
    {
    }
}
