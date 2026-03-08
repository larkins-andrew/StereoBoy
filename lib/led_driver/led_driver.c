#include "led_driver.h"
#include "pico/stdlib.h"
#include <math.h>
#include <stdlib.h>

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
    // pca9685_wakeup(dev);
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

#define NUM_LEDS_PER_CH 8
#define ADC_CENTER 1526 //4096 * (1.23/3.3) where 1.23 is the DC bais from the codec to the audio signal
#define MAX_AMPLITUDE 800.0f // Adjust for sensitivity

// State variables for peak smoothing 
static float peak_l = 0.0f;
static float peak_r = 0.0f;
static const float PEAK_DECAY = 0.05f; // How fast the LEDs fall back down

void pca9685_update_vu(pca9685_t *dev, uint16_t adc_left, uint16_t adc_right) {
    // Remove DC bais from codec
    float amp_l = (float)abs((int)adc_left - ADC_CENTER);
    float amp_r = (float)abs((int)adc_right - ADC_CENTER);

    // Scale to a percent
    float level_l = fminf(amp_l / MAX_AMPLITUDE, 1.0f);
    float level_r = fminf(amp_r / MAX_AMPLITUDE, 1.0f);

    // Optional: Apply Logarithmic curve so quiet sounds still light up the bottom LEDs
    // level_l = log10f(1.0f + 9.0f * level_l); 
    // level_r = log10f(1.0f + 9.0f * level_r);

    // Peak Tracking (Instantly rise, slowly fall)
    if (level_l > peak_l) peak_l = level_l;
    else peak_l -= PEAK_DECAY;
    if (peak_l < 0.0f) peak_l = 0.0f;

    if (level_r > peak_r) peak_r = level_r;
    else peak_r -= PEAK_DECAY;
    if (peak_r < 0.0f) peak_r = 0.0f;

    // 4. Update Left LEDs (Channels 0 to 7)
    for (int i = 0; i < NUM_LEDS_PER_CH; i++) {
        float threshold = (float)(i + 1) / NUM_LEDS_PER_CH;
        float prev_threshold = (float)i / NUM_LEDS_PER_CH;

        if (peak_l >= threshold) {
            pca9685_set_pin(dev, i, 4095, false); // Fully ON
        } else if (peak_l > prev_threshold) {
            // Smoothly fade the top active LED
            float fraction = (peak_l - prev_threshold) * NUM_LEDS_PER_CH;
            pca9685_set_pin(dev, i, (uint16_t)(fraction * 4095), false);
        } else {
            pca9685_set_pin(dev, i, 0, false); // Fully OFF
        }
    }

    // 5. Update Right LEDs (Channels 8 to 15)
    for (int i = 0; i < NUM_LEDS_PER_CH; i++) {
        float threshold = (float)(i + 1) / NUM_LEDS_PER_CH;
        float prev_threshold = (float)i / NUM_LEDS_PER_CH;

        if (peak_r >= threshold) {
            pca9685_set_pin(dev, i + 8, 4095, false); 
        } else if (peak_r > prev_threshold) {
            float fraction = (peak_r - prev_threshold) * NUM_LEDS_PER_CH;
            pca9685_set_pin(dev, i + 8, (uint16_t)(fraction * 4095), false);
        } else {
            pca9685_set_pin(dev, i + 8, 0, false); 
        }
    }
}