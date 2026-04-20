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
uint8_t eq_band = 0;

int radioLoop(vs1053_t* player) {
    stdio_init_all();

    buttons_init(10);


    sleep_ms(1000); // Wait for you to open the serial terminal

    dprint("\n==================================================\n");
    dprint("         RadioMag        \n");
    dprint(" Controls:\n");
    dprint("  [U] : Seek to Next Station (Up)\n");
    dprint("  [D] : Seek to Previous Station (Down)\n");
    dprint("  [L] : Decrease selected EQ band\n");
    dprint("  [R] : Increase selected EQ band\n");
    dprint("==================================================\n");
    dprint("  [A] : Toggle antenna (headphone <-> pcb) \n");
    dprint("  [B] : Select EQ Band\n");
    dprint("[START] : Toggle audio output (analog <-> digital) \n");
    dprint("[SELECT] : Exit RadioMag \n");
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

    //Volume control, switch between Si4705 and DAC
        if (vol_check < 30){
            vol_check = (vol_check + 1) % 31;
        }
        //Update volume
        else {
            adc_select_input(POT_ADC_CHANNEL);
            uint16_t raw_adc = (adc_read() * 63) / 4096;
            //DAC control
            if (is_digital_audio){
                // if (abs(raw_adc - current_volume) < 3) {
                //     inverted_volume = DAC_VOL_MAX - raw_adc;
                    dac_set_volume(DAC_VOL_MAX);
                    printf("Volume set to: %d", inverted_volume);
                // }
                // current_volume = raw_adc;
            }
            //SI4705 control
            else{
                if (abs(raw_adc - current_volume) < 3) {
                    inverted_volume = MAX_VOLUME_SI4705 - raw_adc;
                    si4705_set_volume(30);
                    printf("Volume set to: %d", inverted_volume);
                }
            current_volume = raw_adc;
            }
        }
        
        uint8_t pressed = buttons_get_just_pressed();

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
                case (BTN_A):
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

                // EQ up
                case (BTN_R): 
                    if (is_digital_audio){
                        dac_eq_adjust(eq_band, 0.5f, SAMPLE_SPEED); // Boost
                        dprint("Band %d Gain: %.1f dB\n", eq_band, dac_eq_get_gain(eq_band));
                    }
                    break;

                //Eq down
                case (BTN_L):
                    if (is_digital_audio){
                        dac_eq_adjust(eq_band, -0.5f, SAMPLE_SPEED); // Boost
                        dprint("Band %d Gain: %.1f dB\n", eq_band, dac_eq_get_gain(eq_band));
                    }
                    break;

                case (BTN_B):
                    if (is_digital_audio){
                        eq_band = (eq_band + 1) % 5;
                        dprint("Selected EQ Band: %d", eq_band);
                    }
                    break;

                // Change: digital <-> analog
                case (BTN_START):
                    current_freq = si4705_get_current_frequency();
                    switch_radio_audio_mode(player, current_freq, &is_digital_audio, current_volume, current_antenna);
                    eq_band = 0;
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