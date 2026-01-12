#include "vs1053.h"

#define VS_WRITE 0x02
#define VS_READ  0x03

#define SCI_MODE    0x00
#define SCI_CLOCKF  0x03
#define SCI_VOL     0x0B

#define SCI_WRAM      0x06
#define SCI_WRAMADDR  0x07

#define VS1053_PARA_PLAYSPEED 0x1E06

static inline void cs_low(uint pin)  { gpio_put(pin, 0); }
static inline void cs_high(uint pin) { gpio_put(pin, 1); }

static void wait_dreq(vs1053_t *v) {
    while (!gpio_get(v->dreq)) tight_loop_contents();
}

static void sci_write(vs1053_t *v, uint8_t addr, uint16_t data) {
    wait_dreq(v);

    uint8_t buf[4] = {VS_WRITE, addr, data >> 8, data & 0xFF};
    cs_low(v->cs);
    spi_write_blocking(v->spi, buf, 4);
    cs_high(v->cs);
}

static uint16_t sci_read(vs1053_t *v, uint8_t addr) {
    wait_dreq(v);

    uint8_t tx[4] = {VS_READ, addr, 0xFF, 0xFF};
    uint8_t rx[4];

    cs_low(v->cs);
    spi_write_read_blocking(v->spi, tx, rx, 4);
    cs_high(v->cs);

    return (rx[2] << 8) | rx[3];
}

bool vs1053_ready(vs1053_t *v) {
    return gpio_get(v->dreq);
}

void vs1053_init(vs1053_t *v) {
    gpio_init(v->cs);   gpio_set_dir(v->cs, GPIO_OUT);  gpio_put(v->cs, 1);
    gpio_init(v->dcs);  gpio_set_dir(v->dcs, GPIO_OUT); gpio_put(v->dcs, 1);
    gpio_init(v->rst);  gpio_set_dir(v->rst, GPIO_OUT); gpio_put(v->rst, 1);
    gpio_init(v->dreq); gpio_set_dir(v->dreq, GPIO_IN);

    spi_init(v->spi, 1 * 1000 * 1000);
    spi_set_format(v->spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    vs1053_soft_reset(v);

    // Boost clock (datasheet recommended)
    sci_write(v, SCI_CLOCKF, 0x6000);
    sleep_ms(10);
}

void vs1053_soft_reset(vs1053_t *v) {
    gpio_put(v->rst, 0);
    sleep_ms(10);
    gpio_put(v->rst, 1);
    sleep_ms(10);

    sci_write(v, SCI_MODE, 0x0804); // SM_SDINEW | reset
    sleep_ms(10);
}

void vs1053_set_volume(vs1053_t *v, uint8_t left, uint8_t right) {
    sci_write(v, SCI_VOL, ((uint16_t)left << 8) | right);
}

void vs1053_play_data(vs1053_t *v, const uint8_t *data, size_t len) {
    size_t i = 0;

    while (i < len) {
        wait_dreq(v);

        size_t chunk = (len - i > 32) ? 32 : len - i;

        cs_low(v->dcs);
        spi_write_blocking(v->spi, data + i, chunk);
        cs_high(v->dcs);

        i += chunk;
    }
}

void vs1053_stop(vs1053_t *v) {
    sci_write(v, SCI_MODE, 0x0808); // SM_CANCEL | SM_SDINEW
}

void vs1053_set_play_speed(vs1053_t *player, uint16_t speed) {
    uint8_t buf[4];
    buf[0] = 0x02; // SCI_WRITE
    buf[1] = (VS1053_PARA_PLAYSPEED & 0xFF); // address low byte
    buf[2] = speed >> 8;
    buf[3] = speed & 0xFF;
    spi_write_blocking(player->spi, buf, 4);
}

uint16_t vs1053_get_play_speed(vs1053_t *player) {
    uint8_t buf[4] = {0x03, (VS1053_PARA_PLAYSPEED & 0xFF), 0, 0}; // SCI_READ
    uint8_t resp[2];
    spi_write_read_blocking(player->spi, buf, resp, 2);
    return ((uint16_t)resp[0] << 8) | resp[1];
}

void vs1053_enable_i2s(vs1053_t *v) {
    // Set GPIO4–GPIO7 as outputs
    sci_write(v, SCI_WRAMADDR, 0xC017);  // GPIO_DDR
    sci_write(v, SCI_WRAM,     0x00F0);  // bits 4–7 = outputs
    
    // Select I2S_CONFIG address
    sci_write(v, SCI_WRAMADDR, 0xC040);

    // Enable I2S + MCLK (bits 6 and 7)
    sci_write(v, SCI_WRAM, 0x000C);
}
