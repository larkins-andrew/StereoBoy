#include "buttons.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/time.h"

// --- Pin Definitions ---
static const uint PIN_LATCH = 11;
static const uint PIN_CLOCK = 23;
static const uint PIN_DATA  = 22;

// --- Internal State ---
static volatile uint8_t current_button_states = 0;
static uint8_t last_button_states = 0; // Used for edge detection

// --- Timer Interrupt Callback ---
// We make this static so main.c can't accidentally call it
static bool reading_timer_callback(struct repeating_timer *t) {
    uint8_t reading = 0;

    // 1. LATCH (Pulse High-Low-High)
    gpio_put(PIN_LATCH, 0);
    busy_wait_us_32(1);
    gpio_put(PIN_LATCH, 1);

    // 2. SHIFT
    for(int i = 0; i < 8; i++) {
        // Might need to invert for Active low buttons (change: to !gpio_get(PIN_DATA))
        if (!gpio_get(PIN_DATA)) {
            // MSB First
            reading |= (1 << (7 - i));
        }
        
        // Pulse Clock
        gpio_put(PIN_CLOCK, 1);
        busy_wait_us_32(1);
        gpio_put(PIN_CLOCK, 0);
    }

    current_button_states = reading;
    return true;
}

// --- Public Functions ---

void buttons_init(int32_t scan_time) {
    // GPIO Init
    gpio_init(PIN_LATCH); gpio_set_dir(PIN_LATCH, GPIO_OUT); gpio_put(PIN_LATCH, 1);
    gpio_init(PIN_CLOCK); gpio_set_dir(PIN_CLOCK, GPIO_OUT); gpio_put(PIN_CLOCK, 0);
    gpio_init(PIN_DATA);  gpio_set_dir(PIN_DATA, GPIO_IN);

    // Start Timer (10ms interval)
    // We use a static variable for the timer struct so it persists
    static struct repeating_timer timer;
    add_repeating_timer_ms(scan_time, reading_timer_callback, NULL, &timer);
}

uint8_t buttons_get_raw_state(void) {
    return current_button_states;
}

uint8_t buttons_get_just_pressed(void) {
    // Snapshot volatile state
    uint8_t current = current_button_states;
    
    // Calculate rising edges (0 -> 1)
    uint8_t just_pressed = (current ^ last_button_states) & current;
    
    // Update history
    last_button_states = current;
    
    return just_pressed;
}