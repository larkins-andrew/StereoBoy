#include "lib/sb_util/sb_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hw_config.h"
#include "hardware/i2c.h"
#include "lib/display/display.h"
#include "lib/led_driver/led_driver.h"
#include "lib/buttons/buttons.h"
#include "lib/pot/pot.h"
#include "lib/radiomag/radiomag_util.h"

// #define DEBUG // print all dprints to terminal

#define MAX_FILENAME_LEN 256 // max filaname character length
#define MAX_TRACKS 128 // max number of mp3 files in sd card

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
    // Boot-up banner

    sleep_ms(250);

    printf("\033c"); // clear screen

    printf(R"(
   _____ __                       ____             
  / ___// /____  ________  ____  / __ )____  __  __ 
  \__ \/ __/ _ \/ ___/ _ \/ __ \/ __  / __ \/ / / /
 ___/ / /_/  __/ /  /  __/ /_/ / /_/ / /_/ / /_/ / 
/____/\__/\___/_/   \___/\____/_____/\____/\__, /  
   MODULAR SUPER HI-FI STEREO SYSTEM      /____/
   ENGINEERING PROTOTYPE UNIT 001)");
    printf("\r\n\r\n");

    // sleep_ms(750); // pause for dramatic effect

    dprint("Starting Track Scan");
    // pause_core1();
    track_info_t tracks[MAX_TRACKS];
    int count = sb_scan_tracks(tracks, MAX_TRACKS);
    // resume_core1();
    int exitCode = 0;
    int choice = 0;
    int prev_choice = 0;
    bool selected = 0;
    bool inRadio = 0;
    // --- Print menu ---
    dprint("Debug print test %d", 1); //Trigger Core 2 Print
    printf("Debug print test %s\r\n", "2");
    
    
    while(1) {
        read_lwbt();
        //Return to main menu with list selection:
        if (exitCode == 0) {
            selected = false; 
            set_visualizer(5);
            choice = 0;
            bool confirmed = 0;
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

            clear_framebuffer();
            dprint("Song %d/%d: ", choice+1, count);
            printf("\r\nSong %d/%d: ", choice+1, count);

            dprint("%s", tracks[choice].title);
            dprint("%s", tracks[choice].artist);
            prev_choice = choice;
            while (selected == false) {
                uint8_t pressed = buttons_get_just_pressed();
                if (pressed > 0){
                    if (pressed & BTN_R)      choice = (choice + 1) % count;
                    if (pressed & BTN_L)      choice = (choice - 1 + count) % count; //added roll-over
                    if (pressed & BTN_U)      choice = (choice + 10) % count;
                    if (pressed & BTN_D)      choice = (choice - 10 + (count * 10)) % count; //added roll-over
                    if (pressed & BTN_A)      selected = true;   
                    if (pressed & BTN_SELECT) inRadio = radioLoop(&player);
                       
                }
                if ((prev_choice != choice)){
                    clear_framebuffer();
                    dprint("Song %d/%d: ", choice+1, count);
                    printf("\r\nSong %d/%d: ", choice+1, count);

                    dprint("%s", tracks[choice].title);
                    dprint("%s", tracks[choice].artist);
                    prev_choice = choice;
                }
                if (inRadio){
                    clear_framebuffer();
                    dprint("Song %d/%d: ", choice+1, count);
                    printf("\r\nSong %d/%d: ", choice+1, count);

                    dprint("%s", tracks[choice].title);
                    dprint("%s", tracks[choice].artist);
                    inRadio = 0;
                }
                
                sleep_ms(10);
            }
        }
        printf("outside loop");
        track_info_t *track = &tracks[choice];

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
        printf("  Start: %X\r\n", track->audio_start);
        printf("  Start: %X\r\n", track->audio_end);

        set_visualizer(1);
        exitCode = sb_play_track(&player, track, &display);

        if (exitCode == 1){
            if (choice + 1 > count)
                choice = 1;
            else
                choice += 1;
            printf("\r\n Next song!\r\n");
            dprint("Next song!");
        }
        if (exitCode == 2){
            if (choice - 1 < 1)
                choice = count;
            else
                choice -= 1;
            dprint("Prev Song!");
            printf("\r\nPrev Song!\r\n");
        }

        // dprint("Playback finished!");
        // printf("\r\nPlayback finished!\r\n");
    }
}
