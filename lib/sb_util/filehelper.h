#ifndef FILEHELPER_H
#define FILEHELPER_H

#include <stdint.h>
#include <string.h>
#include "ff.h"         // Required for the FIL struct
#include "sb_util.h"    // Required for track_info_t


uint32_t syncsafe_to_uint(const uint8_t *b);
void read_text_frame(FIL *fil, uint32_t frame_size, char *out, size_t out_size);
uint32_t find_audio_start(FIL *fil);
void get_mp3_header(FIL *fil, track_info_t *track);
void get_mp3_metadata(const char *filename, track_info_t *track);
int compare_filenames(const void *a, const void *b);

#endif