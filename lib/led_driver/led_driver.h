#ifndef PCA9685_H
#define PCA9685_H

#include "hardware/i2c.h"
#include <stdint.h>
#include <stdbool.h>

#define PCA9685_I2C_ADDR 0x40
#define PCA9685_OSC_FREQ 25000000

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    uint32_t osc_freq;
} pca9685_t;

bool pca9685_init(pca9685_t *dev, i2c_inst_t *i2c, uint8_t addr);

void pca9685_reset(pca9685_t *dev);
void pca9685_sleep(pca9685_t *dev);
void pca9685_wakeup(pca9685_t *dev);

void pca9685_set_pwm_freq(pca9685_t *dev, float freq);
void pca9685_set_pwm(pca9685_t *dev, uint8_t channel, uint16_t on, uint16_t off);
void pca9685_set_pin(pca9685_t *dev, uint8_t channel, uint16_t value, bool invert);
void pca9685_write_microseconds(pca9685_t *dev, uint8_t channel, uint16_t us);

#endif
