
#ifndef BITBANG_I2C_PCA_H
#define BITBANG_I2C_PCA_H
#include <stdio.h>
#include "pico/stdlib.h"

// --- Hardware Config ---
#define SDA_PIN 26
#define SCL_PIN 38

// Debug LEDs
#define LED_HEARTBEAT 25   // Blinks to show code is running
#define LED_FOUND     24   // Solid ON if device is detected

// PCA9685 Config
#define PCA_ADDR      0x40
#define TARGET_CHANNEL 9   // Ensure this matches your confirmed working pin

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

/* --- Bit Bang I2C Primitives --- */
void i2c_delay(void);

void sda_high(void);
void sda_low(void);

void scl_high(void);
void scl_low(void);

void i2c_start(void);
void i2c_stop(void);

bool i2c_write_byte(uint8_t byte);


/* --- PCA Helper Functions --- */
void pca_write(uint8_t reg, uint8_t val);
void pca_init(void);


/* --- PWM Control --- */
void pca_set_pwm(uint8_t channel, uint16_t on, uint16_t off);

#endif /* BITBANG_I2C_PCA_H */
