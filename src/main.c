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
#include "hardware/spi.h"


#include "main.pio.h"
#include "raspberry_256x256_rgb565.h"
#include "drivers/driver_vs1053b_basic.h"
#include "drivers/driver_vs1053b_interface.h"
#include "font_13_24.hh"
#include "drivers/driver_vs1053b.h"


#define SERIAL_CLK_DIV 1.f

// Global flag to signal when the VS1053B initialization is done
volatile int init_done = 0;

// Simple dummy callback required by the library initialization
static void dummy_callback(uint8_t type, uint32_t cur_pos) {
    // We don't need this for the connection test
}

static void LEDBlink(uint LED){
    gpio_put(LED, 1);
    sleep_ms(500);
    gpio_put(LED, 0);
    sleep_ms(500);
}

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

    // 1. Init RP2350 System
    stdio_init_all();
    
    // 2. Wait for you to open the Serial Monitor
    sleep_ms(3000); 
    printf("\n\n--- RP2350 VS1053B Connection Test ---\n");

    // 3. Initialize the VS1053B using the Basic driver
    // This will call your 'interface_init' and 'spi_init' functions automatically
    printf("Initializing VS1053B...\n");
    uint8_t res = vs1053b_basic_init(VS1053B_MODE_PLAY, VS1053B_RECORD_FORMAT_WAV, dummy_callback);

    if (res != 0) {
        printf("FAILED: vs1053b_basic_init returned error %d\n", res);
        printf("Check your wiring, specifically RST and DREQ pins.\n");
        return 1;
    }
    printf("Initialization Command Sent.\n");

    // 4. Verify Communication by reading a register
    // We will read the MODE register or similar. 
    // The library doesn't expose a raw 'read_register' in the basic header easily,
    // so we will ask for the 'decode time' which forces an SPI read.
    
    uint16_t decode_time;
    res = vs1053b_basic_get_decode_time(&decode_time);
    //Success
    if (res == 0) {
        const uint LED2 = 24;
        gpio_init(LED2);
        gpio_set_dir(LED2, GPIO_OUT);
       LEDBlink(LED2);
    //Failure
    } else {
        const uint LED3 = 23;
        gpio_init(LED3);
        gpio_set_dir(LED3, GPIO_OUT);
       LEDBlink(LED3);
    }

    // 5. Blink LED to show life
    const uint LED_PIN = 25; // Standard Pico LED (Check if Pico 2 uses 25)
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    while (true) {
        LEDBlink(LED_PIN);
    }
}
