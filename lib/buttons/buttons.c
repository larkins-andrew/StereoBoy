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
static bool reading_timer_callback(struct repeating_timer *t) {
    uint8_t reading = 0;

    // 1. latch values (Pulse High-Low-High)
    gpio_put(PIN_LATCH, 0);
    busy_wait_us_32(1);
    gpio_put(PIN_LATCH, 1);

    // 2. shift ivalues
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


//CHAT MADE THESE FUNCTIONS BELOW!!!
//maps buttons to characters for use in jukebox, allows for multibutton presses
char buttons_map_to_char_jukebox(void) {
    uint8_t raw = buttons_get_raw_state();       // Is a button HELD
    uint8_t edge = buttons_get_just_pressed();   // Was a button CLICKED

    // Identify if the modifier (SELECT) is currently being held
    bool select_held = (raw & BTN_SELECT);

    // Process the "Just Pressed" buttons based on the modifier
    if (edge == 0) return 0; // No new press detected

    if (select_held) {
        // --- Standard actions (just button) ---
        if (edge & BTN_B)     return 's'; // B = stop
        if (edge & BTN_A)     return 'p'; // A = Pause
        if (edge & BTN_U)     return 'u'; // Up = Volume Up
        if (edge & BTN_D)     return 'd'; // Down = Volume Down
        if (edge & BTN_L)     return 'n'; // Left = next song
        if (edge & BTN_R)     return 'o'; // right = prev song
    } else {
        // --- modifier actions (buton + select) ---
        if (edge & BTN_R) return 'f'; // Select + Right = Fast Forward
        if (edge & BTN_L) return 'r'; // Select + Left = Rewind
    }
    return 0; // No match found
}

/**
 * 2. FOR MAIN MENU: Maps buttons to navigation
 * Returns: 'U'(Up), 'D'(Down), 'L'(-5), 'R'(+5), 'E'(Enter/Start)
 */
char buttons_map_menu_navigation(void) {
    uint8_t edge = buttons_get_just_pressed();
    if (edge == 0) return 0;
    if (edge & BTN_U)     return 'U';
    if (edge & BTN_D)     return 'D';
    if (edge & BTN_L)     return 'L';
    if (edge & BTN_R)     return 'R';
    if (edge & BTN_START) return 'E';
    return 0;
}