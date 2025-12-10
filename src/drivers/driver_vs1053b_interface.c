/**
 * Copyright (c) 2015 - present LibDriver All rights reserved
 * 
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. 
 *
 * @file      driver_vs1053b_interface_template.c
 * @brief     driver vs1053b interface template source file
 * @version   1.0.0
 * @author    Shifeng Li
 * @date      2023-06-30
 *
 * <h3>history</h3>
 * <table>
 * <tr><th>Date        <th>Version  <th>Author      <th>Description
 * <tr><td>2023/06/30  <td>1.0      <td>Shifeng Li  <td>first upload
 * </table>
 */

#include "driver_vs1053b_interface.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <stdarg.h>

/* --- 1. USER CONFIGURATION: PINOUT --- */
// Update these to match your board wiring!
#define VS_SPI_PORT         spi1    // CHANGED: spi0 -> spi1

// SPI Bus Pins
#define VS_PIN_SCK          26      // SPI1 SCK (Hardware enforced)
#define VS_PIN_TX           27      // SPI1 TX / MOSI
#define VS_PIN_RX           28      // SPI1 RX / MISO

// Control Pins
#define VS_PIN_XCS          29      // Command Chip Select 
#define VS_PIN_XDCS         30      // Data Chip Select 
#define VS_PIN_DREQ         31      // Data Request 
#define VS_PIN_RST          32      // Reset

/* --- 2. USER CONFIGURATION: FILE SYSTEM --- */
// Set to 1 if you have FatFS (ff.h) set up. Set to 0 to compile without SD card support.
#define USE_FATFS           0  

#if USE_FATFS
#include "ff.h"
static FIL g_file_object;
#endif

// VS1053B Opcodes (internal usage for the interface wrappers)
#define VS_OPCODE_READ      0x03
#define VS_OPCODE_WRITE     0x02

/**
 * @brief  interface spi cmd bus init
 * @note   This initializes the physical SPI bus and the Chip Select pins.
 */
uint8_t vs1053b_interface_spi_cmd_init(void)
{
    // Initialize SPI at 2MHz (VS1053 starts slow until clock multiplier is set)
    spi_init(VS_SPI_PORT, 2000 * 1000);
    
    // Setup SPI Pins
    gpio_set_function(VS_PIN_TX, GPIO_FUNC_SPI);
    gpio_set_function(VS_PIN_RX, GPIO_FUNC_SPI);
    gpio_set_function(VS_PIN_SCK, GPIO_FUNC_SPI);

    // Setup Chip Select Pins (Manual Control)
    gpio_init(VS_PIN_XCS);
    gpio_set_dir(VS_PIN_XCS, GPIO_OUT);
    gpio_put(VS_PIN_XCS, 1); // Deselect (High)

    gpio_init(VS_PIN_XDCS);
    gpio_set_dir(VS_PIN_XDCS, GPIO_OUT);
    gpio_put(VS_PIN_XDCS, 1); // Deselect (High)

    return 0;
}

/**
 * @brief  interface spi cmd bus deinit
 */
uint8_t vs1053b_interface_spi_cmd_deinit(void)
{
    spi_deinit(VS_SPI_PORT);
    return 0;
}

/**
 * @brief      interface spi cmd bus read
 * @note       Reads a 16-bit register. Protocol: [XCS LOW] -> [0x03] -> [Addr] -> [Read 2 bytes] -> [XCS HIGH]
 */
uint8_t vs1053b_interface_spi_cmd_read_address16(uint16_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t header[2];
    header[0] = VS_OPCODE_READ;
    header[1] = (uint8_t)reg;

    gpio_put(VS_PIN_XCS, 0); // Assert Command CS
    
    spi_write_blocking(VS_SPI_PORT, header, 2);
    spi_read_blocking(VS_SPI_PORT, 0, buf, len);
    
    gpio_put(VS_PIN_XCS, 1); // Deassert Command CS
    
    return 0;
}

/**
 * @brief      interface spi cmd bus write
 * @note       Writes a 16-bit register. Protocol: [XCS LOW] -> [0x02] -> [Addr] -> [Write 2 bytes] -> [XCS HIGH]
 */
uint8_t vs1053b_interface_spi_cmd_write_address16(uint16_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t header[2];
    header[0] = VS_OPCODE_WRITE;
    header[1] = (uint8_t)reg;

    gpio_put(VS_PIN_XCS, 0); // Assert Command CS
    
    spi_write_blocking(VS_SPI_PORT, header, 2);
    spi_write_blocking(VS_SPI_PORT, buf, len);
    
    gpio_put(VS_PIN_XCS, 1); // Deassert Command CS

    return 0;
}

/**
 * @brief  interface spi dat bus init
 * @note   We already init SPI in cmd_init, so we just return success.
 */
uint8_t vs1053b_interface_spi_dat_init(void)
{
    return 0;
}

/**
 * @brief  interface spi dat bus deinit
 */
uint8_t vs1053b_interface_spi_dat_deinit(void)
{
    return 0;
}

/**
 * @brief      interface spi dat bus write command
 * @note       Sends audio data. Protocol: [XDCS LOW] -> [Data...] -> [XDCS HIGH]
 */
uint8_t vs1053b_interface_spi_dat_write_cmd(uint8_t *buf, uint16_t len)
{
    gpio_put(VS_PIN_XDCS, 0); // Assert Data CS
    
    spi_write_blocking(VS_SPI_PORT, buf, len);
    
    gpio_put(VS_PIN_XDCS, 1); // Deassert Data CS
    
    return 0;
}

/**
 * @brief      interface audio init
 * @note       Opens the file using FatFS
 */
uint8_t vs1053b_interface_audio_init(uint8_t type, char *name, uint32_t *size)
{
#if USE_FATFS
    if (type == VS1053B_MODE_PLAY) {
        if (f_open(&g_file_object, name, FA_READ) != FR_OK) return 1;
        *size = f_size(&g_file_object);
    } else {
        // Record mode
        if (f_open(&g_file_object, name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 1;
    }
    return 0;
#else
    return 1; // Error: File system not enabled
#endif
}

/**
 * @brief      interface audio read
 */
uint8_t vs1053b_interface_audio_read(uint32_t addr, uint16_t size, uint8_t *buffer)
{
#if USE_FATFS
    UINT br;
    f_lseek(&g_file_object, addr); // Seek to position
    if (f_read(&g_file_object, buffer, size, &br) != FR_OK) return 1;
    return 0;
#else
    return 1;
#endif
}

/**
 * @brief      interface audio write
 */
uint8_t vs1053b_interface_audio_write(uint32_t addr, uint16_t size, uint8_t *buffer)
{
#if USE_FATFS
    UINT bw;
    if (f_write(&g_file_object, buffer, size, &bw) != FR_OK) return 1;
    return 0;
#else
    return 1;
#endif
}

/**
 * @brief      interface audio deinit
 */
uint8_t vs1053b_interface_audio_deinit(void)
{
#if USE_FATFS
    f_close(&g_file_object);
#endif
    return 0;
}

/**
 * @brief      interface timestamp read
 * @note       Implementation optional for basic playback
 */
void vs1053b_interface_timestamp_read(uint32_t *sec, uint32_t *us)
{
    uint64_t time_us = time_us_64();
    *sec = time_us / 1000000;
    *us = time_us % 1000000;
}

/**
 * @brief  interface reset gpio init
 */
uint8_t vs1053b_interface_reset_gpio_init(void)
{
    gpio_init(VS_PIN_RST);
    gpio_set_dir(VS_PIN_RST, GPIO_OUT);
    gpio_put(VS_PIN_RST, 1); // Start high (not reset)
    return 0;
}

/**
 * @brief  interface reset gpio deinit
 */
uint8_t vs1053b_interface_reset_gpio_deinit(void)
{
    return 0;
}

/**
 * @brief      interface reset gpio write
 */
uint8_t vs1053b_interface_reset_gpio_write(uint8_t data)
{
    gpio_put(VS_PIN_RST, data);
    return 0;
}

/**
 * @brief  interface dreq gpio init
 */
uint8_t vs1053b_interface_dreq_gpio_init(void)
{
    gpio_init(VS_PIN_DREQ);
    gpio_set_dir(VS_PIN_DREQ, GPIO_IN);
    // DREQ is usually driven high/low by the chip, no pull-up needed usually.
    return 0;
}

/**
 * @brief  interface dreq gpio deinit
 */
uint8_t vs1053b_interface_dreq_gpio_deinit(void)
{
    return 0;
}

/**
 * @brief      interface dreq gpio read
 */
uint8_t vs1053b_interface_dreq_gpio_read(uint8_t *data)
{
    *data = gpio_get(VS_PIN_DREQ);
    return 0;
}

/**
 * @brief      interface delay ms
 */
void vs1053b_interface_delay_ms(uint32_t ms)
{
    sleep_ms(ms);
}

/**
 * @brief     interface print format data
 */
void vs1053b_interface_debug_print(const char *const fmt, ...)
{
    char str[256];
    uint16_t len;
    va_list args;
    
    va_start(args, fmt);
    len = vsnprintf(str, 254, fmt, args);
    va_end(args);
    
    printf("%s", str); // Print to USB/UART
}

/**
 * @brief      interface receive callback
 * @note       Keep this as is (copy from your template)
 */
void vs1053b_interface_receive_callback(uint8_t type, uint32_t cur_pos)
{
    switch (type)
    {
        case VS1053B_TYPE_PLAY_READ :
        {
           // vs1053b_interface_debug_print("vs1053b: irq read data during playing with %d.\n", cur_pos);
            break;
        }
        case VS1053B_TYPE_PLAY_WRITE :
        {
           // vs1053b_interface_debug_print("vs1053b: irq write data during playing with %d.\n", cur_pos);
            break;
        }
        case VS1053B_TYPE_PLAY_END :
        {
            vs1053b_interface_debug_print("vs1053b: irq play end with %d.\n", cur_pos);
            break;
        }
        case VS1053B_TYPE_RECORD_READ :
        {
            vs1053b_interface_debug_print("vs1053b: irq read data during recording with %d.\n", cur_pos);
            break;
        }
        case VS1053B_TYPE_RECORD_WRITE :
        {
            vs1053b_interface_debug_print("vs1053b: irq write data during recording with %d.\n", cur_pos);
            break;
        }
        case VS1053B_TYPE_RECORD_END :
        {
            vs1053b_interface_debug_print("vs1053b: irq record end with %d.\n", cur_pos);
            break;
        }
        case VS1053B_TYPE_RECORD_OVER :
        {
            vs1053b_interface_debug_print("vs1053b: irq record over buffer with %d.\n", cur_pos);
            break;
        }
        case VS1053B_TYPE_PERIOD :
        {
            // vs1053b_interface_debug_print("vs1053b: irq period.\n");
            break;
        }
        default :
        {
            vs1053b_interface_debug_print("vs1053b: unknown type.\n");
            break;
        }
    }
}