#include <stdio.h>
#include "pico/stdlib.h"

// --- Hardware Config ---
#define SDA_PIN 20
#define SCL_PIN 21

// Debug LEDs
#define LED_HEARTBEAT 25   // Blinks to show code is running
#define LED_FOUND     24   // Solid ON if device is detected

// PCA9685 Config
#define PCA_ADDR      0x40
#define TARGET_CHANNEL 8

// Registers
#define MODE1         0x00
#define MODE2         0x01
#define LED0_ON_L     0x06
#define MODE1_SLEEP   0x10
#define MODE1_AI      0x20
#define MODE1_RESTART 0x80
#define MODE2_OUTDRV  0x04 // Totem Pole (Important for LEDs)

// --- Bit Bang Delay ---
#define DELAY_US      100 

// --- Bit Bang I2C Primitives ---
void i2c_delay() { sleep_us(DELAY_US); }

void sda_high() { gpio_set_dir(SDA_PIN, GPIO_IN); i2c_delay(); }
void sda_low()  { gpio_set_dir(SDA_PIN, GPIO_OUT); gpio_put(SDA_PIN, 0); i2c_delay(); }

void scl_high() {
    gpio_set_dir(SCL_PIN, GPIO_IN);
    // Timeout for clock stretching to prevent infinite hang
    int timeout = 1000;
    while(gpio_get(SCL_PIN) == 0 && timeout > 0) { sleep_us(1); timeout--; } 
    i2c_delay();
}
void scl_low()  { gpio_set_dir(SCL_PIN, GPIO_OUT); gpio_put(SCL_PIN, 0); i2c_delay(); }

void i2c_start() { sda_high(); scl_high(); sda_low(); scl_low(); }
void i2c_stop()  { sda_low(); scl_low(); scl_high(); sda_high(); }

bool i2c_write_byte(uint8_t byte) {
    for(int i=0; i<8; i++) {
        if ((byte & 0x80) != 0) sda_high();
        else sda_low();
        scl_high(); scl_low();
        byte <<= 1;
    }
    sda_high(); scl_high();
    bool nack = gpio_get(SDA_PIN); // Read ACK bit
    scl_low();
    return !nack; // Return true if ACK (Low)
}

// --- PCA Helper ---
void pca_write(uint8_t reg, uint8_t val) {
    i2c_start();
    i2c_write_byte(PCA_ADDR << 1);
    i2c_write_byte(reg);
    i2c_write_byte(val);
    i2c_stop();
}

void pca_init() {
    // 1. Set Output Drive to Totem Pole (Push-Pull)
    // This allows the pin to supply voltage to the LED
    pca_write(MODE2, MODE2_OUTDRV);

    // 2. Wake Up
    pca_write(MODE1, 0x00);
    sleep_ms(10);
    
    // 3. Enable Auto-Increment
    pca_write(MODE1, MODE1_AI);
}

// Force the LED ON or OFF using the "Full ON/OFF" control bits
// This ignores PWM timing registers entirely.
void pca_set_binary(uint8_t channel, bool on) {
    i2c_start();
    i2c_write_byte(PCA_ADDR << 1);
    i2c_write_byte(LED0_ON_L + (4 * channel)); // Start at ON_L
    
    if (on) {
        // ON_H bit 4 (0x10) = Full ON
        i2c_write_byte(0x00); i2c_write_byte(0x10); 
        i2c_write_byte(0x00); i2c_write_byte(0x00);
    } else {
        // OFF_H bit 4 (0x10) = Full OFF
        i2c_write_byte(0x00); i2c_write_byte(0x00); 
        i2c_write_byte(0x00); i2c_write_byte(0x10);
    }
    i2c_stop();
}

int main() {
    stdio_init_all();
    
    // GPIO Init
    gpio_init(LED_HEARTBEAT); gpio_set_dir(LED_HEARTBEAT, GPIO_OUT);
    gpio_init(LED_FOUND);     gpio_set_dir(LED_FOUND, GPIO_OUT);
    
    gpio_init(SDA_PIN); gpio_set_dir(SDA_PIN, GPIO_IN);
    gpio_init(SCL_PIN); gpio_set_dir(SCL_PIN, GPIO_IN);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
    
    sleep_ms(2000); // Give you time to look at the board

    // --- CONNECTION TEST ---
    i2c_start();
    bool ack = i2c_write_byte(PCA_ADDR << 1);
    i2c_stop();
    
    if (!ack) {
        // FAILED: Frantic Blink on Pin 25
        while(true) {
            gpio_put(LED_HEARTBEAT, 1); sleep_ms(50);
            gpio_put(LED_HEARTBEAT, 0); sleep_ms(50);
        }
    }

    // SUCCESS: Turn ON Pin 24
    gpio_put(LED_FOUND, 1);
    
    pca_init();

    // --- MAIN LOOP ---
    while(true) {
        // Turn External LED ON
        pca_set_binary(TARGET_CHANNEL, true);
        gpio_put(LED_HEARTBEAT, 1);
        sleep_ms(1000);

        // Turn External LED OFF
        pca_set_binary(TARGET_CHANNEL, false);
        gpio_put(LED_HEARTBEAT, 0);
        sleep_ms(1000);
    }
    
    return 0;
}