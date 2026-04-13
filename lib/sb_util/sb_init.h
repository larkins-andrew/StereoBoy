#ifndef SB_INIT_H
#define SB_INIT_H


#include "pico/stdlib.h"
#include "ff.h"
#include "lib/codec/vs1053.h"
#include "lib/display/display.h"
#include "lib/sb_util/hw_map.h"

// #include "lib/font/font.h"

#include <complex.h>
#include <math.h>

/* ========= Hardware ========= */
void sb_hw_init(vs1053_t *player, st7789_t *display);

#endif