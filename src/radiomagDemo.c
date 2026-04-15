#include <stdio.h>
#include "pico/stdlib.h"
#include "lib/radiomag/si4705.h"
#include "lib/dac/dac.h"
#include "lib/buttons/buttons.h"


// Define the LM4810 shutdown pin
#define PIN_AMP_SHUTDOWN 17

// Global state variables for the demo
uint8_t current_volume = 45;
uint8_t current_antenna = ANTENNA_FMI; // Start with Headphone Antenna
bool amp_is_muted = false;
int32_t scan_time = 50;

int main() {
    stdio_init_all();

    buttons_init(10);


    sleep_ms(3000); // Wait for you to open the serial terminal

    printf("\n==================================================\n");
    printf("         STEREOBOY INTERACTIVE RADIO DEMO         \n");
    printf("==================================================\n");
    printf(" Controls:\n");
    printf("  [U] : Seek to Next Station (Up)\n");
    printf("  [D] : Seek to Previous Station (Down)\n");
    printf("  [SELECT] : Toggle Antenna (FMI Headphone <-> LPI PCB)\n");
    printf("  [R] : Volume Up\n");
    printf("  [L] : Volume Down\n");
    printf("  [B] : Toggle Amplifier Mute (Hardware)\n");
    printf("  [A] : Print Signal Status\n");
    printf("==================================================\n\n");

    //init
    si4705_init(); //look into init later, I2C should already be initliazed 
    si4705_power_up();

    //set op_amp high (change this if outputting from normal audio stream??)
    gpio_init(PIN_AMP_SHUTDOWN);
    gpio_set_dir(PIN_AMP_SHUTDOWN, GPIO_OUT);
    gpio_put(PIN_AMP_SHUTDOWN, 0); // Drive LOW to wake up amp


    //initial properties
    si4705_set_property(0x1100, 0x0002); // 75us De-emphasis (US)
    si4705_set_volume(current_volume);
    si4705_select_antenna(current_antenna);
    printf("Debugg3");

    // Drop the thresholds slightly (can change this later)

    si4705_set_property(0x1403, 1); // SNR Threshold
    si4705_set_property(0x1404, 15); // RSSI Threshold
    printf("Debugg4");

    // Start by seeking up to find the very first available station
    printf("Searching for initial station...\n");
    if (si4705_seek(true, true)) {
        print_current_station();
    } else {
        printf("-> No stations found on initial scan.\n");
    }

    //Interactive Event Loop
    while (true) {
        uint8_t pressed = buttons_get_just_pressed();
            if (pressed != 0)
                printf("Pressed: %d", pressed);
            switch(pressed) {
                // SEEK UP
                case (BTN_U): 
                    printf("\nSeeking UP...\n");
                    if (si4705_seek(true, true)) print_current_station();
                    else printf("-> Seek wrapped the band. No station found.\n");
                    break;
                
                // SEEK DOWN
                case (BTN_D):
                    printf("\nSeeking DOWN...\n");
                    if (si4705_seek(false, true)) print_current_station();
                    else printf("-> Seek wrapped the band. No station found.\n");
                    break;

                // TOGGLE ANTENNA
                case (BTN_SELECT):
                    if (current_antenna == ANTENNA_FMI) {
                        current_antenna = ANTENNA_LPI;
                        printf("\nSwitched to PCB Trace Antenna (LPI / Pin 11)\n");
                    } else {
                        current_antenna = ANTENNA_FMI;
                        printf("\nSwitched to Headphone Antenna (FMI / Pin 8)\n");
                    }
                    si4705_select_antenna(current_antenna);
                    // Give the LNA a few milliseconds to settle, then print new signal quality
                    sleep_ms(50); 
                    print_current_station();
                    break;

                // VOLUME UP
                case (BTN_R): 
                    if (current_volume < 63) {
                        current_volume += 3;
                        if (current_volume > 63) current_volume = 63;
                        si4705_set_volume(current_volume);
                        printf("\nVolume: %d/63\n", current_volume);
                    }
                    break;

                // VOLUME DOWN
                case (BTN_L):
                    if (current_volume >= 3) {
                        current_volume -= 3;
                        si4705_set_volume(current_volume);
                        printf("\nVolume: %d/63\n", current_volume);
                    }
                    break;

                // HARDWARE MUTE TOGGLE
                case (BTN_B):
                    amp_is_muted = !amp_is_muted;
                    // LM4810 shutdown is active HIGH
                    gpio_put(PIN_AMP_SHUTDOWN, amp_is_muted ? 1 : 0);
                    printf("\nAmplifier is now: %s\n", amp_is_muted ? "MUTED" : "ACTIVE");
                    break;

                // PRINT STATUS
                case (BTN_A):
                    printf("\n");
                    print_current_station();
                    break;
        }
        sleep_ms(10); //might want to remove this sleep (or extend it if we are doing other things)
    }

    return 0;
}