#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

// Configuration
#define CAPTURE_DEPTH 1000      // Number of samples to capture per batch
#define ADC_PIN_0 26            // GPIO 26 (ADC Channel 0)
#define ADC_PIN_1 27            // GPIO 27 (ADC Channel 1)

// Center at 0.65V (ADC is 12-bit, 0-3.3V)
// #define ADC_CH 5 This is never used
#define ADC_CH_L 6
#define ADC_CH_R 5

uint16_t capture_buf[CAPTURE_DEPTH];
