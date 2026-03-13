#ifndef FFT_H
#define FFT_H

#include <complex.h>
#include <stdint.h>
#include <math.h>


typedef float complex cplx;

#ifndef PI
#define PI 3.141592653589793
#endif

#ifndef HISTORY_SIZE
#define HISTORY_SIZE 256
#endif

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 240
#endif

#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 240
#endif

#define FFT_L_COLOR_DARK  0x0600
#define FFT_R_COLOR_DARK  0x05FF
#define FFT_L_COLOR_LIGHT 0x8FF1
#define FFT_R_COLOR_LIGHT 0xAFFF



extern cplx audio_history_l[HISTORY_SIZE];
extern cplx audio_history_r[HISTORY_SIZE];
extern uint16_t frame_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];



void bit_reverse(cplx buf[], int n);
void fft_optimized(cplx buf[], int n);
void draw_bins(int n);
void draw_spectrum_bars(int x_start, int width, int h_l, int h_r, int target_l, int target_r);

#endif // FFT_H