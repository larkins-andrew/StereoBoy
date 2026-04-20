#ifndef ADC
#define ADC

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

// Configuration
#define CAPTURE_DEPTH 1000      // Number of samples to capture per batch
#define ADC_PIN_0 26            // GPIO 26 (ADC Channel 0)
#define ADC_PIN_1 27            // GPIO 27 (ADC Channel 1)

#define ADC_CH_L 6
#define ADC_CH_R 5
#define POT_CH   4

#endif
