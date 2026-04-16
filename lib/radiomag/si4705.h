#ifndef SI4705_H
#define SI4705_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"
#include <stdio.h>
#include "lib/codec/vs1053.h"

// --- Hardware Configuration ---
#define SI4705_I2C_PORT i2c0
#define SI4705_I2C_ADDR 0x11 // Set via driving SEN low during reset

#define SI4705_PIN_SDA  20
#define SI4705_PIN_SCL  21
#define SI4705_PIN_RST  18 
#define SI4705_PIN_SEN  19 

// --- Command Macros ---
#define CMD_POWER_UP       0x01
#define CMD_GET_INT_STATUS 0x14
#define CMD_SET_PROPERTY   0x12
#define CMD_FM_TUNE_FREQ   0x20
#define CMD_FM_TUNE_STATUS 0x22
#define CMD_GET_REV        0x10
#define CMD_FM_SEEK_START  0x21

// --- Property Macros ---
#define PROP_RX_VOLUME     0x4000
#define PROP_FM_ANTENNA_INPUT 0x1107
#define PROP_DIGITAL_OUTPUT_FORMAT      0x0102
#define PROP_DIGITAL_OUTPUT_SAMPLE_RATE 0x0104

#define ANTENNA_FMI 0  // Pin 8 (Headphone Antenna)
#define ANTENNA_LPI 1  // Pin 11 (PCB Trace Antenna)
#define PROP_FM_SEEK_TUNE_SNR_THRESHOLD  0x1403
#define PROP_FM_SEEK_TUNE_RSSI_THRESHOLD 0x1404
#define CMD_POWER_DOWN 0x11

// --- Function Prototypes ---
void si4705_init(void);
void si4705_power_up(uint8_t opmode); // Modify to accept the opmode
void si4705_power_down(void);
void switch_radio_audio_mode(vs1053_t *player, uint16_t current_freq, bool is_digital_audio, uint8_t current_volume, uint8_t current_antenna);
void si4705_set_property(uint16_t prop_id, uint16_t prop_value);
void si4705_tune_fm(uint16_t freq_10khz);
bool si4705_get_tune_status(uint8_t *rssi, uint8_t *snr);
void si4705_set_volume(uint8_t volume);
void si4705_select_antenna(uint8_t antenna_pin);
bool si4705_seek(bool seek_up, bool wrap); 
// Diagnostics
bool si4705_get_revision(uint8_t *part_number, uint8_t *fw_major, uint8_t *fw_minor);
uint16_t si4705_get_current_frequency(void);
void print_current_station();


#endif // SI4705_H