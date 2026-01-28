#pragma once
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdbool.h>

typedef struct {
    spi_inst_t *spi;
    uint cs;
    uint dcs;
    uint dreq;
    uint rst;
} vs1053_t;

void vs1053_init(vs1053_t *v);
void vs1053_soft_reset(vs1053_t *v);
void vs1053_set_volume(vs1053_t *v, uint8_t left, uint8_t right);
void vs1053_play_data(vs1053_t *v, const uint8_t *data, size_t len);
bool vs1053_ready(vs1053_t *v);
void vs1053_stop(vs1053_t *v);
void vs1053_set_play_speed(vs1053_t *player, uint16_t speed);
uint16_t vs1053_get_play_speed(vs1053_t *player);
void vs1053_enable_i2s(vs1053_t *v);
