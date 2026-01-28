#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include <stdint.h>

#define TLV_RESET_PIN 5
#define DAC_I2C_ADDR 0x18

#define DAC_VOL_MIN   0x06   // ~ -4 dB (pretty loud)
#define DAC_VOL_MAX   0x60   // ~ -48 dB (very quiet)
#define DAC_VOL_STEP  3      // 1.5 dB step

uint8_t dac_read(uint8_t page, uint8_t reg) {
    uint8_t val;

    // 1. Select page
    uint8_t page_buf[2] = {0x00, page};
    i2c_write_blocking(i2c0, DAC_I2C_ADDR, page_buf, 2, false);

    // 2. Write register address
    i2c_write_blocking(i2c0, DAC_I2C_ADDR, &reg, 1, true);  // no stop, repeated start

    // 3. Read back value
    i2c_read_blocking(i2c0, DAC_I2C_ADDR, &val, 1, false);

    return val;
}

// Helper to write a register on the DAC
static void dac_write(uint8_t page, uint8_t reg, uint8_t val) {
    // Set page first
    i2c_write_blocking(i2c0, DAC_I2C_ADDR, (uint8_t[]){0x00, page}, 2, false);
    // Write register
    i2c_write_blocking(i2c0, DAC_I2C_ADDR, (uint8_t[]){reg, val}, 2, false);
}

static uint8_t dac_volume = 0x20;

static void dac_apply_volume(uint8_t vol) {
    dac_write(1, 0x24, vol);
    dac_write(1, 0x25, vol);
}

void dac_increase_volume(void) {
    if (dac_volume > DAC_VOL_MIN + DAC_VOL_STEP) {
        dac_volume -= DAC_VOL_STEP;
    } else {
        dac_volume = DAC_VOL_MIN;
    }

    dac_apply_volume(dac_volume);
}

void dac_decrease_volume(void) {
    if (dac_volume < DAC_VOL_MAX - DAC_VOL_STEP) {
        dac_volume += DAC_VOL_STEP;
    } else {
        dac_volume = DAC_VOL_MAX;
    }

    dac_apply_volume(dac_volume);
}

void dac_set_volume(uint8_t vol) {
    if (vol < DAC_VOL_MIN) vol = DAC_VOL_MIN;
    if (vol > DAC_VOL_MAX) vol = DAC_VOL_MAX;

    dac_volume = vol;
    dac_apply_volume(dac_volume);
}

void dac_init() {
    // 1. Hardware Reset
    gpio_init(TLV_RESET_PIN);
    gpio_set_dir(TLV_RESET_PIN, GPIO_OUT);
    gpio_put(TLV_RESET_PIN, 0);
    sleep_ms(100);
    gpio_put(TLV_RESET_PIN, 1);
    sleep_ms(10);

    // 2. Software Reset (Page 0, Reg 1)
    dac_write(0, 0x01, 0x01);
    sleep_ms(10);

    // 3. Interface Control (I2S, 16-bit)
    // Reg 0x1B: 0x00 = I2S, 16bit
    dac_write(0, 0x1B, 0x00);

    // 4. Clock Setup (PLL from BCLK)
    // Reg 0x04: CLKOUT Mux. PLL_CLKIN = BCLK (0x07 is typical for BCLK)
    // Your code used setCodecClockInput(PLL) and setPLLClockInput(BCLK)
    dac_write(0, 0x04, 0x07); 

    // 5. PLL Values (P=1, R=2, J=32, D=0)
    // Reg 0x05: P and R. Bit 7=PowerUp. P=1 (bits 6:4), R=2 (bits 3:0)
    // Note: Library uses P=1, but 0 in register often means 1. Check datasheet.
    dac_write(0, 0x05, 0x92); // 1001 0010 -> PLL Power On, P=1, R=2
    dac_write(0, 0x06, 32);   // J=32
    dac_write(0, 0x07, 0);    // D MSB
    dac_write(0, 0x08, 0);    // D LSB

    // 6. DAC Dividers (NDAC=8, MDAC=2)
    dac_write(0, 0x0B, 0x88); // NDAC Power on, Val=8
    dac_write(0, 0x0C, 0x82); // MDAC Power on, Val=2
    // DOSR = 128 (0x0080) - Standard for 44.1/48k
    dac_write(0, 0x0D, 0x00); 
    dac_write(0, 0x0E, 0x80);

    // 7. DAC Data Path
    // Reg 0x3F: Left/Right DAC Power Up, Normal Path
    dac_write(0, 0x3F, 0xD6); 

    // 8. Routing (Page 1)
    // Reg 0x23: DAC to Mixer
    dac_write(1, 0x23, 0x44); 

    // 9. DAC Volume (Page 0)
    dac_write(0, 0x40, 0x00); // Unmute
    dac_write(0, 0x41, 0);   // Left Vol (0dB is usually 0, 18 is +9dB depending on mapping)
    dac_write(0, 0x42, 0);   // Right Vol

    // 10. Headphone & Speaker Setup (Page 1)
    // Reg 0x1F: HP Drivers power up
    dac_write(1, 0x1F, 0xC0); 
    // Reg 0x24/0x25: HPL/R Gain (0x06 = 6dB approx)
    // ADJUST THESE VALUES FOR VOLUME CONTROL IN MAIN FIRMWARE!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    dac_write(1, 0x24, 0x10);
    dac_write(1, 0x25, 0x10);
    // Reg 0x28/0x29: HPL/R Driver unmute
    dac_write(1, 0x28, 0x06); 
    dac_write(1, 0x29, 0x06); 

    // Speaker
    dac_write(1, 0x20, 0x86); // Speaker Power Up, 6dB gain
    dac_write(1, 0x2A, 0x00); // Speaker Unmute / Gain

    // 11. Headset Detect
    dac_write(1, 0x2E, 0x08); // Mic Bias
    dac_write(0, 0x43, 0x80); // Headset detect enable
    dac_write(0, 0x30, 0x80); // INT1 source
    dac_write(0, 0x33, 0x10); // GPIO1 as INT1

    dac_apply_volume(dac_volume);
}