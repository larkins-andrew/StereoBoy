#include "led_driver.h"
#include "pico/stdlib.h"
#include <math.h>

/* Registers */
#define MODE1      0x00
#define MODE2      0x01
#define PRESCALE   0xFE
#define LED0_ON_L  0x06

/* Bits */
#define MODE1_SLEEP  0x10
#define MODE1_AI     0x20
#define MODE1_RESTART 0x80

#define MODE2_OUTDRV 0x04

static void write8(pca9685_t *dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(dev->i2c, dev->addr, buf, 2, false);
}

static uint8_t read8(pca9685_t *dev, uint8_t reg) {
    i2c_write_blocking(dev->i2c, dev->addr, &reg, 1, true);
    uint8_t val;
    i2c_read_blocking(dev->i2c, dev->addr, &val, 1, false);
    return val;
}

bool pca9685_init(pca9685_t *dev, i2c_inst_t *i2c, uint8_t addr) {
    dev->i2c = i2c;
    dev->addr = addr;
    dev->osc_freq = PCA9685_OSC_FREQ;

    pca9685_reset(dev);
    sleep_ms(10);

    pca9685_set_pwm_freq(dev, 1000);
    return true;
}

void pca9685_reset(pca9685_t *dev) {
    write8(dev, MODE1, MODE1_RESTART);
    sleep_ms(10);
}

void pca9685_sleep(pca9685_t *dev) {
    uint8_t mode = read8(dev, MODE1);
    write8(dev, MODE1, mode | MODE1_SLEEP);
    sleep_ms(1);
}

void pca9685_wakeup(pca9685_t *dev) {
    uint8_t mode = read8(dev, MODE1);
    write8(dev, MODE1, mode & ~MODE1_SLEEP);
    sleep_ms(1);
}

void pca9685_set_pwm_freq(pca9685_t *dev, float freq) {
    if (freq < 1) freq = 1;
    if (freq > 3500) freq = 3500;

    float prescale_val = ((dev->osc_freq / (freq * 4096.0f)) + 0.5f) - 1.0f;
    uint8_t prescale = (uint8_t)fminf(fmaxf(prescale_val, 3), 255);

    uint8_t oldmode = read8(dev, MODE1);
    write8(dev, MODE1, (oldmode & ~MODE1_RESTART) | MODE1_SLEEP);
    write8(dev, PRESCALE, prescale);
    write8(dev, MODE1, oldmode);
    sleep_ms(5);
    write8(dev, MODE1, oldmode | MODE1_RESTART | MODE1_AI);
}

void pca9685_set_pwm(pca9685_t *dev, uint8_t channel, uint16_t on, uint16_t off) {
    uint8_t reg = LED0_ON_L + 4 * channel;
    uint8_t buf[5] = {
        reg,
        on & 0xFF,
        on >> 8,
        off & 0xFF,
        off >> 8
    };
    i2c_write_blocking(dev->i2c, dev->addr, buf, 5, false);
}

void pca9685_set_pin(pca9685_t *dev, uint8_t channel, uint16_t value, bool invert) {
    if (value > 4095) value = 4095;

    if (invert) {
        if (value == 0)
            pca9685_set_pwm(dev, channel, 4096, 0);
        else if (value == 4095)
            pca9685_set_pwm(dev, channel, 0, 4096);
        else
            pca9685_set_pwm(dev, channel, 0, 4095 - value);
    } else {
        if (value == 4095)
            pca9685_set_pwm(dev, channel, 4096, 0);
        else if (value == 0)
            pca9685_set_pwm(dev, channel, 0, 4096);
        else
            pca9685_set_pwm(dev, channel, 0, value);
    }
}

void pca9685_write_microseconds(pca9685_t *dev, uint8_t channel, uint16_t us) {
    uint8_t prescale = read8(dev, PRESCALE) + 1;

    double pulse_len = 1000000.0;
    pulse_len *= prescale;
    pulse_len /= dev->osc_freq;

    double ticks = us / pulse_len;
    pca9685_set_pwm(dev, channel, 0, (uint16_t)ticks);
}
