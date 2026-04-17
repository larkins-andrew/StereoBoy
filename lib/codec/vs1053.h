#pragma once
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdbool.h>

#define VS_WRITE 0x02
#define VS_READ  0x03

#define SCI_MODE    0x00
#define SCI_CLOCKF  0x03
#define SCI_VOL     0x0B
#define SCI_AUDATA  0x05

#define SCI_WRAM      0x06
#define SCI_WRAMADDR  0x07

#define VS1053_PARA_PLAYSPEED 0x1E04

typedef struct {
    spi_inst_t *spi;
    uint cs;
    uint dcs;
    uint dreq;
    uint rst;
} vs1053_t;

void sci_write(vs1053_t *v, uint8_t addr, uint16_t data);
uint16_t sci_read(vs1053_t *v, uint8_t addr);
void vs1053_init(vs1053_t *v);
void vs1053_soft_reset(vs1053_t *v);
void vs1053_set_volume(vs1053_t *v, uint8_t left, uint8_t right);
void vs1053_play_data(vs1053_t *v, const uint8_t *data, size_t len);
bool vs1053_ready(vs1053_t *v);
void vs1053_stop(vs1053_t *v);
void vs1053_set_play_speed(vs1053_t *player, uint16_t speed);
uint16_t vs1053_get_play_speed(vs1053_t *player);
void vs1053_enable_i2s(vs1053_t *v);
void vs1053_set_samplerate(vs1053_t *player, uint16_t samplerate);
void vs1053_load_patch(vs1053_t *v, const unsigned short* plugin, unsigned short plugin_size);
void vs1053_tape_stop(vs1053_t *v);
void adjustRate(vs1053_t *v, long ppm2);
void vs1053_float_i2s_data(vs1053_t *v);
void vs1053_claim_i2s_bus(vs1053_t *v);


