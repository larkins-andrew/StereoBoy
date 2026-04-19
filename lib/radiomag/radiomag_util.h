#ifndef RADIOUTIL_H
#define RADIOUTIL_H

#include <stdio.h>
#include "pico/stdlib.h"
#include "lib/radiomag/si4705.h"
#include "lib/dac/dac.h"
#include "lib/buttons/buttons.h"
#include "lib/sb_util/sb_util.h"
#include "lib/pot/pot.h"
#include "lib/dac/dac.h"
// Define the LM4810 shutdown pin
#define PIN_AMP_SHUTDOWN 17
#define SAMPLE_SPEED 48000


int radioLoop(vs1053_t* player);


#endif