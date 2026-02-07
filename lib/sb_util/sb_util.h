#ifndef SB_UTIL_H
#define SB_UTIL_H

#include "pico/stdlib.h"
#include "ff.h"
#include "lib/codec/vs1053.h"
#include "../display/display.h"

#define MAX_TRACKS 64
#define MAX_FILENAME_LEN 256

typedef struct {
    char filename[256];
    char title[128];
    char artist[128];
    char album[128];
    uint8_t mpegID;
    uint16_t bitrate;
    uint16_t samplespeed;
    uint8_t channels;
    uint32_t header;
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
void fast_drawline(int x, int y1, int y2, uint16_t color);

// ADD THIS LINE HERE:
void update_visualizer_core1(void);

#endif
