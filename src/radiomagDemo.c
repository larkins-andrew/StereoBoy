#include <stdio.h>
#include "pico/stdlib.h"
#include "lib/radiomag/si4705.h"

// Define the LM4810 shutdown pin
#define PIN_AMP_SHUTDOWN 17

int main() {
    stdio_init_all();
    sleep_ms(3000); 
    printf("StereoBoy Audio Test Starting...\n");

    // 1. Hardware Init & Power Up for Si4705
    si4705_init();
    si4705_power_up();

    // 2. Enable the LM4810 Headphone Amplifier
    // The LM4810 has an active-high shutdown. Drive LOW to turn it ON.
    printf("Enabling LM4810 Amplifier...\n");
    gpio_init(PIN_AMP_SHUTDOWN);
    gpio_set_dir(PIN_AMP_SHUTDOWN, GPIO_OUT);
    gpio_put(PIN_AMP_SHUTDOWN, 0); 

    // 3. Configure Radio Properties
    si4705_set_property(0x1100, 0x0002); // 75us De-emphasis (US Standard)
    si4705_set_volume(45);               // Bump the volume up a bit (Max 63)
    
    // Explicitly route antenna to the headphone jack (FMI Pin 8)
    si4705_select_antenna(ANTENNA_LPI);  

    // 4. Hard-tune to 93.5 MHz
    // 93.5 MHz = 9350 in 10 kHz units
    uint16_t target_freq = 9350;
    printf("Tuning directly to 93.5 MHz...\n");
    si4705_tune_fm(target_freq);

    // Variables for status monitoring
    uint8_t rssi = 0;
    uint8_t snr = 0;
    bool is_valid = false;

    // 5. Main Audio Loop
    while (true) {
        // Read the live metrics of the station you are listening to
        is_valid = si4705_get_tune_status(&rssi, &snr);
        
        printf("PLAYING 93.5 MHz | RSSI: %3d dBuV | SNR: %3d dB | Valid: %d\n", 
               rssi, snr, is_valid);

        // Update the serial monitor once per second
        sleep_ms(1000); 
    }

    return 0;
}