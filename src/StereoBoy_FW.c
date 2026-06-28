#include "lib/sb_util/global_vars.h"

#include "lib/sb_util/sb_util.h"
#include "lib/buttons/buttons.h"
#include "lib/pot/pot.h"

#include "pico/stdlib.h"
#include "hardware/vreg.h"

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

track_info_t tracks[MAX_TRACKS];

folder_info_t folders[MAX_FOLDERS];

char folder_names[20][64];
int folder_file_counts[20];

int song_choice = 0;
int count;

int temp_visualizer = 1;

int main() {
    set_visualizer(7);
    // Lower RP2350 core voltage to 1V
    // P = V^2 * f, so 0.1V drop results in quadratic change
    // Before: 1.1 ^ 2 * 150 = 181.5
    // Now: 1.0 ^ 2 * 150 = 150
    vreg_set_voltage(VREG_VOLTAGE_1_00);

    stdio_init_all();

    sb_hw_init(&player, &display);
    
    // Boot-up banner

    // printf("\033c"); // clear screen

    printf(R"(
   _____ __                       ____             
  / ___// /____  ________  ____  / __ )____  __  __ 
  \__ \/ __/ _ \/ ___/ _ \/ __ \/ __  / __ \/ / / /
 ___/ / /_/  __/ /  /  __/ /_/ / /_/ / /_/ / /_/ / 
/____/\__/\___/_/   \___/\____/_____/\____/\__, /  
   MODULAR SUPER HI-FI STEREO SYSTEM      /____/
   ENGINEERING PROTOTYPE UNIT 001)");
    printf("\r\n\r\n");

    sleep_ms(750); // pause for dramatic effect

    dprint("Starting Folder Scan");

    uint8_t total_folders = sb_scan_folders(folders, 20);
    printf("--- Found %d Folders ---\n", total_folders);
    for (int i = 0; i < total_folders; i++) {
        printf("[%02d] %-16s (%d songs)\n", 
                i, 
                folders[i].foldername, 
                folders[i].num_tracks);
    }

    dprint("Starting Track Scan");
    // pause_core1();

    sb_scan_tracks(tracks, MAX_TRACKS);
    
    printf("--- Found %d MP3 Files ---\n", count);

    // --- Stream metadata for printing without loading an entire array ---
    FIL db_fil;
    UINT br;
    track_info_t temp_track; // Single local scratchpad

    if (f_open(&db_fil, "0:/.tracklib", FA_READ) == FR_OK)
    {
        for (int i = 0; i < count; i++) 
        {
            get_track_by_index(i, &temp_track);
            printf("[%02d] Title:  %s\n", i, temp_track.title);
            printf("     Artist: %s\n", temp_track.artist);
            printf("     Album:  %s\n", temp_track.album);
            printf("----------------------------------------\n");
        }
        f_close(&db_fil);
    } else {
        printf("Error: Could not open library.tracklib for reading.\n");
    }

    printf("--- End of List ---\n");

    // resume_core1();
    int exitCode = 0;
    int prev_choice = 0;
    bool selected = 0;
    // --- Print menu ---
    dprint("Debug print test %d", 1); //Trigger Core 2 Print
    printf("Debug print test %s\r\n", "2");
    
    song_choice = 0;
    
    while(1) {
        read_lwbt();
        temp_visualizer = (visualizer == 7) ? 1 : visualizer;
        //Return to main menu with list selection:
        if (exitCode == 0) {
            // pca9685_all_off(&vu_meter);
            selected = false;
            set_visualizer(6);
            // clear_framebuffer(); // Seemed completely unnecessary so commented out, but not deleting it
            printf("\r\nSong %d/%d: ", song_choice+1, count);
            prev_choice = song_choice;
            while (selected == false) {
                uint8_t pressed = buttons_get_just_pressed();
                if (pressed > 0){
                    if (pressed & BTN_D)      song_choice = (song_choice + 1) % count;
                    if (pressed & BTN_U)      song_choice = (song_choice - 1 + count) % count; //added roll-over
                        // shift songs down by one and insert new song on top
                    if (pressed & BTN_R)      song_choice = (song_choice + 10) % count;
                    if (pressed & BTN_L)      song_choice = (song_choice - 10 + count) % count;
                    if (pressed & BTN_A){
                        selected = true;   
                        printf("Poo cum fart shit pee");
                    }       
                }
                if (prev_choice != song_choice){
                    printf("\r\nSong %d/%d: ", song_choice+1, count);
                    prev_choice = song_choice;
                }
                
                sleep_ms(10);
            }
        }

        // 1. Allocate actual memory container for the struct on the stack
        track_info_t current_track;

        // 2. Pass its memory address using the '&' operato`r
        if (!get_track_by_index(song_choice, &current_track)) {
            printf("[Cache Error] Could not read index %d from .tracklib!\n", song_choice);
            exitCode = 1; // Skip playback execution if file lookup fails
            continue;
        }

        // 3. (Optional but Recommended) Extract fresh stream metrics on the fly 
        FIL temp_fil;
        if (f_open(&temp_fil, current_track.filename, FA_READ) == FR_OK) {
            get_mp3_header(&temp_fil, &current_track);
            get_mp3_metadata(current_track.filename, &current_track);
            current_track.audio_end = f_size(&temp_fil);
            f_close(&temp_fil);
        }

        printf("\r\n\rNOW PLAYING:\r\n");
        printf("  Title : %s\r\n", current_track.title);
        printf("  Artist: %s\r\n", current_track.artist);
        printf("  Album : %s\r\n", current_track.album);
        printf("  Bitrate : %d Kbps\r\n", current_track.bitrate);
        printf("  Sample rate : %d Hz\r\n", current_track.samplespeed);
        printf("  Channels : %s\r\n", current_track.channels == 1 ? "Mono" : "Stereo");
        printf("  Header: %X\r\n", current_track.header);
        printf("  Start: %X\r\n", current_track.audio_start);
        printf("  End: %X\r\n", current_track.audio_end);

        set_visualizer(temp_visualizer);
        
        // 4. Pass the address of your clean, populated local structure to jukebox
        exitCode = jukebox(&player, &current_track, &display);

        // play next song
        if (exitCode == 1){
            if (song_choice + 1 > count)
                song_choice = 0;
            else
                song_choice += 1;
            printf("\r\n Next song!\r\n");
            dprint("Next song!");
        }
        // play previous song
        if (exitCode == 2){
            if (song_choice - 1 < 1)
                song_choice = count;
            else
                song_choice -= 1;
            dprint("Prev Song!");
            printf("\r\nPrev Song!\r\n");
        }
        // play selected song in menu (visualizer 6)
        if (exitCode == 3){
            dprint("Playing picked Song!");
            printf("\r\nPlaying picked Song!\r\n");
        }
    }
}
