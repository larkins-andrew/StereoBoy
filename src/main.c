#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// ============================================================================
//  PCA9685 REGISTER DEFINITIONS (Ported from library headers)
// ============================================================================
#define PCA9685_ADDR        0x40
#define MODE1               0x00
#define MODE2               0x01
#define SUBADR1             0x02
#define SUBADR2             0x03
#define SUBADR3             0x04
#define ALLCALLADR          0x05
#define LED0_ON_L           0x06
#define LED0_ON_H           0x07
#define LED0_OFF_L          0x08
#define LED0_OFF_H          0x09
#define ALL_LED_ON_L        0xFA
#define ALL_LED_ON_H        0xFB
#define ALL_LED_OFF_L       0xFC
#define ALL_LED_OFF_H       0xFD
#define PRE_SCALE           0xFE

// Mode 1 Register bits
#define MODE1_RESTART       0x80
#define MODE1_EXTCLK        0x40
#define MODE1_AI            0x20 // Auto-Increment
#define MODE1_SLEEP         0x10
#define MODE1_SUB1          0x08
#define MODE1_SUB2          0x04
#define MODE1_SUB3          0x02
#define MODE1_ALLCALL       0x01

// Hardware Configuration
#define I2C_PORT            i2c0
#define I2C_SDA_PIN         20
#define I2C_SCL_PIN         21
#define TARGET_CHANNEL      8    // LED attached to Channel 8
#define ONBOARD_LED         PICO_DEFAULT_LED_PIN

// ============================================================================
//  I2C HELPER FUNCTIONS (Adapting ESP32 logic to Pico SDK)
// ============================================================================

static void i2c_write_byte(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    i2c_write_blocking(I2C_PORT, PCA9685_ADDR, data, 2, false);
}

static uint8_t i2c_read_byte(uint8_t reg) {
    uint8_t val;
    i2c_write_blocking(I2C_PORT, PCA9685_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, PCA9685_ADDR, &val, 1, false);
    return val;
}

// ============================================================================
//  PCA9685 DRIVER IMPLEMENTATION
// ============================================================================

/**
 * @brief Sets the PWM frequency for the entire chip.
 * Logic derived from library: Sleep -> Set Prescale -> Wake -> Restart
 */
void pca9685_set_pwm_freq(float freq_hz) {
    // Calculate prescale value based on 25MHz internal oscillator
    // prescale = round(osc_clock / (4096 * update_rate)) - 1
    float prescaleval = 25000000.0f;
    prescaleval /= 4096.0f;
    prescaleval /= freq_hz;
    prescaleval -= 1.0f;
    
    uint8_t prescale = (uint8_t)(prescaleval + 0.5f);

    uint8_t oldmode = i2c_read_byte(MODE1);
    uint8_t newmode = (oldmode & 0x7F) | MODE1_SLEEP; // Sleep to set prescale

    i2c_write_byte(MODE1, newmode);
    i2c_write_byte(PRE_SCALE, prescale);
    i2c_write_byte(MODE1, oldmode);
    
    sleep_ms(5); // Wait for oscillator to stabilize
    
    i2c_write_byte(MODE1, oldmode | MODE1_RESTART | MODE1_AI);
}

/**
 * @brief Sets the PWM output for a specific channel.
 * @param num Channel (0-15)
 * @param on  Step to turn ON (0-4095)
 * @param off Step to turn OFF (0-4095)
 */
void pca9685_set_pwm(uint8_t num, uint16_t on, uint16_t off) {
    uint8_t buffer[5];
    buffer[0] = LED0_ON_L + 4 * num;
    buffer[1] = on & 0xFF;
    buffer[2] = (on >> 8);
    buffer[3] = off & 0xFF;
    buffer[4] = (off >> 8);
    
    // Write all 4 registers in one transmission (enabled by MODE1_AI)
    i2c_write_blocking(I2C_PORT, PCA9685_ADDR, buffer, 5, false);
}

/**
 * @brief Simplified helper to set a specific duty cycle value (0-4095)
 * Use this for LEDs (always start at tick 0, end at 'val')
 */
void pca9685_set_value(uint8_t num, uint16_t val) {
    if (val == 4096) {
        // Fully ON
        pca9685_set_pwm(num, 4096, 0);
    } else if (val == 0) {
        // Fully OFF
        pca9685_set_pwm(num, 0, 4096);
    } else {
        // PWM duty
        pca9685_set_pwm(num, 0, val);
    }
}

void pca9685_init() {
    // Reset
    i2c_write_byte(MODE1, 0x00);
    
    // Set frequency to 1000Hz (Good for LEDs, prevents flickering)
    pca9685_set_pwm_freq(1000);
}

// ============================================================================
//  MAIN APPLICATION
// ============================================================================

int main() {
    // 1. Initialize Pico Peripherals
    stdio_init_all();
    
    // Initialize Onboard LED
    gpio_init(ONBOARD_LED);
    gpio_set_dir(ONBOARD_LED, GPIO_OUT);
    gpio_put(ONBOARD_LED, 1); // Indicate power on

    // Initialize I2C0
    i2c_init(I2C_PORT, 400 * 1000); // 400kHz
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // 2. Initialize PCA9685
    pca9685_init();

    // 3. Fading Loop
    while (1) {
        // Toggle Onboard LED to show loop is active
        gpio_put(ONBOARD_LED, 1);

        // Fade IN
        for (int i = 0; i < 4096; i += 16) {
            pca9685_set_value(TARGET_CHANNEL, i);
            sleep_ms(2);
        }

        gpio_put(ONBOARD_LED, 0);

        // Fade OUT
        for (int i = 4095; i >= 0; i -= 16) {
            pca9685_set_value(TARGET_CHANNEL, i);
            sleep_ms(2);
        }
    }

    return 0;
}