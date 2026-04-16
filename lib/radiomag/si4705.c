#include "si4705.h"
#include "pico/stdlib.h"

// Wait for the Si4705 to set the Clear-To-Send (CTS) bit (Bit 7)
static void wait_for_cts(void) {
    uint8_t status = 0;
    while ((status & 0x80) == 0) {
        i2c_read_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, &status, 1, false);
        sleep_us(500); 
    }
}


void si4705_init(void) {
    //Initialize RP2350 I2C (FIX FOR INTEGRATION, SHOULD ALREADY BE DONE!)
    i2c_init(SI4705_I2C_PORT, 100 * 1000); 
    gpio_set_function(SI4705_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SI4705_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SI4705_PIN_SDA);
    gpio_pull_up(SI4705_PIN_SCL);

    //Setup RST and SEN pins
    gpio_init(SI4705_PIN_RST);
    gpio_set_dir(SI4705_PIN_RST, GPIO_OUT);
    
    gpio_init(SI4705_PIN_SEN);
    gpio_set_dir(SI4705_PIN_SEN, GPIO_OUT);

    //Reset Sequence: Drive SEN low to select 0x11 I2C Address
    gpio_put(SI4705_PIN_SEN, 0); 

    // Toggle Reset
    gpio_put(SI4705_PIN_RST, 0);
    sleep_ms(1); // Keep reset low
    gpio_put(SI4705_PIN_RST, 1);
    sleep_ms(5); // Allow time for device to power up
}

void si4705_power_up(uint8_t opMode) {
    uint8_t cmd[3];
    cmd[0] = CMD_POWER_UP;
    
    // CTS interrupt disabled, FM Receive, XOSCEN = 0
    cmd[1] = 0x00; 
    
    // ARG 2: OPMODE = 0x05 (Analog Audio Output Only)
    // cmd[2] = 0x05; 
    cmd[2] = opMode; //analog AND digital

    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, cmd, 3, false);
    wait_for_cts();
}

void si4705_power_down(void) {
    uint8_t cmd = CMD_POWER_DOWN;
    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, &cmd, 1, false);
    wait_for_cts();
}

void switch_radio_audio_mode(vs1053_t *player, uint16_t current_freq, bool is_digital_audio, uint8_t current_volume, uint8_t current_antenna) {
    printf("\nSwitching Audio Mode...\n");

    si4705_power_down();
    si4705_init();

    is_digital_audio = !is_digital_audio;

    if (is_digital_audio) {
        printf("Mode: DIGITAL (Si4705 is I2S Master)\n");
        
        //Silence the VS1053 and remove it from the bus
        vs1053_float_i2s_data(player);
        
        // Power up Si4705 with Analog & Digital outputs active
        si4705_power_up(0xB5); 
        
        // Configure Si4705 as I2S MASTER at 48kHz
        si4705_set_property(0x0102, 0x0008); 
        si4705_set_property(0x0104, 0xBB80); 
    } else {
        printf("Mode: ANALOG (Headphone Jack)\n");
        
        // Allow the VS1053 to take back control of the bus
        vs1053_claim_i2s_data(player);
        
        //Power up Si4705 with Analog output only (0x05)
        si4705_power_up(0x05); 
    }

    // Restore radio properties
    si4705_set_property(0x1100, 0x0002); 
    si4705_set_volume(current_volume);
    si4705_select_antenna(current_antenna);
    si4705_tune_fm(current_freq);
}


void si4705_set_property(uint16_t prop_id, uint16_t prop_value) {
    uint8_t cmd[6];
    cmd[0] = CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = (prop_id >> 8) & 0xFF;
    cmd[3] = prop_id & 0xFF;
    cmd[4] = (prop_value >> 8) & 0xFF;
    cmd[5] = prop_value & 0xFF;

    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, cmd, 6, false);
    wait_for_cts();
}


// --- Layer 3: Application / Tuning ---
void si4705_tune_fm(uint16_t freq_10khz) {
    uint8_t cmd[5];
    cmd[0] = CMD_FM_TUNE_FREQ;
    cmd[1] = 0x00;
    cmd[2] = (freq_10khz >> 8) & 0xFF;
    cmd[3] = freq_10khz & 0xFF;
    cmd[4] = 0x00; // Antenna tuning cap (0 = Auto)

    // Send the tune command
    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, cmd, 5, false);
    wait_for_cts();

    // The Si4705 requires us to explicitly poll the GET_INT_STATUS 
    // command to force the internal STCINT (Tune Complete) bit to update.
    uint8_t status_cmd = CMD_GET_INT_STATUS;
    uint8_t status = 0;
    
    while (true) {
        // Send GET_INT_STATUS
        i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, &status_cmd, 1, false);
        wait_for_cts(); // Wait for the chip to process the status request
        
        // Read the freshly updated status byte
        i2c_read_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, &status, 1, false);
        
        // Check if Bit 0 (STCINT) has been set to 1
        if (status & 0x01) { 
            break; // Tune is complete!
        }
        sleep_ms(5); // Wait 5ms before polling again
    }
}

bool si4705_get_tune_status(uint8_t *rssi, uint8_t *snr) {
    // INTACK = 1 clears the STC interrupt we just triggered
    uint8_t cmd[2] = {CMD_FM_TUNE_STATUS, 0x01}; 
    uint8_t resp[8];

    // Issue a repeated start (true) for the read operation
    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, cmd, 2, true);
    i2c_read_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, resp, 8, false);

    if (rssi) *rssi = resp[4];
    if (snr) *snr = resp[5];

    // Return the VALID bit (Bit 0 of Response Byte 1).
    // This returns true if the tuned frequency contains a valid, strong FM station.
    return (resp[1] & 0x01); 
}

void si4705_set_volume(uint8_t volume) {
    // Volume range is 0 to 63
    if (volume > 63) volume = 63;
    si4705_set_property(PROP_RX_VOLUME, volume);
}

void si4705_select_antenna(uint8_t antenna_pin) {
    // 0 = FMI, 1 = LPI
    if (antenna_pin > 1) {
        antenna_pin = 0; // Default to FMI if an invalid argument is passed
    }
    si4705_set_property(PROP_FM_ANTENNA_INPUT, antenna_pin);
}

// --- Diagnostics ---

bool si4705_get_revision(uint8_t *part_number, uint8_t *fw_major, uint8_t *fw_minor) {
    uint8_t cmd = CMD_GET_REV;
    uint8_t resp[16];

    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, &cmd, 1, false);
    wait_for_cts();
    
    // GET_REV returns 16 bytes of data. We read it out directly.
    i2c_read_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, resp, 16, false);

    if (part_number) *part_number = resp[1]; // Should be 0x05 for Si4705
    if (fw_major) *fw_major = resp[2];
    if (fw_minor) *fw_minor = resp[3];

    return (resp[1] == 0x05);
}

uint16_t si4705_get_current_frequency(void) {
    wait_for_cts(); //Wait for previous commands to finish

    // CMD_FM_TUNE_STATUS with INTACK = 0 (just read, don't clear interrupts)
    uint8_t cmd[2] = {CMD_FM_TUNE_STATUS, 0x00}; 
    uint8_t resp[8];

    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, cmd, 2, true);
    i2c_read_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, resp, 8, false);

    // The current frequency is stored across bytes 2 (High) and 3 (Low)
    uint16_t current_freq = (resp[2] << 8) | resp[3];
    return current_freq;
}

// --- Seeking ---

bool si4705_seek(bool seek_up, bool wrap) {
    uint8_t cmd[2];
    cmd[0] = CMD_FM_SEEK_START;
    
    // Build the argument byte
    cmd[1] = 0x00;
    if (seek_up) cmd[1] |= (1 << 3); // Set SEEKUP bit
    if (wrap)    cmd[1] |= (1 << 2); // Set WRAP bit

    // 1. Send the Seek Command
    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, cmd, 2, false);
    wait_for_cts();

    // 2. Poll for the Seek to Complete
    uint8_t status_cmd = CMD_GET_INT_STATUS;
    uint8_t status = 0;
    
    while (true) {
        i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, &status_cmd, 1, false);
        wait_for_cts();
        i2c_read_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, &status, 1, false);
        
        // Check if Bit 0 (STCINT) has been set to 1
        if (status & 0x01) { 
            break; 
        }
        sleep_ms(10); // Wait 10ms between polling
    }

    // 3. Clear the STC interrupt and check if a valid station was found
    uint8_t clear_cmd[2] = {CMD_FM_TUNE_STATUS, 0x01}; 
    uint8_t resp[8];
    i2c_write_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, clear_cmd, 2, true);
    i2c_read_blocking(SI4705_I2C_PORT, SI4705_I2C_ADDR, resp, 8, false);

    // Return the VALID bit. 
    // True = Stopped on a good station. False = Wrapped around and found nothing.
    return (resp[1] & 0x01); 
}

void print_current_station() {
    uint16_t freq = si4705_get_current_frequency();
    uint8_t rssi, snr;
    bool valid = si4705_get_tune_status(&rssi, &snr);
    
    printf("-> Station: %d.%d MHz | RSSI: %3d dBuV | SNR: %3d dB | Valid: %s\n", 
           freq / 100, (freq % 100) / 10, 
           rssi, snr, 
           valid ? "YES" : "NO");
}