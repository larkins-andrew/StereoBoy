#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "SB_PCA9685.h"

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
#define MODE2_OUTDRV  0x04

// -----------------------------------------------------------------------------
// Helper: Write 1 byte to a PCA9685 register
// -----------------------------------------------------------------------------
void pca_write_reg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    i2c_write_blocking(i2c0, PCA_ADDR, buf, 2, false);
}

// -----------------------------------------------------------------------------
// Helper: Write 4 bytes: ON_L, ON_H, OFF_L, OFF_H
// -----------------------------------------------------------------------------
void pca_set_pwm(uint8_t channel, uint16_t on, uint16_t off) {
    uint8_t reg = LED0_ON_L + 4 * channel;
    uint8_t buf[5] = {
        reg,
        on,
        on >> 8,
        off,
        off >> 8
    };

    i2c_write_blocking(i2c0, PCA_ADDR, buf, 5, false);
}

// -----------------------------------------------------------------------------
// PCA9685 Init
// -----------------------------------------------------------------------------
void pca_init() {
    // MODE1: Enter sleep so prescale can be set
    pca_write_reg(MODE1, MODE1_SLEEP);

    // Set prescale (60Hz PWM)
    uint8_t prescale = 101;
    pca_write_reg(0xFE, prescale);

    // MODE2: Totem pole output
    pca_write_reg(MODE2, MODE2_OUTDRV);

    // MODE1: Wake up, auto-increment
    pca_write_reg(MODE1, MODE1_AI);

    sleep_ms(5);

    // Optional: Restart bit
    pca_write_reg(MODE1, MODE1_AI | MODE1_RESTART);
}

// -----------------------------------------------------------------------------
// Check for ACK from the PCA9685
// -----------------------------------------------------------------------------
bool pca_check_presence() {
    int result = i2c_write_blocking(i2c0, PCA_ADDR, NULL, 0, 0);

    // Hardware I²C returns number of bytes written OR a negative error code
    return (!result);
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main() {
    stdio_init_all();

    // Init LEDs
    gpio_init(LED_HEARTBEAT); gpio_set_dir(LED_HEARTBEAT, GPIO_OUT);
    gpio_init(LED_FOUND);     gpio_set_dir(LED_FOUND, GPIO_OUT);

    // Init I²C0 at 400 kHz
    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(2000);

    // Test if PCA9685 is present
    if (!pca_check_presence()) {
        // Blink rapidly forever
        while (true) {
            gpio_put(LED_HEARTBEAT, 1); sleep_ms(50);
            gpio_put(LED_HEARTBEAT, 0); sleep_ms(50);
        }
    }

    // Device found
    gpio_put(LED_FOUND, 1);
    pca_init();

    // -------------------------------------------------------------------------
    // Fade loop
    // -------------------------------------------------------------------------
    while (true) {

        // Fade UP
        gpio_put(LED_HEARTBEAT, 1);
        for (int i = 0; i < 4096; i += 128) {
            pca_set_pwm(TARGET_CHANNEL, 0, i);
        }
        sleep_ms(200);

        // Fade DOWN
        gpio_put(LED_HEARTBEAT, 0);
        for (int i = 4095; i >= 0; i -= 128) {
            pca_set_pwm(TARGET_CHANNEL, 0, i);
        }

        sleep_ms(200);
    }

    return 0;
}
