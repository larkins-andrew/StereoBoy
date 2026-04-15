#include "radiomag_util.h"

// Global state variables for the demo
uint8_t current_volume = 45;
uint8_t current_antenna = ANTENNA_FMI; // Start with Headphone Antenna
bool amp_is_muted = false;
int32_t scan_time = 50;

int radioLoop() {
    stdio_init_all();

    buttons_init(10);


    sleep_ms(3000); // Wait for you to open the serial terminal

    dprint("\n==================================================\n");
    dprint("         STEREOBOY INTERACTIVE RADIO DEMO         \n");
    dprint(" Controls:\n");
    dprint("  [U] : Seek to Next Station (Up)\n");
    dprint("  [D] : Seek to Previous Station (Down)\n");
    dprint("==================================================\n");
    dprint("  [SELECT] : Toggle Antenna (FMI Headphone <-> LPI PCB)\n");
    dprint("  [R] : Volume Up\n");
    dprint("  [L] : Volume Down\n");
    dprint("  [B] : Toggle Amplifier Mute (Hardware)\n");
    dprint("  [A] : Print Signal Status\n");
    dprint("==================================================\n\n");

    //init
    si4705_init();
    si4705_power_up();

    //set op_amp high (change this if outputting from normal audio stream??)
    gpio_init(PIN_AMP_SHUTDOWN);
    gpio_set_dir(PIN_AMP_SHUTDOWN, GPIO_OUT);
    gpio_put(PIN_AMP_SHUTDOWN, 0); // Drive LOW to wake up amp


    //initial properties
    si4705_set_property(0x1100, 0x0002); // 75us De-emphasis (US)
    si4705_set_volume(current_volume);
    si4705_select_antenna(current_antenna);

    // Drop the thresholds slightly (can change this later)

    si4705_set_property(0x1403, 1); // SNR Threshold
    si4705_set_property(0x1404, 15); // RSSI Threshold

    // Start by seeking up to find the very first available station
    dprint("Searching for initial station...\n");
    if (si4705_seek(true, true)) {
        print_current_station();
    } else {
        dprint("-> No stations found on initial scan.\n");
    }

    //Interactive Event Loop
    bool exit = false;
    while (true) {
        uint8_t pressed = buttons_get_just_pressed();
            if (pressed != 0)
                dprint("Pressed: %d", pressed);
            switch(pressed) {
                // SEEK UP
                case (BTN_U): 
                    dprint("\nSeeking UP...\n");
                    if (si4705_seek(true, true)) print_current_station();
                    else dprint("-> Seek wrapped the band. No station found.\n");
                    break;
                
                // SEEK DOWN
                case (BTN_D):
                    dprint("\nSeeking DOWN...\n");
                    if (si4705_seek(false, true)) print_current_station();
                    else dprint("-> Seek wrapped the band. No station found.\n");
                    break;

                // TOGGLE ANTENNA
                case (BTN_START):
                    if (current_antenna == ANTENNA_FMI) {
                        current_antenna = ANTENNA_LPI;
                        dprint("\nSwitched to PCB Trace Antenna (LPI / Pin 11)\n");
                    } else {
                        current_antenna = ANTENNA_FMI;
                        dprint("\nSwitched to Headphone Antenna (FMI / Pin 8)\n");
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
                        dprint("\nVolume: %d/63\n", current_volume);
                    }
                    break;

                // VOLUME DOWN
                case (BTN_L):
                    if (current_volume >= 3) {
                        current_volume -= 3;
                        si4705_set_volume(current_volume);
                        dprint("\nVolume: %d/63\n", current_volume);
                    }
                    break;

                // HARDWARE MUTE TOGGLE
                case (BTN_B):
                    amp_is_muted = !amp_is_muted;
                    // LM4810 shutdown is active HIGH
                    gpio_put(PIN_AMP_SHUTDOWN, amp_is_muted ? 1 : 0);
                    dprint("\nAmplifier is now: %s\n", amp_is_muted ? "MUTED" : "ACTIVE");
                    break;

                // PRINT STATUS
                case (BTN_A):
                    dprint("\n");
                    print_current_station();
                    break;
                case (BTN_SELECT):
                    exit = true;
                    break;
        }
        if (exit)
            break;
    }
    return 1;
}