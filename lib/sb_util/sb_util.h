#ifndef SB_UTIL_H
#define SB_UTIL_H

#include "pico/stdlib.h"
#include "ff.h"
#include "lib/codec/vs1053.h"
#include "lib/display/display.h"
// #include "lib/font/font.h"


#include <complex.h>
#include <math.h>


#define MAX_TRACKS 64
#define MAX_FILENAME_LEN 256

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
void sb_play_track(vs1053_t *player, track_info_t *track, st7789_t *display);

/* ========= Display ========= */
void fast_drawline(int x, int y1, int y2, uint16_t color); // Unused?

// ADD THIS LINE HERE:
void update_scope_core1(void);


#ifndef PI
#define PI 3.141592653589793
#endif

typedef float complex cplx;

void dprint(char * str, ...);
void bit_reverse(cplx buf[], int n);
void fft_optimized(cplx buf[], int n);
void draw_bins(int n);
void draw_spectrum_bars(int x_start, int width, int h_l, int h_r, int target_l, int target_r);
void draw_lissajous();
void draw_lissajous_connected();
void album_art_centered();
void process_image(track_info_t *track, const char *filename, float output_size);

#endif
