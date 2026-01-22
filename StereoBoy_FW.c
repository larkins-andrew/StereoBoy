#include <stdio.h>
#include <string.h>
#include <stdlib.h> // Required for qsort
#include "pico/stdlib.h"
#include "sd_card.h"
#include "ff.h"
#include "hw_config.h"
#include "lib/vs1053.h"
#include "lib/dac.h"

#define MAX_FILENAME_LEN 256 // max filaname character length
#define MAX_TRACKS 50 // max number of mp3 files in sd card

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

#define SKIP_INTERVAL_MS 100   // minimum interval between FF/RW jumps

vs1053_t player = {
    .spi = spi1,
    .cs = PIN_CS,
    .dcs = PIN_DCS,
    .dreq = PIN_DREQ,
    .rst = PIN_RST
};

// Convert syncsafe integer (ID3 size format)
static uint32_t syncsafe_to_uint(const uint8_t *b) {
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3];
}

static void read_text_frame(FIL *fil, uint32_t frame_size, char *out, size_t out_size) {
    UINT br;
    uint8_t encoding;

    f_read(fil, &encoding, 1, &br);
    frame_size--;

    memset(out, 0, out_size);

    // ---------------- UTF-8 ----------------
    if (encoding == 3) {
        uint32_t n = (frame_size < out_size - 1) ? frame_size : out_size - 1;
        f_read(fil, out, n, &br);
        out[n] = '\0';
        return;
    }

    // ---------------- ISO-8859-1 → UTF-8 ----------------
    if (encoding == 0) {
        uint8_t b;
        size_t oi = 0;

        for (uint32_t i = 0; i < frame_size && oi < out_size - 1; i++) {
            f_read(fil, &b, 1, &br);
            if (b < 0x80) {
                out[oi++] = b;
            } else {
                if (oi + 2 >= out_size) break;
                out[oi++] = 0xC0 | (b >> 6);
                out[oi++] = 0x80 | (b & 0x3F);
            }
        }
        out[oi] = '\0';
        return;
    }

    // ---------------- UTF-16 → UTF-8 ----------------
    if (encoding == 1 || encoding == 2) {
        bool little_endian = true;
        uint8_t bom[2];

        // Read BOM if present
        f_read(fil, bom, 2, &br);
        frame_size -= 2;

        if (encoding == 1) {
            if (bom[0] == 0xFE && bom[1] == 0xFF)
                little_endian = false;
            else if (bom[0] == 0xFF && bom[1] == 0xFE)
                little_endian = true;
        } else {
            // UTF-16BE without BOM
            little_endian = false;
        }

        size_t oi = 0;
        for (uint32_t i = 0; i + 1 < frame_size && oi < out_size - 1; i += 2) {
            uint8_t b1, b2;
            f_read(fil, &b1, 1, &br);
            f_read(fil, &b2, 1, &br);

            uint16_t ch = little_endian ? (b1 | (b2 << 8)) : ((b1 << 8) | b2);
            if (ch == 0x0000) break;

            if (ch < 0x80) {
                out[oi++] = ch;
            } else if (ch < 0x800 && oi + 2 < out_size) {
                out[oi++] = 0xC0 | (ch >> 6);
                out[oi++] = 0x80 | (ch & 0x3F);
            } else if (oi + 3 < out_size) {
                out[oi++] = 0xE0 | (ch >> 12);
                out[oi++] = 0x80 | ((ch >> 6) & 0x3F);
                out[oi++] = 0x80 | (ch & 0x3F);
            }
        }

        out[oi] = '\0';
        return;
    }

    // Unknown encoding → skip
    f_lseek(fil, f_tell(fil) + frame_size);
}

typedef struct {
    char filename[256];
    char title[128];
    char artist[128];
    char album[128];
} track_info_t;

static uint32_t find_audio_start(FIL *fil) {
    UINT br;
    uint8_t header[10];

    f_lseek(fil, 0);

    if (f_read(fil, header, 10, &br) != FR_OK || br != 10)
        return 0;

    if (memcmp(header, "ID3", 3) == 0) {
        uint32_t tag_size = syncsafe_to_uint(&header[6]);
        return 10 + tag_size;
    }

    // No ID3 tag → audio starts at 0
    return 0;
}

static void get_mp3_metadata(const char *filename, track_info_t *track) {
    strcpy(track->filename, filename);
    strcpy(track->title, "(unknown)");
    strcpy(track->artist, "(unknown)");
    strcpy(track->album, "(unknown)");

    FIL fil;
    UINT br;
    uint8_t header[10];
    uint8_t frame_header[10];

    if (f_open(&fil, filename, FA_READ) != FR_OK)
        return;

    if (f_read(&fil, header, 10, &br) != FR_OK || br != 10)
        goto out;

    if (memcmp(header, "ID3", 3) != 0)
        goto out;

    uint32_t tag_size = syncsafe_to_uint(&header[6]);
    uint32_t bytes_read = 0;

    while (bytes_read < tag_size) {
        if (f_read(&fil, frame_header, 10, &br) != FR_OK || br != 10)
            break;

        bytes_read += 10;
        if (frame_header[0] == 0) break;

        char id[5];
        memcpy(id, frame_header, 4);
        id[4] = 0;

        uint32_t size =
            (frame_header[4] << 24) |
            (frame_header[5] << 16) |
            (frame_header[6] << 8) |
            frame_header[7];

        if (!strcmp(id, "TIT2"))
            read_text_frame(&fil, size, track->title, sizeof(track->title));
        else if (!strcmp(id, "TPE1"))
            read_text_frame(&fil, size, track->artist, sizeof(track->artist));
        else if (!strcmp(id, "TALB"))
            read_text_frame(&fil, size, track->album, sizeof(track->album));
        else
            f_lseek(&fil, f_tell(&fil) + size);

        bytes_read += size;
    }

out:
    f_close(&fil);
}

/* ##########################################################
JUKEBOX: MAIN PLAY LOOP
########################################################## */

/* ##########################################################
JUKEBOX: MAIN PLAY LOOP
########################################################## */

bool paused = false;
bool warping = false;
bool stopped = false;
bool warping = false;
bool stopped = false;
bool fast_forward = false;
bool audio_rewind = false;
uint16_t normal_speed = 1;  // 1 = normal

#define PAUSE_WARP_US 600000      // 0.7 seconds for pause
#define RESUME_WARP_US 1200000    // 1.2 seconds for resume

#define PAUSE_WARP_US 600000      // 0.7 seconds for pause
#define RESUME_WARP_US 1200000    // 1.2 seconds for resume

void jukebox(vs1053_t *player, const char *filename) {
    FIL fil; // file object
    UINT br; // pointer to number of bytes read
    uint8_t buffer[512]; // buffer read from file

    sci_write(player, 0x05, 0xAC45); // initialize codec sampling speed to 44.1 Khz

    // status bits for player state and warp effect
void jukebox(vs1053_t *player, const char *filename) {
    FIL fil; // file object
    UINT br; // pointer to number of bytes read
    uint8_t buffer[512]; // buffer read from file

    sci_write(player, 0x05, 0xAC45); // initialize codec sampling speed to 44.1 Khz

    // status bits for player state and warp effect
    paused = false;
    warping = false;
    stopped = 0;

    // more warp effect stuff
    float transport = 1.0f; // desired speed
    float warp_start_transport = 1.0f; // start speed for warp
    float warp_target = 1.0f; // target speed for warp
    uint32_t warp_duration = RESUME_WARP_US; // warp effect duration
    absolute_time_t warp_start_time;

    // open selected MP3 file
    warping = false;
    stopped = 0;

    // more warp effect stuff
    float transport = 1.0f; // desired speed
    float warp_start_transport = 1.0f; // start speed for warp
    float warp_target = 1.0f; // target speed for warp
    uint32_t warp_duration = RESUME_WARP_US; // warp effect duration
    absolute_time_t warp_start_time;

    // open selected MP3 file
    if (f_open(&fil, filename, FA_READ) != FR_OK) {
        printf("Failed to open %s\r\n", filename);
        return;
    }

    uint16_t reg = sci_read(player, 0x05); // read SCI_AUDATA (0x05) from codec to get sampling speed info
    uint16_t stereo_bit = reg & 1; // LSB indicates mono or stereo (not exactly sure what but this is pretty much always 1)
    uint16_t base_rate  = reg & 0xFFFE; // sampling speed in upper 15 bits

    uint16_t reg = sci_read(player, 0x05); // read SCI_AUDATA (0x05) from codec to get sampling speed info
    uint16_t stereo_bit = reg & 1; // LSB indicates mono or stereo (not exactly sure what but this is pretty much always 1)
    uint16_t base_rate  = reg & 0xFFFE; // sampling speed in upper 15 bits

    uint32_t start = find_audio_start(&fil);
    f_lseek(&fil, start);
    absolute_time_t last_skip_time = get_absolute_time();
    absolute_time_t last_skip_time = get_absolute_time();

    // This while loop continuously scans for key inputs while playing audio.
    // Warping is achieved by continuously sending audio bytes after pause point until warp duration is met.
    // This while loop continuously scans for key inputs while playing audio.
    // Warping is achieved by continuously sending audio bytes after pause point until warp duration is met.
    while (1) {
        int c = getchar_timeout_us(0); // nonblocking getchar
        int c = getchar_timeout_us(0); // nonblocking getchar
        if (c != PICO_ERROR_TIMEOUT) {
            long pos = f_tell(&fil);
            // bool headphonesIn = dac_read(0, 0x43) & 0x20;
            // printf("Headphone prescence: %d\r\n", headphonesIn);
            // bool headphonesIn = dac_read(0, 0x43) & 0x20;
            // printf("Headphone prescence: %d\r\n", headphonesIn);
            absolute_time_t now = get_absolute_time();
            switch (c) {
                case 'p':
                case 'P':
                    paused = !paused; // set paused flag
                    warp_start_time = get_absolute_time(); // get timestamp for warp start
                    warp_start_transport = transport; // 
                    warp_target = paused ? 0.0f : 1.0f;
                    warping = true;

                    // select duration based on pause/resume
                    warp_duration = paused ? PAUSE_WARP_US : RESUME_WARP_US;

                    printf(paused ? "\r\nTape slowing...\r\n"
                                   : "\r\nTape resuming...\r\n");
                    paused = !paused; // set paused flag
                    warp_start_time = get_absolute_time(); // get timestamp for warp start
                    warp_start_transport = transport; // 
                    warp_target = paused ? 0.0f : 1.0f;
                    warping = true;

                    // select duration based on pause/resume
                    warp_duration = paused ? PAUSE_WARP_US : RESUME_WARP_US;

                    printf(paused ? "\r\nTape slowing...\r\n"
                                   : "\r\nTape resuming...\r\n");
                    break;
                case 'f':
                case 'F':
                    if (absolute_time_diff_us(last_skip_time, now) >= SKIP_INTERVAL_MS * 1000) {
                        pos += 320 * 128;
                        pos += 320 * 128;
                        if (pos > f_size(&fil)) pos = f_size(&fil) - 1;
                        f_lseek(&fil, pos);
                        printf("\r\nFast-forwarded ~320Kb\r\n");
                        printf("\r\nFast-forwarded ~320Kb\r\n");
                        last_skip_time = now;
                    }
                    break;
                case 'r':
                case 'R':
                    if (absolute_time_diff_us(last_skip_time, now) >= SKIP_INTERVAL_MS * 1000) {
                        pos -= 320 * 128;
                        pos -= 320 * 128;
                        if (pos < 0) pos = 0;
                        f_lseek(&fil, pos);
                        printf("\r\nRewound ~320Kb\r\n");
                        printf("\r\nRewound ~320Kb\r\n");
                        last_skip_time = now;
                    }
                    break;
                case 'u':
                case 'U':
                    dac_increase_volume(3);
                    dac_increase_volume(3);
                    printf("\r\nVolume up!\r\n");
                    break;
                case 'd':
                case 'D':
                    dac_decrease_volume(3);
                    dac_decrease_volume(3);
                    printf("\r\nVolume down!\r\n");
                    break;
                case 's':
                case 'S':
                    if (paused) {
                        vs1053_set_play_speed(player, 0); // hard pause
                        printf("\r\nStopping....\r\n");
                        f_close(&fil);
                        vs1053_stop(player);
                        return;
                    }
                    stopped = 1;
                    warp_start_time = get_absolute_time();
                    warp_start_transport = transport;
                    warp_target = 0.0f;
                    warp_duration = PAUSE_WARP_US;
                    warping = true;
                    printf("Stopping...\r\n");
                    break;
                    if (paused) {
                        vs1053_set_play_speed(player, 0); // hard pause
                        printf("\r\nStopping....\r\n");
                        f_close(&fil);
                        vs1053_stop(player);
                        return;
                    }
                    stopped = 1;
                    warp_start_time = get_absolute_time();
                    warp_start_transport = transport;
                    warp_target = 0.0f;
                    warp_duration = PAUSE_WARP_US;
                    warping = true;
                    printf("Stopping...\r\n");
                    break;
            }
        }

        // Always feed decoder unless fully paused
        if (!paused || warping) {
        // Always feed decoder unless fully paused
        if (!paused || warping) {
            if (f_read(&fil, buffer, sizeof(buffer), &br) != FR_OK || br == 0)
                break;

            vs1053_play_data(player, buffer, br);
        }

        // --- Warp logic ---
        if (warping) {
            int64_t elapsed = absolute_time_diff_us(warp_start_time, get_absolute_time());

            if (elapsed >= warp_duration) {
                transport = warp_target;
                warping = false;

                if (paused) {
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nPaused.\r\n");
                } else if (stopped) {
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nPaused.\r\n");
                    f_close(&fil);
                    vs1053_stop(player);
                    return;
                }
            } else {
                float t = (float)elapsed / (float)warp_duration;
                transport = warp_start_transport +
                            (warp_target - warp_start_transport) * t;
            }
        }

        // --- Apply playback rate + volume ---
        if (!paused || warping) {
            uint16_t new_rate = (uint16_t)(base_rate * transport) & 0xFFFE;
            if (new_rate < 9000) new_rate = 9000;
            sci_write(player, 0x05, new_rate | stereo_bit);

            // volume scales with transport
            // uint8_t vol = (uint8_t)(0xFE * (1.0f - transport));
            // if (vol > 0xFE) vol = 0xFE;
            // vs1053_set_volume(player, vol, vol);
        }
    }

        }

        // --- Warp logic ---
        if (warping) {
            int64_t elapsed = absolute_time_diff_us(warp_start_time, get_absolute_time());

            if (elapsed >= warp_duration) {
                transport = warp_target;
                warping = false;

                if (paused) {
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nPaused.\r\n");
                } else if (stopped) {
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nPaused.\r\n");
                    f_close(&fil);
                    vs1053_stop(player);
                    return;
                }
            } else {
                float t = (float)elapsed / (float)warp_duration;
                transport = warp_start_transport +
                            (warp_target - warp_start_transport) * t;
            }
        }

        // --- Apply playback rate + volume ---
        if (!paused || warping) {
            uint16_t new_rate = (uint16_t)(base_rate * transport) & 0xFFFE;
            if (new_rate < 9000) new_rate = 9000;
            sci_write(player, 0x05, new_rate | stereo_bit);

            // volume scales with transport
            // uint8_t vol = (uint8_t)(0xFE * (1.0f - transport));
            // if (vol > 0xFE) vol = 0xFE;
            // vs1053_set_volume(player, vol, vol);
        }
    }

    f_close(&fil);
}

// Helper for qsort
static int compare_filenames(const void *a, const void *b) {
    const track_info_t *ta = (const track_info_t *)a;
    const track_info_t *tb = (const track_info_t *)b;
    return strcasecmp(ta->filename, tb->filename);
}

// Headphones disconnect interrupt
void dac_int_callback(uint gpio, uint32_t events) {
    // Read 0x2C to clear the sticky interrupt
    dac_read(0, 0x2C); // THIS NEEDS TO BE HERE!!!! DO NOT REMOVE THIS LINE
    // read whether headphone in or out
    if (dac_read(0, 0x2E) & 0x10) { // Bit 5
        printf("Headphones plugged in! Paused and switching to stereo headphones.\n");
        dac_write(1, 0x20, 0b00000110); // shut down speaker driver
        // pause without warping
        paused = 1;
        warping = 0;
    } else {
        printf("Headphones pulled out! Paused and switching to mono speakers.\n");
        uint16_t mode = sci_read(&player, 0x00);  // SCI_MODE
        sci_write(&player, 0x00, mode & 0xFFFE);
        dac_write(1, 0x20, 0b10000110); // power up speaker driver
        // pause without warping
        paused = 1;
        warping = 0;
    }
}

// ---- Init GPIO interrupt ----
void dac_interrupt_init(void) {
    gpio_init(15);
    gpio_set_dir(15, GPIO_IN);
    gpio_pull_up(15);  // INT is usually open-drain

    gpio_set_irq_enabled_with_callback(
        15,
        GPIO_IRQ_EDGE_RISE,   // active-low interrupt
        true,
        &dac_int_callback
    );
}

int main() {

    stdio_init_all();

    sleep_ms(3000);
    sleep_ms(3000);

    // set SPI0 for codec and SD card
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);

    // set I2C0 for DAC
    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(PIN_I2C0_SCL, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C0_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C0_SCL);
    gpio_pull_up(PIN_I2C0_SDA);

    printf("SPI0 and I2C0 initialized.\r\n");

    if (!sd_init_driver()) {
        printf("SD init failed\r\n");
        while (1);
    }

    FATFS fs;
    if (f_mount(&fs, "0:", 1) != FR_OK) {
        printf("Mount failed\r\n");
        while (1);
    }

    vs1053_init(&player);
    printf("VS1053 initialized.\r\n");
    vs1053_set_volume(&player, 0x00, 0x00);
    printf("VS1053 volume set to max!\r\n");
    
    // Enable I2S output
    vs1053_enable_i2s(&player);
    printf("VS1053 I2S enabled.\r\n");

    // initialize DAC
    dac_init(i2c0);
    dac_interrupt_init();
    dac_interrupt_init();
    printf("DAC intialized.\r\n");

    printf("Audio init complete.\r\n");
    printf("\r\nScanning directory...\r\n");

    // --- Scan MP3 files ---
    DIR dir;
    FILINFO fno;
    track_info_t tracks[MAX_TRACKS];
    int count = 0;

    f_opendir(&dir, "0:/");

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (fno.fattrib & AM_DIR) continue;

        char *ext = strrchr(fno.fname, '.');
        if (ext && !strcasecmp(ext, ".mp3") && count < MAX_TRACKS) {
            get_mp3_metadata(fno.fname, &tracks[count]);
            count++;
        }
    }

    f_closedir(&dir);

    if (count == 0) {
        printf("No MP3 files found.\r\n");
        while (1);
    }

    qsort(tracks, count, sizeof(track_info_t), compare_filenames);

    while (1) {
        // --- Print menu ---
        printf("\r\nAvailable tracks:\r\n");
        for (int i = 0; i < count; i++) {
            printf("\r\n[%d] %s - %s\r\n", i + 1, tracks[i].artist, tracks[i].title);
            printf("     Album: %s\r\n", tracks[i].album);
        }

        char input[8];
        int choice = 0;

        while (choice < 1 || choice > count) {
            printf("\r\nSelect track (1-%d): ", count);

            int idx = 0;
            memset(input, 0, sizeof(input));

            while (1) {
                int c = getchar(); // blocking read
                if (c == '\r' || c == '\n') { // Enter pressed
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
                printf("Invalid. Try again.");
        }

        track_info_t *sel = &tracks[choice - 1];

        printf("\r\n\r\nNow playing:\r\n");
        printf("  Title : %s\r\n", sel->title);
        printf("  Artist: %s\r\n", sel->artist);
        printf("  Album : %s\r\n\r\n", sel->album);

        jukebox(&player, sel->filename);
        jukebox(&player, sel->filename);

        printf("\r\nPlayback finished.\r\n");
    };
}
