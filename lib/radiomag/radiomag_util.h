#ifndef RADIOUTIL_H
#define RADIOUTIL_H

#include "lib/sb_util/sb_util.h"

// Define the LM4810 shutdown pin
#define PIN_AMP_SHUTDOWN 17
#define SAMPLE_SPEED 48000


int radioLoop(vs1053_t* player);


#endif