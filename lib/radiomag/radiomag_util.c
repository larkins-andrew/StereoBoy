#include "radiomag_util.h"

// Global state variables for the demo
uint8_t current_volume = 45;
uint8_t current_antenna = ANTENNA_FMI; // Start with Headphone Antenna
uint16_t current_freq = 40000;
bool amp_is_muted = false;
int32_t scan_time = 50;
bool is_digital_audio = false;
uint16_t vol_check = 0;
uint16_t inverted_volume = 0;

int radioLoop(vs1053_t* player) {
    stdio_init_all();

    buttons_init(10);


    sleep_ms(3000); // Wait for you to open the serial terminal

    dprint("\n==================================================\n");
    dprint("         STEREOBOY INTERACTIVE RADIO DEMO         \n");
    dprint(" Controls:\n");
    dprint("  [U] : Seek to Next Station (Up)\n");
    dprint("  [D] : Seek to Previous Station (Down)\n");
    dprint("==================================================\n");
    dprint("  [L] : Mute\n");
    dprint("  [B] : Toggle Antenna (FMI Headphone <-> LPI PCB)\n");
    dprint("  [A] : Toggle Audio (analog <-> digital) \n");
    dprint("==================================================\n\n");

    //init
    si4705_init();
    si4705_power_up(0x05); 

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
        if (vol_check < 30){
            vol_check = (vol_check + 1) % 31;
        }
        else {
            adc_select_input(POT_ADC_CHANNEL);
            uint16_t raw_adc = (adc_read() * 63) / 4096;
            if (abs(raw_adc - current_volume) < 3) {
                inverted_volume = MAX_VOLUME - raw_adc;
                si4705_set_volume(inverted_volume);
                printf("Volume set to: %d", inverted_volume);
            }
            current_volume = raw_adc;
        }
        
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

                case (BTN_L):
                    amp_is_muted = !amp_is_muted;
                    gpio_put(PIN_AMP_SHUTDOWN, amp_is_muted ? 1 : 0);
                    dprint("\nAmplifier is now: %s\n", amp_is_muted ? "MUTED" : "ACTIVE");
                    break;

                case (BTN_B):
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

                // PRINT STATUS
                case (BTN_A):
                    current_freq = si4705_get_current_frequency();
                    switch_radio_audio_mode(player, current_freq, is_digital_audio, current_volume, current_antenna);
                    printf("debug 11 \n");
                    break;
                case (BTN_SELECT):
                    exit = true;
                    break;
        }
        if (exit)
            break;    }
    //give codec control again
    vs1053_claim_i2s_bus(player);
    si4705_power_down();
    dprint("outside of radio");
    return 1;
}