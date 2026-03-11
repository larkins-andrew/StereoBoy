#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "lib/led_driver/led_driver.h" 

// I2C1 for LED Driver (PCA9685) on RP2350
#define PIN_I2C1_SDA 42
#define PIN_I2C1_SCL 43

int main() {
    stdio_init_all();
    sleep_ms(2000); // Give USB serial time to connect so you can see printfs
    printf("\n--- StereoBoy LED Subsystem Test ---\n");

    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(PIN_I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C1_SDA);
    gpio_pull_up(PIN_I2C1_SCL);
    printf("I2C1 hardware initialized on pins 42/43.\n");


    pca9685_t vu_meter;
    if (pca9685_init(&vu_meter, i2c1, 0x40)) {
        printf("PCA9685 successfully found at 0x40!\n");
    } else {
        printf("ERROR: PCA9685 not responding.\n");
        while(1); 
    }

    if (!pca_check_presence(&vu_meter)) {
        // Blink rapidly forever
        while (true) {
            printf("ERROR");
            sleep_ms(100);
        }
    }

    printf("Starting LED sweep animation...\n");

    while (1) {
        // Sweep ON
        for (int i = 0; i < 16; i++) {
            // Channel i, Value 4095 (100% duty cycle), Invert false
            pca9685_set_pin(&vu_meter, i, 4095, false); 
            sleep_ms(100); 
            printf("Sweeep on...\n");
        }
        
        // Sweep OFF
        for (int i = 0; i < 16; i++) {
            // Channel i, Value 0 (0% duty cycle), Invert false
            pca9685_set_pin(&vu_meter, i, 0, false); 
            sleep_ms(100);
            printf("Sweeep off...\n");
        }
    }

    return 0;
}