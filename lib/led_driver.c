#include "led_driver.h"


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
