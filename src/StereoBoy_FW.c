#include "lib/sb_util/sb_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hw_config.h"
#include "hardware/i2c.h"
#include "lib/display/display.h"

#define MAX_FILENAME_LEN 256 // max filaname character length
#define MAX_TRACKS 64 // max number of mp3 files in sd card

// SPI1 configuration for codec & sd card
#define PIN_SCK  30
#define PIN_MOSI 28
#define PIN_MISO 31
#define PIN_CS   32

// Codec control signals
#define PIN_DCS  33
#define PIN_DREQ 29
#define PIN_RST  27

// I2C0 for DAC
#define PIN_I2C0_SCL 21
#define PIN_I2C0_SDA 20

vs1053_t player = {
    .spi = spi1,
    .cs = PIN_CS,
    .dcs = PIN_DCS,
    .dreq = PIN_DREQ,
    .rst = PIN_RST
};

struct st7789_t display = {
    .spi      = spi0,
    .gpio_din = 35,
    .gpio_clk = 34,
    .gpio_cs  = 37,
    .gpio_dc  = 39,
    .gpio_rst = 4,
    .gpio_bl  = 5,
};

#define LCD_WIDTH  240
#define LCD_HEIGHT 240
int main()
{

    stdio_init_all();

    sleep_ms(3000);

    sb_hw_init(&player, &display);

    track_info_t tracks[MAX_TRACKS];
    int count = sb_scan_tracks(tracks, MAX_TRACKS);

    // --- Print menu ---
    dprint("Example %s", "thing");
    while(1) {
        dprint("Available tracks:");
        printf("\r\nAvailable tracks:\r\n");
        for (int i = 0; i < count; i++) {
            // dprint("[%d] %s - %s", i + 1, tracks[i].artist, tracks[i].title);
            printf("\r\n[%d] %s - %s\r\n", i + 1, tracks[i].artist, tracks[i].title);
            // dprint("     Album: %s", tracks[i].album);
            printf("     Album: %s\r\n", tracks[i].album);
            // dprint("     Bit Rate: %d Kbps", tracks[i].bitrate);
            printf("     Bit Rate: %d Kbps\r\n", tracks[i].bitrate);
            // dprint("     Sample Rate: %d Hz", tracks[i].samplespeed);
            printf("     Sample Rate: %d Hz\r\n", tracks[i].samplespeed);
            // dprint("     Channels : %s", tracks[i].channels == 1 ? "Mono" : "Stereo");
            printf("     Channels : %s\r\n", tracks[i].channels == 1 ? "Mono" : "Stereo");
            // dprint("     Header: %X", tracks[i].header);
            printf("     Header: %X\r\n", tracks[i].header);
        }

        char input[8];
        int choice = 0;

        while (choice < 1 || choice > count) {
            dprint("Select track (1-%d): ", count);
            printf("\r\nSelect track (1-%d): ", count);

            int idx = 0;
            memset(input, 0, sizeof(input));

            while (1) {
                int c = getchar(); // blocking read
                if (c == '\r' || c == '\n') { // Enter pressed
                    dprint("");
                    printf("\r\n");
                    break;
                }
                if (idx < sizeof(input)-1) {
                    input[idx++] = c;
                    putchar(c); // echo typed char
                }
            }

            choice = atoi(input);
            if (choice < 1 || choice > count)
                dprint("Invalid. Try again.");
                printf("Invalid. Try again.");
        }

        track_info_t *track = &tracks[choice - 1];

        dprint("NOW PLAYING:");
        printf("\r\n\rNOW PLAYING:\r\n");
        dprint("  Title : %s", track->title);
        printf("  Title : %s\r\n", track->title);
        dprint("  Artist: %s", track->artist);
        printf("  Artist: %s\r\n", track->artist);
        dprint("  Album : %s", track->album);
        printf("  Album : %s\r\n", track->album);
        dprint("  Bitrate : %d Kbps", track->bitrate);
        printf("  Bitrate : %d Kbps\r\n", track->bitrate);
        dprint("  Sample rate : %d Hz", track->samplespeed);
        printf("  Sample rate : %d Hz\r\n", track->samplespeed);
        dprint("  Channels : %s", track->channels == 1 ? "Mono" : "Stereo");
        printf("  Channels : %s\r\n", track->channels == 1 ? "Mono" : "Stereo");
        dprint("  Header: %X", track->header);
        printf("  Header: %X\r\n", track->header);

        sb_play_track(&player, track, &display);

        dprint("Playback finished!");
        printf("\r\nPlayback finished!\r\n");
    }
}
