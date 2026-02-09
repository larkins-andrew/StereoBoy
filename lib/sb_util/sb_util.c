#include "sb_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sd_card.h"
#include "hw_config.h"
#include "lib/dac/dac.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "../display/display.h"
#include "pico/multicore.h"

#define MAX_FILENAME_LEN 256 // max filaname character length
#define MAX_TRACKS 64        // max number of mp3 files in sd card

// SPI1 configuration for codec & sd card
#define PIN_SCK 30
#define PIN_MOSI 28
#define PIN_MISO 31
#define PIN_CS 32

static FATFS fs;

// Codec control signals
#define PIN_DCS 33
#define PIN_DREQ 29
#define PIN_RST 27

// I2C0 for DAC
#define PIN_I2C0_SCL 21
#define PIN_I2C0_SDA 20

// Display and oscope stuff
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define WAVE_COLOR 0x07E0 // Bright Green
#define BG_COLOR 0x0000   // Black

// Center at 0.65V (ADC is 12-bit, 0-3.3V)
// (0.65 / 3.3) * 4095 = 806
#define ADC_CENTER 806
#define ADC_CH 5

// Updated Constants for Split View
#define ADC_BIAS_CENTER 1551
#define ADC_RANGE_PKPK 1613
#define TARGET_HEIGHT 60 // Height of each individual wave (reduced to prevent overlap)

#define OFFSET_L 150 // Bottom half-ish
#define OFFSET_R 90  // Top half-ish

#define ADC_CH_L 5
#define ADC_CH_R 4

#define WAVE_L_COLOR 0x07E0
#define WAVE_R_COLOR 0x07FF
#define WAVE_L_COLOR_DARK 0x0600
#define WAVE_R_COLOR_DARK 0x05FF
#define WAVE_L_COLOR_LIGHT 0x8FF1
#define WAVE_R_COLOR_LIGHT 0xAFFF

static uint16_t frame_buffer[240 * 240];
static uint16_t column_buf[240];
static int dma_chan = -1;
static dma_channel_config dcc;

/*******************fft*******************/

#define HISTORY_SIZE 256
volatile cplx audio_history_l[HISTORY_SIZE];
volatile cplx audio_history_r[HISTORY_SIZE];
int history_index = 0;
// static const int bucket_limits[16] = {2, 3, 4, 6, 9, 13, 18, 25, 35, 48, 63, 80, 98, 110, 120, 128};
static const int bucket_limits[16] = {2, 3, 4, 5, 7, 10, 14, 19, 25, 32, 40, 49, 59, 70, 80, 90};
int visualizer = 1;
/*******************fft*******************/

/* =========================================================
   PRIVATE HELPERS (static)
   ========================================================= */

static uint32_t syncsafe_to_uint(const uint8_t *b);
static void read_text_frame(FIL *fil, uint32_t size, char *out, size_t out_size);
static uint32_t find_audio_start(FIL *fil);
static void get_mp3_header(FIL *fil, track_info_t *track);
static void get_mp3_metadata(const char *filename, track_info_t *track);
static int compare_filenames(const void *a, const void *b);
static void jukebox(vs1053_t *player, track_info_t *track, st7789_t *display);

/* =========================================================
   PUBLIC API
   ========================================================= */

// This is the main loop for Core 1
void core1_entry()
{
    adc_select_input(ADC_CH_L);
    const uint32_t sample_interval_us = 45;
    absolute_time_t next_sample_time = get_absolute_time();

    int last_visualizer_mode = -1;

    while (1)
    {
        // If we just switched modes, clear the buffer to prevent ghosting
        if (visualizer != last_visualizer_mode)
        {
            memset(frame_buffer, 0, sizeof(frame_buffer));
            last_visualizer_mode = visualizer;
        }

        if (visualizer == 1)
        {
            // SPECTRUM MODE
            // 1. Collect 256 samples at a specific cadence
            for (int i = 0; i < HISTORY_SIZE; i++)
            {
                // Wait until it's time for the next sample
                while (absolute_time_diff_us(get_absolute_time(), next_sample_time) > 0)
                {
                    tight_loop_contents();
                }

                adc_select_input(ADC_CH_L);
                uint16_t raw_l = adc_read();
                adc_select_input(ADC_CH_R);
                uint16_t raw_r = adc_read();

                audio_history_l[i] = (cplx)raw_l;
                audio_history_r[i] = (cplx)raw_r;

                next_sample_time = delayed_by_us(next_sample_time, sample_interval_us);
            }

            // 2. Process and Draw (Full frame)
            memset(frame_buffer, 0, sizeof(frame_buffer));
            get_bins();

            st7789_set_cursor(0, 0);
            st7789_ramwr();
            spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
            spi_write16_blocking(spi0, frame_buffer, 240 * 240);
        }
        else
        {
            // OSCILLOSCOPE MODE
            update_visualizer_core1();
        }
    }
}

void fast_drawline(int x, int y1, int y2, uint16_t color)
{
    if (y1 > y2)
    {
        int tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    for (int y = y1; y <= y2; y++)
    {
        frame_buffer[y * SCREEN_WIDTH + x] = color;
    }
}

void sb_hw_init(vs1053_t *player, st7789_t *display)
{
    if (!sd_init_driver())
    {
        while (1)
        {
            printf("SD init failed\r\n");
        }
    }
    else
    {
        printf("SD card initialized!\r\n");
    }

    FRESULT fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK)
    {
        while (1)
        {
            printf("SD Mount failed: %d\n", fr);
        }
    }
    else
    {
        printf("SD card mounted!\r\n");
    }

    st7789_init(display, SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("Display initialized!\r\n");

    multicore_launch_core1(core1_entry);
    printf("CORE 1 LAUNCHED!\r\n");

    adc_init();        // Inside sb_hw_init
    adc_gpio_init(45); // Left
    adc_gpio_init(44); // Right
    adc_select_input(ADC_CH);
    // Setup DMA for super-fast draw routines
    dma_chan = dma_claim_unused_channel(true);
    dcc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dcc, DMA_SIZE_16);
    channel_config_set_dreq(&dcc, spi_get_dreq(display->spi, true));
    // 1. Fill the entire buffer with zeros (Black) instantly
    // Each pixel is 2 bytes, so total size is 240 * 240 * 2
    memset(frame_buffer, 0, sizeof(frame_buffer));

    // 2. Set the display window to the full screen
    st7789_set_cursor(0, 0);
    st7789_ramwr();

    // 3. Ensure SPI is in 16-bit mode for the DMA transfer
    spi_set_format(display->spi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // 4. Use DMA to push the black buffer to the display
    // This returns almost immediately while the hardware does the work
    dma_channel_configure(
        dma_chan,
        &dcc,
        &spi_get_hw(display->spi)->dr, // Destination: SPI TX register
        frame_buffer,                  // Source: Our cleared RAM buffer
        240 * 240,                     // Count: Total number of 16-bit pixels
        true                           // Start now!
    );
    sleep_ms(500);
    printf("Oscope ADC and DMA initialized!\r\n");

    // initialize DAC
    dac_init(i2c0);
    dac_interrupt_init();
    printf("DAC intialized.\r\n");

    vs1053_init(player);
    printf("VS1053 initialized.\r\n");
    vs1053_set_volume(player, 0x00, 0x00);
    printf("VS1053 volume set to max!\r\n");

    // Enable I2S output
    vs1053_enable_i2s(player);
    printf("VS1053 I2S enabled.\r\n");

    printf("Audio init complete.\r\n");
    printf("\r\nScanning directory...\r\n");
}

int sb_scan_tracks(track_info_t *tracks, int max_tracks)
{
    DIR dir;
    FILINFO fno;
    int count = 0;

    f_opendir(&dir, "0:/");

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0])
    {
        if (fno.fattrib & AM_DIR)
            continue;

        char *ext = strrchr(fno.fname, '.');
        if (ext && !strcasecmp(ext, ".mp3") && count < MAX_TRACKS)
        {
            get_mp3_metadata(fno.fname, &tracks[count]);
            count++;
        }
    }

    f_closedir(&dir);

    if (count == 0)
    {
        printf("No MP3 files found.\r\n");
        while (1)
            ;
    }

    qsort(tracks, count, sizeof(track_info_t), compare_filenames);

    return count;
}

void sb_print_track(track_info_t *t)
{
    printf("\n%s - %s\n", t->artist, t->title);
    printf("Album: %s\n", t->album);
    printf("%d kbps  %d Hz  %s\n",
           t->bitrate,
           t->samplespeed,
           t->channels ? "Mono" : "Stereo");
}

void sb_play_track(vs1053_t *player, track_info_t *track, st7789_t *display)
{
    jukebox(player, track, display);
}

/* =========================================================
   COPY YOUR EXISTING FUNCTIONS BELOW
   (unchanged, just made static)
   ========================================================= */

// Convert syncsafe integer (ID3 size format)
static uint32_t syncsafe_to_uint(const uint8_t *b)
{
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3];
}

static void read_text_frame(FIL *fil, uint32_t frame_size, char *out, size_t out_size)
{
    UINT br;
    uint8_t encoding;

    f_read(fil, &encoding, 1, &br);
    frame_size--;

    memset(out, 0, out_size);

    // ---------------- UTF-8 ----------------
    if (encoding == 3)
    {
        uint32_t n = (frame_size < out_size - 1) ? frame_size : out_size - 1;
        f_read(fil, out, n, &br);
        out[n] = '\0';
        return;
    }

    // ---------------- ISO-8859-1 → UTF-8 ----------------
    if (encoding == 0)
    {
        uint8_t b;
        size_t oi = 0;

        for (uint32_t i = 0; i < frame_size && oi < out_size - 1; i++)
        {
            f_read(fil, &b, 1, &br);
            if (b < 0x80)
            {
                out[oi++] = b;
            }
            else
            {
                if (oi + 2 >= out_size)
                    break;
                out[oi++] = 0xC0 | (b >> 6);
                out[oi++] = 0x80 | (b & 0x3F);
            }
        }
        out[oi] = '\0';
        return;
    }

    // ---------------- UTF-16 → UTF-8 ----------------
    if (encoding == 1 || encoding == 2)
    {
        bool little_endian = true;
        uint8_t bom[2];

        // Read BOM if present
        f_read(fil, bom, 2, &br);
        frame_size -= 2;

        if (encoding == 1)
        {
            if (bom[0] == 0xFE && bom[1] == 0xFF)
                little_endian = false;
            else if (bom[0] == 0xFF && bom[1] == 0xFE)
                little_endian = true;
        }
        else
        {
            // UTF-16BE without BOM
            little_endian = false;
        }

        size_t oi = 0;
        for (uint32_t i = 0; i + 1 < frame_size && oi < out_size - 1; i += 2)
        {
            uint8_t b1, b2;
            f_read(fil, &b1, 1, &br);
            f_read(fil, &b2, 1, &br);

            uint16_t ch = little_endian ? (b1 | (b2 << 8)) : ((b1 << 8) | b2);
            if (ch == 0x0000)
                break;

            if (ch < 0x80)
            {
                out[oi++] = ch;
            }
            else if (ch < 0x800 && oi + 2 < out_size)
            {
                out[oi++] = 0xC0 | (ch >> 6);
                out[oi++] = 0x80 | (ch & 0x3F);
            }
            else if (oi + 3 < out_size)
            {
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

static uint32_t find_audio_start(FIL *fil)
{
    UINT br;
    uint8_t header[10];

    f_lseek(fil, 0);

    if (f_read(fil, header, 10, &br) != FR_OK || br != 10)
        return 0;

    if (memcmp(header, "ID3", 3) == 0)
    {
        uint32_t tag_size = syncsafe_to_uint(&header[6]);
        return 10 + tag_size;
    }

    // No ID3 tag → audio starts at 0
    return 0;
}

static void get_mp3_header(FIL *fil, track_info_t *track)
{
    UINT br;
    uint8_t header[4];

    // Find first frame header (0x7FF)
    while (1)
    {
        if (f_read(fil, &header[0], 1, &br) != FR_OK || br != 1)
            return;

        // First 8 sync bits must be all ones
        if (header[0] != 0xFF)
            continue;

        if (f_read(fil, &header[1], 1, &br) != FR_OK || br != 1)
            return;

        // Next 3 bits must also be ones (111xxxxx)
        if ((header[1] & 0xE0) != 0xE0)
        {
            // Not a real sync → rewind 1 byte so we don't skip potential syncs
            f_lseek(fil, f_tell(fil) - 1);
            continue;
        }

        // We now have a valid 11-bit sync → read remaining 2 bytes
        if (f_read(fil, &header[2], 2, &br) != FR_OK || br != 2)
            return;

        break; // Valid frame header found
    }

    track->header =
        ((uint32_t)header[0] << 24) |
        ((uint32_t)header[1] << 16) |
        ((uint32_t)header[2] << 8) |
        ((uint32_t)header[3]);

    uint8_t version_bits = (header[1] >> 3) & 0x03; // MPEG version
    uint8_t layer_bits = (header[1] >> 1) & 0x03;   // Layer
    uint8_t bitrate_bits = (header[2] >> 4) & 0x0F;
    uint8_t samplespeed_bits = (header[2] >> 2) & 0x03;
    uint8_t channel_bits = (header[3] >> 6) & 0x03;

    // MPEG version
    switch (version_bits)
    {
    case 0:
        track->mpegID = 2;
        break; // MPEG 2.5
    case 2:
        track->mpegID = 2;
        break; // MPEG 2
    case 3:
        track->mpegID = 1;
        break; // MPEG 1
    default:
        track->mpegID = 0;
        break; // reserved/unknown
    }

    // Sample rates table (Hz)
    const uint16_t samplespeeds[4][4] = {
        {11025, 12000, 8000, 0},  // MPEG 2.5  [00]
        {0, 0, 0, 0},             // reserved  [01]
        {22050, 24000, 16000, 0}, // MPEG 2    [10]
        {44100, 48000, 32000, 0}  // MPEG 1    [11]
    };

    track->samplespeed = samplespeeds[version_bits][samplespeed_bits];

    // Bitrate tables
    const uint16_t v1_bitrates[4][16] = {
        // Layer 0 (should never happen)
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        // V1 L3
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
        // V1 L2
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
        // V1 L1
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0}};

    const uint16_t v2_bitrates[4][16] = {
        // Layer 0 (should never happen)
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        // V2, L3
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        // V2, L2
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        // V2 L1
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0}};

    if (track->mpegID == 2)
    {
        track->bitrate = v2_bitrates[layer_bits][bitrate_bits]; // if MPEG Version 2
    }
    else if (track->mpegID == 1)
    {
        track->bitrate = v1_bitrates[layer_bits][bitrate_bits]; // if MPEG Version 1
    }
    else
    {
        track->bitrate = 0;
    }

    // Channels
    track->channels = (channel_bits >> 1) & 1; // 0 = stereo, 1 = mono
}

static void get_mp3_metadata(const char *filename, track_info_t *track)
{
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

    while (bytes_read < tag_size)
    {
        if (f_read(&fil, frame_header, 10, &br) != FR_OK || br != 10)
            break;

        bytes_read += 10;
        if (frame_header[0] == 0)
            break;

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
    get_mp3_header(&fil, track);

out:
    f_close(&fil);
}

// Helper for qsort
static int compare_filenames(const void *a, const void *b)
{
    const track_info_t *ta = (const track_info_t *)a;
    const track_info_t *tb = (const track_info_t *)b;
    return strcasecmp(ta->filename, tb->filename);
}

void update_visualizer_core1()
{
    static int x = 0;
    static int last_y_l = OFFSET_L;
    static int last_y_r = OFFSET_R;

    // 1. Sample Channels
    adc_select_input(ADC_CH_L);
    uint16_t raw_l = adc_read();
    adc_select_input(ADC_CH_R);
    uint16_t raw_r = adc_read();

    // 2. Map to Split Offsets
    // Left Channel centered at 150
    int dev_l = (int)raw_l - ADC_BIAS_CENTER;
    int y_l = OFFSET_L - (dev_l * TARGET_HEIGHT / ADC_RANGE_PKPK);

    // Right Channel centered at 90
    int dev_r = (int)raw_r - ADC_BIAS_CENTER;
    int y_r = OFFSET_R - (dev_r * TARGET_HEIGHT / ADC_RANGE_PKPK);

    // 3. Clamps (Keep them within their respective zones or full screen)
    if (y_l < 0)
        y_l = 0;
    if (y_l > 239)
        y_l = 239;
    if (y_r < 0)
        y_r = 0;
    if (y_r > 239)
        y_r = 239;

    // 4. Clear Column
    for (int i = 0; i < 240; i++)
    {
        frame_buffer[i * 240 + x] = BG_COLOR;
    }

    // 5. Draw Left (Green)
    int start_l = (y_l < last_y_l) ? y_l : last_y_l;
    int end_l = (y_l < last_y_l) ? last_y_l : y_l;
    for (int i = start_l; i <= end_l; i++)
    {
        frame_buffer[i * 240 + x] |= WAVE_L_COLOR;
    }

    // 6. Draw Right (Cyan)
    int start_r = (y_r < last_y_r) ? y_r : last_y_r;
    int end_r = (y_r < last_y_r) ? last_y_r : y_r;
    for (int i = start_r; i <= end_r; i++)
    {
        frame_buffer[i * 240 + x] |= WAVE_R_COLOR;
    }

    last_y_l = y_l;
    last_y_r = y_r;
    x++;

    // 7. Push to Display
    if (x >= 240)
    {
        x = 0;
        st7789_set_cursor(0, 0);
        st7789_ramwr();
        spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        spi_write16_blocking(spi0, frame_buffer, 240 * 240);
    }
}

/////////////////////////FFT///////////////////////

void bit_reverse(cplx buf[], int n)
{
    for (int i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            cplx temp = buf[i];
            buf[i] = buf[j];
            buf[j] = temp;
        }
    }
}

void fft_optimized(cplx buf[], int n)
{
    // 1. Reorder the array in bit-reversed order
    bit_reverse(buf, n);

    // 2. Butterfly computations
    for (int len = 2; len <= n; len <<= 1)
    {
        double ang = 2.0 * PI / len;
        cplx wlen = cos(ang) - I * sin(ang);
        for (int i = 0; i < n; i += len)
        {
            cplx w = 1.0 + 0.0 * I;
            for (int j = 0; j < len / 2; j++)
            {
                cplx u = buf[i + j];
                cplx v = buf[i + j + len / 2] * w;
                buf[i + j] = u + v;
                buf[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
void get_bins()
{
    float complex proc_l[HISTORY_SIZE];
    float complex proc_r[HISTORY_SIZE];
    static float display_l[16];
    static float display_r[16];

    // 1. Calculate DC Bias
    float avg_l = 0, avg_r = 0;
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        avg_l += (float)audio_history_l[i];
        avg_r += (float)audio_history_r[i];
    }
    avg_l /= HISTORY_SIZE;
    avg_r /= HISTORY_SIZE;

    // 2. Capture and Windowing
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        float mult = 0.5f * (1.0f - cosf(2.0f * PI * i / (HISTORY_SIZE - 1)));

        // Subtract bias AND normalize to 0.0 - 1.0 range (important for log math)
        proc_l[i] = ((float)audio_history_l[i] - avg_l) / 4096.0f * mult + 0.0f * I;
        proc_r[i] = ((float)audio_history_r[i] - avg_r) / 4096.0f * mult + 0.0f * I;
    }

    fft_optimized(proc_l, HISTORY_SIZE);
    fft_optimized(proc_r, HISTORY_SIZE);

    // 3. Process 16 Buckets
    int start_bin = 2; // START AT BIN 2: Skip DC (0) and very low rumble (1)
    for (int b = 0; b < 16; b++)
    {
        float max_mag_l = 0;
        float max_mag_r = 0;
        int end_bin = bucket_limits[b];

        // 1. PEAK DETECTION: Find the loudest bin in this bucket
        for (int i = start_bin; i <= end_bin; i++)
        {
            float cur_l = cabsf(proc_l[i]);
            float cur_r = cabsf(proc_r[i]);
            if (cur_l > max_mag_l)
                max_mag_l = cur_l;
            if (cur_r > max_mag_r)
                max_mag_r = cur_r;
        }
        start_bin = end_bin + 1;

        // 2. LOG SCALING (Normalized by FFT Size)
        float db_l = 20.0f * log10f((max_mag_l / HISTORY_SIZE) + 1e-9f);
        float db_r = 20.0f * log10f((max_mag_r / HISTORY_SIZE) + 1e-9f);

        // 3. TILT COMPENSATION
        // We add more "fake" volume the further right (higher index) we go
        float tilt = b * 1.5f; // Increase 2.0f if highs are still too low

        // 4. MAPPING TO SCREEN
        float target_l = (db_l + 95.0f) * 1.5f + tilt;
        float target_r = (db_r + 95.0f) * 1.5f + tilt;

        if (target_l < 0)
            target_l = 0;
        if (target_r < 0)
            target_r = 0;

        // 5. Fall/Decay
        if (target_l > display_l[b])
            display_l[b] = target_l;
        else
            display_l[b] -= 1.0f;

        if (target_r > display_r[b])
            display_r[b] = target_r;
        else
            display_r[b] -= 1.0f;

        // 6. Draw
        draw_spectrum_bars(b * 15, (int)display_l[b], (int)display_r[b], (int)target_l, (int)target_r);
    }
}
void draw_spectrum_bars(int x_start, int h_l, int h_r, int target_l, int target_r)
{
    const int BAR_WIDTH = 12;
    const int MAX_BAR_HEIGHT = 110; // Slightly less than half screen to leave a center gap

    if (h_l > MAX_BAR_HEIGHT)
        h_l = MAX_BAR_HEIGHT;
    if (h_r > MAX_BAR_HEIGHT)
        h_r = MAX_BAR_HEIGHT;
    if (target_l > MAX_BAR_HEIGHT)
        target_l = MAX_BAR_HEIGHT;
    if (target_r > MAX_BAR_HEIGHT)
        target_r = MAX_BAR_HEIGHT;

    for (int w = 0; w < BAR_WIDTH; w++)
    {
        int cur_x = x_start + w;
        if (cur_x >= SCREEN_WIDTH)
            break;

        // LEFT CHANNEL: Top of screen, drawing DOWNWARD
        for (int y = 0; y < h_l; y++)
        {
            frame_buffer[y * SCREEN_WIDTH + cur_x] = y > target_l ? WAVE_L_COLOR_LIGHT: WAVE_L_COLOR_DARK;
        }

        // RIGHT CHANNEL: Bottom of screen, drawing UPWARD
        for (int y = 0; y < h_r; y++)
        {
            frame_buffer[(239 - y) * SCREEN_WIDTH + cur_x] = y > target_r ? WAVE_R_COLOR_LIGHT : WAVE_R_COLOR_DARK;
        }
    }
}
///////////////////////FFT////////////////////////////////

/* ##########################################################
JUKEBOX: MAIN PLAY LOOP
########################################################## */

bool paused = false;
bool warping = false;
bool stopped = false;
bool fast_forward = false;
bool audio_rewind = false;
uint16_t normal_speed = 1; // 1 = normal

#define PAUSE_WARP_US 600000   // 0.7 seconds for pause
#define RESUME_WARP_US 1200000 // 1.2 seconds for resume
#define SKIP_INTERVAL_MS 100   // minimum interval between FF/RW jumps

void jukebox(vs1053_t *player, track_info_t *track, st7789_t *display)
{
    FIL fil;             // file object
    UINT br;             // pointer to number of bytes read
    uint8_t buffer[512]; // buffer read from file

    char *filename = track->filename;
    uint16_t sampleSpeed = track->samplespeed;
    uint16_t bitRate = track->bitrate;
    uint32_t skip_bits = bitRate * 256; // bitrate * 1024 / 4 = approx. 2 seconds

    sci_write(player, 0x05, sampleSpeed + 1); // initialize codec sampling speed (+1 at the end for stereo)

    // status bits for player state and warp effect
    paused = false;
    warping = false;
    stopped = 0;

    // more warp effect stuff
    float transport = 1.0f;                  // desired speed
    float warp_start_transport = 1.0f;       // start speed for warp
    float warp_target = 1.0f;                // target speed for warp
    uint32_t warp_duration = RESUME_WARP_US; // warp effect duration
    absolute_time_t warp_start_time;

    // open selected MP3 file
    if (f_open(&fil, filename, FA_READ) != FR_OK)
    {
        printf("Failed to open %s\r\n", filename);
        return;
    }

    uint16_t stereo_bit = sampleSpeed & 1;     // LSB indicates mono or stereo (not exactly sure what but this is pretty much always 1)
    uint16_t base_rate = sampleSpeed & 0xFFFE; // sampling speed in upper 15 bits

    uint32_t start = find_audio_start(&fil);
    f_lseek(&fil, start);
    absolute_time_t last_skip_time = get_absolute_time();

    // This while loop continuously scans for key inputs while playing audio.
    // Warping is achieved by continuously sending audio bytes after pause point until warp duration is met.
    while (1)
    {

        // --- 2. MUSIC FEEDING (Priority) ---
        // The rest of your jukebox logic remains here...
        int c = getchar_timeout_us(0); // nonblocking getchar
        if (c != PICO_ERROR_TIMEOUT)
        {
            long pos = f_tell(&fil);
            // bool headphonesIn = dac_read(0, 0x43) & 0x20;
            // printf("Headphone prescence: %d\r\n", headphonesIn);
            absolute_time_t now = get_absolute_time();
            switch (c)
            {
            case 'p':
            case 'P':
                paused = !paused;                      // set paused flag
                warp_start_time = get_absolute_time(); // get timestamp for warp start
                warp_start_transport = transport;      //
                warp_target = paused ? 0.0f : 1.0f;
                warping = true;

                // select duration based on pause/resume
                warp_duration = paused ? PAUSE_WARP_US : RESUME_WARP_US;

                printf(paused ? "\r\nTape slowing...\r\n"
                              : "\r\nTape resuming...\r\n");
                break;
            case 'f':
            case 'F':
                dac_decrease_volume(8);
                if (absolute_time_diff_us(last_skip_time, now) >= SKIP_INTERVAL_MS * 1000)
                {
                    pos += skip_bits;
                    if (pos > f_size(&fil))
                        pos = f_size(&fil) - 1;
                    f_lseek(&fil, pos);
                    printf("\r\nFast-forwarded ~2s\r\n");
                    last_skip_time = now;
                }
                dac_increase_volume(8);
                break;
            case 'r':
            case 'R':
                dac_decrease_volume(8);
                if (absolute_time_diff_us(last_skip_time, now) >= SKIP_INTERVAL_MS * 1000)
                {
                    pos -= skip_bits;
                    if (pos < 0)
                        pos = 0;
                    f_lseek(&fil, pos);
                    printf("\r\nRewound ~2s\r\n");
                    last_skip_time = now;
                }
                dac_increase_volume(8);
                break;
            case 'u':
            case 'U':
                dac_increase_volume(3);
                printf("\r\nVolume up!\r\n");
                break;
            case 'd':
            case 'D':
                dac_decrease_volume(3);
                printf("\r\nVolume down!\r\n");
                break;
            case 'i':
            case 'I':
                printf("\r\n\rNOW PLAYING:\r\n");
                printf("  Title : %s\r\n", track->title);
                printf("  Artist: %s\r\n", track->artist);
                printf("  Album : %s\r\n", track->album);
                printf("  Bitrate : %d Kbps\r\n", track->bitrate);
                printf("  Sample rate : %d Hz\r\n", track->samplespeed);
                printf("  Channels : %s\r\n", track->channels == 1 ? "Mono" : "Stereo");
                printf("  Header: %X\r\n", track->header);
                break;
            case 's':
            case 'S':
                if (paused)
                {
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
                if (paused)
                {
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nStopping....\r\n");
                    f_close(&fil);
                    vs1053_stop(player);
                    return;
                }
            }
        }

        // Always feed decoder unless fully paused
        if (!paused || warping)
        {
            if (f_read(&fil, buffer, sizeof(buffer), &br) != FR_OK || br == 0)
                break;

            vs1053_play_data(player, buffer, br);
        }

        // --- Warp logic ---
        if (warping)
        {
            int64_t elapsed = absolute_time_diff_us(warp_start_time, get_absolute_time());

            if (elapsed >= warp_duration)
            {
                transport = warp_target;
                warping = false;

                if (paused)
                {
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nPaused.\r\n");
                }
                else if (stopped)
                {
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nPaused.\r\n");
                    f_close(&fil);
                    vs1053_stop(player);
                    return;
                }
            }
            else
            {
                float t = (float)elapsed / (float)warp_duration;
                transport = warp_start_transport +
                            (warp_target - warp_start_transport) * t;
            }
        }

        // --- Apply playback rate + volume ---
        if (!paused || warping)
        {
            uint16_t new_rate = (uint16_t)(base_rate * transport) & 0xFFFE;
            if (new_rate < 9000)
                new_rate = 9000;
            sci_write(player, 0x05, new_rate | stereo_bit);

            // volume scales with transport
            // uint8_t vol = (uint8_t)(0xFE * (1.0f - transport));
            // if (vol > 0xFE) vol = 0xFE;
            // vs1053_set_volume(player, vol, vol);
        }
    }

    f_close(&fil);
}

// Headphones disconnect interrupt
void dac_int_callback(uint gpio, uint32_t events)
{
    // Read 0x2C to clear the sticky interrupt
    dac_read(0, 0x2C); // THIS NEEDS TO BE HERE!!!! DO NOT REMOVE THIS LINE
    // read whether headphone in or out
    if (dac_read(0, 0x2E) & 0x10)
    { // Bit 5
        printf("Headphones plugged in! Paused and switching to stereo headphones.\n");
        dac_write(1, 0x20, 0b00000110); // shut down speaker driver
        // pause without warping
        paused = 1;
        warping = 0;
    }
    else
    {
        printf("Headphones pulled out! Paused and switching to mono speakers.\n");
        dac_write(1, 0x20, 0b10000110); // power up speaker driver
        // pause without warping
        paused = 1;
        warping = 0;
    }
}

// ---- Init GPIO interrupt ----
void dac_interrupt_init(void)
{
    gpio_init(15);
    gpio_set_dir(15, GPIO_IN);
    gpio_pull_up(15); // INT is usually open-drain

    gpio_set_irq_enabled_with_callback(
        15,
        GPIO_IRQ_EDGE_RISE, // active-low interrupt
        true,
        &dac_int_callback);
}
