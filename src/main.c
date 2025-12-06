#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "SB_PCA9685.h"

// --- Hardware Config ---
#define SDA_PIN 20
#define SCL_PIN 21

// Debug LEDs
#define LED_HEARTBEAT 25
#define LED_FOUND     24

#define TARGET_CHANNEL 8

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
