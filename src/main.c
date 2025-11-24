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
#define TARGET_CHANNEL 8   // Ensure this matches your confirmed working pin

// Registers
#define MODE1         0x00
#define MODE2         0x01
#define LED0_ON_L     0x06
#define MODE1_SLEEP   0x10
#define MODE1_AI      0x20
#define MODE1_RESTART 0x80
#define MODE2_OUTDRV  0x04 // Totem Pole (Critical for your setup)

// --- Bit Bang Delay ---
#define DELAY_US      100 

// --- Bit Bang I2C Primitives ---
void i2c_delay() { sleep_us(DELAY_US); }

void sda_high() { gpio_set_dir(SDA_PIN, GPIO_IN); i2c_delay(); }
void sda_low()  { gpio_set_dir(SDA_PIN, GPIO_OUT); gpio_put(SDA_PIN, 0); i2c_delay(); }

void scl_high() {
    gpio_set_dir(SCL_PIN, GPIO_IN);
    // Timeout for clock stretching
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
    pca_write(MODE2, MODE2_OUTDRV);

    // 2. Wake Up
    pca_write(MODE1, 0x00);
    sleep_ms(10);
    
    // 3. Enable Auto-Increment (Crucial for writing 4 PWM bytes at once)
    pca_write(MODE1, MODE1_AI);
}

// --- NEW FUNCTION: PWM Fading ---
void pca_set_pwm(uint8_t channel, uint16_t on, uint16_t off) {
    i2c_start();
    
    // Write Address
    i2c_write_byte(PCA_ADDR << 1);
    
    // Write Start Register (LEDn_ON_L)
    // Because MODE1_AI is set, the chip will auto-move to the next registers
    i2c_write_byte(LED0_ON_L + (4 * channel));
    
    // Send 4 Bytes: ON_L, ON_H, OFF_L, OFF_H
    i2c_write_byte(on & 0xFF);
    i2c_write_byte(on >> 8);
    i2c_write_byte(off & 0xFF);
    i2c_write_byte(off >> 8);
    
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
    
    sleep_ms(2000); 

    // --- CONNECTION TEST ---
    i2c_start();
    bool ack = i2c_write_byte(PCA_ADDR << 1);
    i2c_stop();
    
    if (!ack) {
        // FAILED: Frantic Blink
        while(true) {
            gpio_put(LED_HEARTBEAT, 1); sleep_ms(50);
            gpio_put(LED_HEARTBEAT, 0); sleep_ms(50);
        }
    }

    // SUCCESS
    gpio_put(LED_FOUND, 1);
    pca_init();

    // --- FADE LOOP ---
    while(true) {
        
        // FADE UP (Get Brighter)
        gpio_put(LED_HEARTBEAT, 1); // Heartbeat ON
        // We step by 32 because bit-banging is slow. 
        // 0 -> 4096
        for(int i=0; i<4096; i+=128) {
            // ON=0, OFF=i means the LED is ON for 'i' ticks out of 4096
            pca_set_pwm(TARGET_CHANNEL, 0, i); 
        }

        // FADE DOWN (Get Dimmer)
        gpio_put(LED_HEARTBEAT, 0); // Heartbeat OFF
        // 4095 -> 0
        for(int i=4095; i>=0; i-=128) {
            pca_set_pwm(TARGET_CHANNEL, 0, i);
        }
        
        sleep_ms(200); // Brief pause at darkness
    }
    
    return 0;
}