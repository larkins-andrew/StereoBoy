#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include <stdint.h>
#include "pico/time.h"
#include <math.h>


#define DEBOUNCE_US 1000000  // 1000 ms


#define TLV_RESET_PIN 5
#define DAC_I2C_ADDR 0x18


#define DAC_VOL_MIN   0x00
#define DAC_VOL_MAX   0x60
#define DAC_VOL_STEP  3      // 1.5 dB step


#define DAC_INT_GPIO 15   // Pico pin connected to TLV320 GPIO1/INT1


#define SCALE_FACTOR_16 32768.0
#define MAX_16BIT 32767 //2^15 - 1 (16 bits, 2's comp)
#define MIN_16BIT -32768

//Constants for chaning eq
#define NUM_EQ_BANDS 6
#define MAX_GAIN_DB 12.0f
#define MIN_GAIN_DB -12.0f


static uint8_t dac_volume = 0x20; // default DAC volume


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
void dac_write(uint8_t page, uint8_t reg, uint8_t val) {
    // Set page first
    i2c_write_blocking(i2c0, DAC_I2C_ADDR, (uint8_t[]){0x00, page}, 2, false);
    // Write register
    i2c_write_blocking(i2c0, DAC_I2C_ADDR, (uint8_t[]){reg, val}, 2, false);
}


void dac_increase_volume(uint8_t step) {
    if (dac_volume > DAC_VOL_MIN + step) {
        dac_volume -= step;
    } else {
        dac_volume = DAC_VOL_MIN;
    }


    dac_write(1, 0x24, dac_volume);
    dac_write(1, 0x25, dac_volume);
    dac_write(1, 0x26, dac_volume);
}


void dac_decrease_volume(uint8_t step) {
    if (dac_volume < DAC_VOL_MAX - step) {
        dac_volume += step;
    } else {
        dac_volume = DAC_VOL_MAX;
    }


    dac_write(1, 0x24, dac_volume);
    dac_write(1, 0x25, dac_volume);
    dac_write(1, 0x26, dac_volume);
}


void dac_set_volume(uint8_t vol) {
    if (vol < DAC_VOL_MIN) vol = DAC_VOL_MIN;
    if (vol > DAC_VOL_MAX) vol = DAC_VOL_MAX;


    dac_write(1, 0x24, vol);
    dac_write(1, 0x25, vol);
    dac_write(1, 0x26, vol);
}


// gets volume of left headphone
// hopefully left and right are equal
uint8_t dac_get_volume() {
    uint8_t vol = dac_read(1, 0x24);
    // dac_read(1, 0x25);


    return vol & 0b01111111;
}


// Convert float to 2-byte Hex array (MSB, LSB)
void floatToHex16(double value, uint8_t *hexBuffer) {
    // 1. Scale the float
    int32_t fixedPoint = (int32_t)(value * SCALE_FACTOR_16);
   
    // 2. Clamp to 16-bit range (0x7FFF to 0x8000)
    if (fixedPoint > MAX_16BIT) fixedPoint = MAX_16BIT;
    if (fixedPoint < MIN_16BIT) fixedPoint = MIN_16BIT;


    // 3. Store as Big Endian (MSB first, as per your Table 6-121)
    hexBuffer[0] = (fixedPoint >> 8) & 0xFF; // Upper 8 bits (e.g., Reg 2)
    hexBuffer[1] = fixedPoint & 0xFF;        // Lower 8 bits (e.g., Reg 3)
}


// Frequencies (Spotity)
static const float eq_frequencies[NUM_EQ_BANDS] = {60, 150, 400, 1000, 2400, 15000};
// Current Gain State
static float eq_gains[NUM_EQ_BANDS] = {0};

// filterSlot: 0 to 5, 1 for each filter in PRB_P2
// freqHz: Target frequency (e.g., 60, 150, 1000)
// gaindB: Amount to boost/cut (e.g., +3.0 or -5.0)
// Q: Width of the bell curve (1.0 is standard)
void setEQBand(int filterSlot, float freqHz, float gaindB, float Q, float sampleRate) {

    // --- MATH SECTION (Standard RBJ Formulas) ---
    double A = pow(10, gaindB / 40.0);
    double omega = 2.0 * M_PI * freqHz / sampleRate;
    double sn = sin(omega);
    double cs = cos(omega);
    double alpha = sn / (2.0 * Q);

    double b0 = 1 + (alpha * A);
    double b1 = -2 * cs;
    double b2 = 1 - (alpha * A);
    double a0 = 1 + (alpha / A);
    double a1 = -2 * cs;
    double a2 = 1 - (alpha / A);

    // Normalize
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

    // Negate a1/a2 for TI Hardware
    a1 = -a1; a2 = -a2;

    // --- CONVERSION SECTION (Float -> 2 Bytes) ---
    uint8_t n0[2], n1[2], n2[2], d1[2], d2[2];
   
    floatToHex16(b0, n0);
    floatToHex16(b1, n1);
    floatToHex16(b2, n2);
    floatToHex16(a1, d1);
    floatToHex16(a2, d2);

    //WRITE TO MEMORY SECTION:
    // Calculate start register based on the filter
    // Filter 0 -> 2, Filter 1 -> 12, Filter 2 -> 22, etc.
    uint8_t base = 2 + (filterSlot * 10);

    //Write Coefficients sequentially (LEFT)
    // N0 (Registers base, base+1)
    dac_write(8, base + 0, n0[0]);
    dac_write(8, base + 1, n0[1]);
    // N1 (Registers base+2, base+3)
    dac_write(8, base + 2, n1[0]);
    dac_write(8, base + 3, n1[1]);
    // N2 (Registers base+4, base+5)
    dac_write(8, base + 4, n2[0]);
    dac_write(8, base + 5, n2[1]);
    // D1 (Registers base+6, base+7)
    dac_write(8, base + 6, d1[0]);
    dac_write(8, base + 7, d1[1]);
    // D2 (Registers base+8, base+9)
    dac_write(8, base + 8, d2[0]);
    dac_write(8, base + 9, d2[1]);

    //REPEAT FOR RIGHT CHANNEL
    // N0
    dac_write(12, base + 0, n0[0]);
    dac_write(12, base + 1, n0[1]);
    // N1
    dac_write(12, base + 2, n1[0]);
    dac_write(12, base + 3, n1[1]);
    // N2
    dac_write(12, base + 4, n2[0]);
    dac_write(12, base + 5, n2[1]);
    // D1
    dac_write(12, base + 6, d1[0]);
    dac_write(12, base + 7, d1[1]);
    // D2
    dac_write(12, base + 8, d2[0]);
    dac_write(12, base + 9, d2[1]);
}

void dac_eq_init(float sampleRate) {
    // Init all bands to 0
    for(int i=0; i<NUM_EQ_BANDS; i++) {
        eq_gains[i] = 0.0f;
        setEQBand(i, eq_frequencies[i], 0.0f, 1.0f, sampleRate);
    }
}

void dac_eq_adjust(int band, float step_db, float sampleRate) {
    if (band < 0 || band >= NUM_EQ_BANDS) return;

    eq_gains[band] += step_db;

    // prevent going above or below limits
    if (eq_gains[band] > MAX_GAIN_DB) eq_gains[band] = MAX_GAIN_DB;
    if (eq_gains[band] < MIN_GAIN_DB) eq_gains[band] = MIN_GAIN_DB;

    //update registers
    setEQBand(band, eq_frequencies[band], eq_gains[band], 1.0f, sampleRate);
}

//Geters for variables
float dac_eq_get_gain(int band) {
    if (band < 0 || band >= NUM_EQ_BANDS) return 0.0f;
    return eq_gains[band];
}
int dac_eq_get_freq(int band) {
    if (band < 0 || band >= NUM_EQ_BANDS) return 0;
    return (int)eq_frequencies[band];
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

    // Enable PRB_P2 (for EQ)
    dac_write(0, 60, 0x02);
    //Enable adaptive filtering (so eq can change in real time)
    dac_write(8, 1, 0x01);

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
    dac_write(0, 0x41, 00);   // Left Vol (0dB is usually 0, 18 is +9dB depending on mapping)
    dac_write(0, 0x42, 00);   // Right Vol


    // 10. Headphone & Speaker Setup (Page 1)
    // Reg 0x1F: HP Drivers power up
    dac_write(1, 0x1F, 0xC0);
    // Reg 0x24/0x25: HPL/R Gain (0x06 = 6dB approx)
    // ADJUST THESE VALUES FOR VOLUME CONTROL IN MAIN FIRMWARE!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    dac_write(1, 0x24, 0x10);
    dac_write(1, 0x25, 0x10);
    dac_write(1, 0x26, 0x10);
    // Reg 0x28/0x29: HPL/R Driver unmute
    dac_write(1, 0x28, 0x06);
    dac_write(1, 0x29, 0x06);


    // Speaker
    dac_write(1, 0x20, 0b10000110); // Speaker Power Up, 6dB gain
    dac_write(1, 0x2A, 0b00010100); // Speaker Unmute / Gain


    // 11. Headset Detect
    dac_write(1, 0x2E, 0x0b);  // MICBIAS enable


    // Route headset detect interrupt to GPIO1
    dac_write(0, 0x43, 0b10010101);  // Enable headset detect + interrupt
    dac_write(0, 0x30, 0b10000001); // route INT1 to headset change interrupt
    dac_write(0, 0x33, 0b00010100); // GPIO1 as INT1 output


    dac_set_volume(dac_volume);
}

