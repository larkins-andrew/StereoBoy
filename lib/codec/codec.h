#include <stdio.h>
#include <string.h>
#include <stdlib.h> // Required for qsort
#include "pico/stdlib.h"
#include "sd_card.h"
#include "ff.h"
#include "hw_config.h"
#include "lib/vs1053.h"
#include "hardware/i2c.h"
#include "dac.h"



#define MAX_FILENAME_LEN 256 // max filaname character length
#define MAX_TRACKS 50        // max number of mp3 files in sd card

// SPI1 configuration for codec & sd card
#define PIN_SCK 30
#define PIN_MOSI 28
#define PIN_MISO 31
#define PIN_CS 32

// Debug LEDs

// Codec control signals
#define PIN_DCS 33
#define PIN_DREQ 29
#define PIN_RST 27

// I2C0 for DAC
#define PIN_I2C0_SCL 21
#define PIN_I2C0_SDA 20

#define SKIP_INTERVAL_MS 50 // minimum interval between FF/RW jumps


typedef struct
{
    char filename[256];
    char title[128];
    char artist[128];
    char album[128];
} track_info_t;

/* =======================
   Player state variables
   ======================= */

extern bool paused;
extern bool fast_forward;
extern bool audio_rewind;
extern uint16_t normal_speed;   // 1 = normal speed
extern uint16_t ff_speed;       // e.g. 3 = 3x speed

/* =======================
   Public functions
   ======================= */

void play_file(vs1053_t *player, const char *filename);

uint8_t readReg(i2c_inst_t *i2c, uint8_t reg);

void get_mp3_metadata(const char *filename, track_info_t *track);

int compare_filenames(const void *a, const void *b);