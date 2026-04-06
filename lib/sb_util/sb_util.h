#ifndef SB_UTIL_H
#define SB_UTIL_H

#include "pico/stdlib.h"
#include "ff.h"
#include "lib/codec/vs1053.h"
#include "lib/display/display.h"
// #include "lib/font/font.h"
#include <complex.h>
#include <math.h>

typedef float complex cplx;

// SPI1 configuration for codec & sd card
#define PIN_SCK 30
#define PIN_MOSI 28
#define PIN_MISO 31
#define PIN_CS 32

// I2C0 for DAC
#define PIN_I2C0_SCL 21
#define PIN_I2C0_SDA 20

// I2C1 for LED Driver
#define PIN_I2C1_SDA 42
#define PIN_I2C1_SCL 43

// Display and oscope stuff
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define WAVE_COLOR 0x07E0 // Bright Green
#define BG_COLOR 0x0000   // Black

// Center at 0.65V (ADC is 12-bit, 0-3.3V)
#define ADC_CH 5

#define MAX_TRACKS 128
#define MAX_FILENAME_LEN 256

// Updated Constants for Split View
#define ADC_BIAS_CENTER 1551
#define ADC_RANGE_PKPK 1613
#define TARGET_HEIGHT 60 // Height of each individual wave (reduced to prevent overlap)

#define OFFSET_L 150 // Bottom half-ish
#define OFFSET_R 90  // Top half-ish

#define ADC_CH_L 6
#define ADC_CH_R 5

#define WAVE_L_COLOR 0x07E0
#define WAVE_R_COLOR 0x07FF
#define FFT_L_COLOR_DARK 0x0600
#define FFT_R_COLOR_DARK 0x05FF
#define FFT_L_COLOR_LIGHT 0x8FF1
#define FFT_R_COLOR_LIGHT 0xAFFF
#define IMG_WIDTH 160
#define IMG_HEIGHT 160

typedef struct {
    uint32_t album_art_size;
    uint32_t album_art_offset;
    uint32_t header;
    uint16_t bitrate;
    uint16_t samplespeed;
    uint8_t mpegID;
    uint8_t channels;
    uint8_t album_art_type;
    char mime_type[32];
    char filename[256];
    char title[128];
    char artist[128];
    char album[128];
} track_info_t;

/* ========= Hardware ========= */
void sb_hw_init(vs1053_t *player, st7789_t *display);

/* ========= Audio ========= */
void sb_audio_init(vs1053_t *player);

/* ========= MP3 / Metadata ========= */
int  sb_scan_tracks(track_info_t *tracks, int max_tracks);
void sb_print_track(track_info_t *t);

/* ========= Playback ========= */
int sb_play_track(vs1053_t *player, track_info_t *track, st7789_t *display);

/* ========= Display ========= */
void fast_drawline(int x, int y1, int y2, uint16_t color); // Unused?

// ADD THIS LINE HERE:
void update_scope_core1(void);

#ifndef PI
#define PI 3.141592653589793
#endif

void dprint(char * str, ...);

void album_art_centered();
void process_image(track_info_t *track, const char *filename, float output_size);

struct Node {
    struct Node * next;
    char str[30];
};

void printLL();
void pause_core1();
void resume_core1();
void set_visualizer(int num);
void clear_framebuffer();

#endif