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
#include "../display/picojpeg.h"
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
#define FFT_L_COLOR_DARK 0x0600
#define FFT_R_COLOR_DARK 0x05FF
#define FFT_L_COLOR_LIGHT 0x8FF1
#define FFT_R_COLOR_LIGHT 0xAFFF
#define IMG_WIDTH 160
#define IMG_HEIGHT 160


static uint16_t frame_buffer[240 * 240];
static uint16_t img_buffer[IMG_WIDTH * IMG_HEIGHT];
static uint16_t column_buf[240];
static int dma_chan = -1;
static dma_channel_config dcc;

/*******************visualizations not scope*******************/

#define HISTORY_SIZE 256
volatile cplx audio_history_l[HISTORY_SIZE];
volatile cplx audio_history_r[HISTORY_SIZE];
int history_index = 0;
int visualizer = 0;
int num_visualizations = 5;
volatile bool album_art_ready = false;
/*******************visualizations not scope*******************/

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
    static int history_ptr = 0;

    while (1)
    {
        switch (visualizer)
        {
        case 2:
        case 3:
        case 4:
            // 1. CONTINUOUS SAMPLE: Fill the buffer over time
            // To balance bass and treble, we want about 22kHz sampling
            for (int i = 0; i < 32; i++)
            {
                adc_select_input(ADC_CH_L);
                audio_history_l[history_ptr] = (cplx)adc_read();
                adc_select_input(ADC_CH_R);
                audio_history_r[history_ptr] = (cplx)adc_read();

                history_ptr = (history_ptr + 1) % HISTORY_SIZE;
                sleep_us(20);
            }

            if (visualizer == 2)
            {
                draw_lissajous_connected();
            }
            else if (visualizer == 3)
            {
                draw_lissajous();
            }
            else
            {
                memset(frame_buffer, 0, sizeof(frame_buffer));
                draw_bins(60);

                st7789_set_cursor(0, 0);
                st7789_ramwr();
                spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
                spi_write16_blocking(spi0, frame_buffer, 240 * 240);
            }
            break;
        case 1:
            update_visualizer_core1();
            break;
        default:
            if (album_art_ready)
            {
                album_art_centered();

                st7789_set_cursor(0, 0);
                st7789_ramwr();
                spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
                spi_write16_blocking(spi0, frame_buffer, 240 * 240);
            }
            break;
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
    album_art_ready = false;
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
    strcpy(track->mime_type, "unknown");
    track->album_art_size = 0;
    track->album_art_offset = 0;
    track->album_art_type = 0;

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
        {
            read_text_frame(&fil, size, track->title, sizeof(track->title));
        }
        else if (!strcmp(id, "TPE1"))
        {
            read_text_frame(&fil, size, track->artist, sizeof(track->artist));
        }
        else if (!strcmp(id, "TALB"))
        {
            read_text_frame(&fil, size, track->album, sizeof(track->album));
        }
        else if (!strcmp(id, "APIC"))
        {
            // Record exactly where the 10-byte header ended
            FSIZE_t frame_start_pos = f_tell(&fil);
            UINT br;
            uint8_t encoding;

            // 1. Read Text Encoding (1 byte)
            f_read(&fil, &encoding, 1, &br);

            // 2. Read MIME Type (null-terminated string)
            char temp_mime[32];
            int i = 0;
            do
            {
                f_read(&fil, &temp_mime[i], 1, &br);
            } while (temp_mime[i++] != '\0' && i < 31);

            // 3. Read Picture Type (1 byte)
            uint8_t pic_type;
            f_read(&fil, &pic_type, 1, &br);

            // FILTER: Only process if it's the Front Cover (0x03) or if we haven't found one yet
            if (pic_type == 0x03 || track->album_art_offset == 0)
            {
                track->album_art_type = pic_type;
                strncpy(track->mime_type, temp_mime, sizeof(track->mime_type));

                // 4. Skip Description (null-terminated string)
                char dummy;
                if (encoding == 0)
                { // ISO-8859-1 (Single null)
                    do
                    {
                        f_read(&fil, &dummy, 1, &br);
                    } while (dummy != '\0');
                }
                else
                { // UTF-16 (Double null)
                    uint16_t dummy16;
                    do
                    {
                        f_read(&fil, &dummy16, 2, &br);
                    } while (dummy16 != 0x0000);
                }

                // 5. STORE THE DATA JUMP POINT
                // We are now at the first byte of the JPEG/PNG data
                track->album_art_offset = f_tell(&fil);

                // 6. CALCULATE TRUE IMAGE SIZE
                // Total frame size minus everything we just read/skipped
                track->album_art_size = size - (track->album_art_offset - frame_start_pos);
            }

            // 7. ALWAYS seek to the end of the frame to continue parsing other ID3 tags
            f_lseek(&fil, frame_start_pos + size);
        }
        else
        {
            f_lseek(&fil, f_tell(&fil) + size);
        }

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

void draw_bins(int n)
{
    if (n <= 0)
        n = 1;
    float complex proc_l[HISTORY_SIZE], proc_r[HISTORY_SIZE];
    static float display_l[64], display_r[64]; // Support up to 64 bars

    // 1. DC Removal & Windowing (Same as before)
    float avg_l = 0, avg_r = 0;
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        avg_l += (float)audio_history_l[i];
        avg_r += (float)audio_history_r[i];
    }
    avg_l /= HISTORY_SIZE;
    avg_r /= HISTORY_SIZE;

    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        float mult = 0.5f * (1.0f - cosf(2.0f * PI * i / (HISTORY_SIZE - 1)));
        proc_l[i] = ((float)audio_history_l[i] - avg_l) * mult + 0.0f * I;
        proc_r[i] = ((float)audio_history_r[i] - avg_r) * mult + 0.0f * I;
    }

    fft_optimized(proc_l, HISTORY_SIZE);
    fft_optimized(proc_r, HISTORY_SIZE);

    // 2. Dynamic Logarithmic Bucketing
    int last_bin = 2; // Start skipping DC
    int max_bin = HISTORY_SIZE / 2;

    for (int b = 0; b < n; b++)
    {
        // Calculate end_bin using an exponential curve
        // This ensures the frequency range of each bar grows as we go higher
        float ratio = (float)(b + 1) / n;
        int end_bin = (int)(2 + (max_bin - 2) * powf(ratio, 2.5f));

        if (end_bin <= last_bin)
            end_bin = last_bin + 1;
        if (end_bin >= max_bin)
            end_bin = max_bin - 1;

        float peak_l = 0, peak_r = 0;
        for (int i = last_bin; i <= end_bin; i++)
        {
            float m_l = cabsf(proc_l[i]) / HISTORY_SIZE;
            float m_r = cabsf(proc_r[i]) / HISTORY_SIZE;
            if (m_l > peak_l)
                peak_l = m_l;
            if (m_r > peak_r)
                peak_r = m_r;
        }

        // Adaptive Gate & Scaling
        float adaptive_gate = 6.0f - (ratio * 4.0f);
        if (adaptive_gate < 1.0f)
            adaptive_gate = 1.0f;

        float sens = 50.0f + (ratio * 100.0f);
        float target_l = 0, target_r = 0;

        if (peak_l > adaptive_gate)
            target_l = (log10f(peak_l + 1e-9f) - log10f(adaptive_gate)) * sens;
        if (peak_r > adaptive_gate)
            target_r = (log10f(peak_r + 1e-9f) - log10f(adaptive_gate)) * sens;

        // Smoothing
        float decay = 4.0f - (ratio * 2.0f);
        if (target_l > display_l[b])
            display_l[b] = target_l;
        else
            display_l[b] -= decay;
        if (target_r > display_r[b])
            display_r[b] = target_r;
        else
            display_r[b] -= decay;

        if (display_l[b] < 0)
            display_l[b] = 0;
        if (display_r[b] < 0)
            display_r[b] = 0;

        // 3. Dynamic Screen Positioning
        int total_width = SCREEN_WIDTH;
        int bar_plus_gap = total_width / n;
        int bar_width = (int)(bar_plus_gap * 0.8f); // 80% bar, 20% gap
        if (bar_width < 1)
            bar_width = 1;

        int x_pos = b * bar_plus_gap;

        draw_spectrum_bars(x_pos, bar_width, (int)display_l[b], (int)display_r[b], (int)target_l, (int)target_r);

        last_bin = end_bin + 1;
    }
}

void draw_spectrum_bars(int x_start, int width, int h_l, int h_r, int target_l, int target_r)
{
    const int MAX_BAR_HEIGHT = 110;

    if (h_l > MAX_BAR_HEIGHT)
        h_l = MAX_BAR_HEIGHT;
    if (h_r > MAX_BAR_HEIGHT)
        h_r = MAX_BAR_HEIGHT;

    for (int w = 0; w < width; w++)
    {
        int cur_x = x_start + w;
        if (cur_x >= SCREEN_WIDTH)
            break;

        // LEFT CHANNEL: Top half
        for (int y = 0; y < h_l; y++)
        {
            frame_buffer[(120 + y) * SCREEN_WIDTH + cur_x] = y > target_l ? FFT_L_COLOR_LIGHT : FFT_L_COLOR_DARK;
        }

        // RIGHT CHANNEL: Bottom half
        for (int y = 0; y < h_r; y++)
        {
            frame_buffer[(120 - y) * SCREEN_WIDTH + cur_x] = y > target_r ? FFT_R_COLOR_LIGHT : FFT_R_COLOR_DARK;
        }
    }
}

///////////////////////FFT////////////////////////////////

////////////////LISSAJOUS///////////////////////////

uint16_t dim_pixel(uint16_t color, uint16_t divide)
{
    return color / divide;
}

void draw_line_hot(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        if (x0 >= 0 && x0 < 240 && y0 >= 0 && y0 < 240)
        {
            frame_buffer[y0 * 240 + x0] = color;
        }
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_lissajous()
{
    // 1. Instead of clearing to black, "fade" the previous frame
    // This creates the phosphor trail effect
    for (int i = 0; i < (SCREEN_WIDTH * SCREEN_HEIGHT); i++)
    {
        if (frame_buffer[i] != 0)
        {
            frame_buffer[i] = dim_pixel(frame_buffer[i], 2);
        }
    }

    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        float val_l = crealf(audio_history_l[i]);
        float val_r = crealf(audio_history_r[i]);

        // 2. Map and Scale (Same as before)
        int x = (int)(((val_l - ADC_BIAS_CENTER) * 110.0f) / (ADC_RANGE_PKPK / 2.0f)) + 120;
        int y = (int)(((val_r - ADC_BIAS_CENTER) * 110.0f) / (ADC_RANGE_PKPK / 2.0f)) + 120;

        // 3. Clamping
        if (x < 0)
            x = 0;
        if (x > 239)
            x = 239;
        if (y < 0)
            y = 0;
        if (y > 239)
            y = 239;

        // 4. Draw the new sample with FULL brightness
        // Using WAVE_L_COLOR (0x07E0)
        frame_buffer[y * SCREEN_WIDTH + x] = 0xFFFF;
        frame_buffer[y * SCREEN_WIDTH + (239 - x)] = 0xFFFF;
        frame_buffer[(239 - y) * SCREEN_WIDTH + x] = 0xFFFF;
        frame_buffer[(239 - y) * SCREEN_WIDTH + (239 - x)] = 0xFFFF;
    }

    // 5. Push to Display
    st7789_set_cursor(0, 0);
    st7789_ramwr();
    spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_write16_blocking(spi0, frame_buffer, 240 * 240);
}

void draw_lissajous_connected()
{
    for (int i = 0; i < (SCREEN_WIDTH * SCREEN_HEIGHT); i++)
    {
        if (frame_buffer[i] != 0)
        {
            frame_buffer[i] = dim_pixel(frame_buffer[i], 2);
        }
    }

    int last_x = -1, last_y = -1;

    // 2. Connect the dots in the audio history
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        float val_l = crealf(audio_history_l[i]);
        float val_r = crealf(audio_history_r[i]);

        // Map to screen coordinates
        int x = (int)(((val_l - ADC_BIAS_CENTER) * 110.0f) / (ADC_RANGE_PKPK / 2.0f)) + 120;
        int y = (int)(((val_r - ADC_BIAS_CENTER) * 110.0f) / (ADC_RANGE_PKPK / 2.0f)) + 120;

        // If we have a previous point, draw a line to the current one
        if (last_x != -1)
        {
            draw_line_hot(last_x, last_y, x, y, 0xFFFF); // Hot White Line
        }

        last_x = x;
        last_y = y;
    }

    st7789_set_cursor(0, 0);
    st7789_ramwr();
    spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_write16_blocking(spi0, frame_buffer, 240 * 240);
}

////////////////////LISSAJOUS////////////////////////////

////////////////////IMAGE////////////////////////////

typedef struct
{
    FIL *fil;
    uint32_t bytes_left;
} jpeg_stream_t;

unsigned char jpeg_need_bytes_callback(
    unsigned char *pBuf,
    unsigned char buf_size,
    unsigned char *pBytes_actually_read,
    void *pCallback_data)
{
    jpeg_stream_t *ctx = (jpeg_stream_t *)pCallback_data;

    if (ctx->bytes_left == 0)
    {
        *pBytes_actually_read = 0;
        return 0; // EOF is OK for picojpeg
    }

    UINT to_read = buf_size;
    if (to_read > ctx->bytes_left)
        to_read = ctx->bytes_left;

    UINT br;
    if (f_read(ctx->fil, pBuf, to_read, &br) != FR_OK)
        return PJPG_STREAM_READ_ERROR;

    *pBytes_actually_read = br;
    ctx->bytes_left -= br;

    return 0;
}

void album_art_centered(void)
{
    // Clear screen first (black borders)
    memset(frame_buffer, 0, sizeof(frame_buffer));

    const int offset = (SCREEN_WIDTH - 160) / 2;

    for (int y = 0; y < 160; y++)
    {
        uint16_t *dst = &frame_buffer[(y + offset) * SCREEN_WIDTH + offset];
        uint16_t *src = &img_buffer[y * 160];

        memcpy(dst, src, 160 * sizeof(uint16_t));
    }
}

void process_image(track_info_t *track, const char *filename, float output_size)
{
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

    f_lseek(&fil, track->album_art_offset);
    if (strcmp(track->mime_type, "image/jpeg") == 0)
    {
        pjpeg_image_info_t jpeg_info;

        jpeg_stream_t stream = {
            .fil = &fil,
            .bytes_left = track->album_art_size};

        unsigned char status =
            pjpeg_decode_init(&jpeg_info, jpeg_need_bytes_callback, &stream, 0);

        if (status)
            goto out;

        float scale_x = (float)jpeg_info.m_width / output_size;
        float scale_y = (float)jpeg_info.m_height / output_size;

        for (uint16_t my = 0; my < jpeg_info.m_MCUSPerCol; my++)
        {
            for (uint16_t mx = 0; mx < jpeg_info.m_MCUSPerRow; mx++)
            {
                status = pjpeg_decode_mcu();

                if (status == PJPG_NO_MORE_BLOCKS)
                {
                    break;
                }
                if (status)
                    goto out;

                for (uint16_t ly = 0; ly < jpeg_info.m_MCUHeight; ly++)
                {
                    for (uint16_t lx = 0; lx < jpeg_info.m_MCUWidth; lx++)
                    {
                        uint16_t src_x = mx * jpeg_info.m_MCUWidth + lx;
                        uint16_t src_y = my * jpeg_info.m_MCUHeight + ly;

                        uint16_t dst_x = src_x / scale_x;
                        uint16_t dst_y = src_y / scale_y;

                        if (dst_x >= output_size || dst_y >= output_size)
                            continue;

                        uint16_t idx = ly * jpeg_info.m_MCUWidth + lx;

                        uint8_t r = jpeg_info.m_pMCUBufR[idx];
                        uint8_t g = jpeg_info.m_pMCUBufG[idx];
                        uint8_t b = jpeg_info.m_pMCUBufB[idx];

                        uint16_t rgb565 =
                            ((r & 0xF8) << 8) |
                            ((g & 0xFC) << 3) |
                            (b >> 3);

                        img_buffer[dst_y * IMG_WIDTH + dst_x] = rgb565;
                    }
                }
            }
            if (status == PJPG_NO_MORE_BLOCKS)
            {
                break;
            }
        }
    }
out:
    f_close(&fil);
}

////////////////////IMAGE////////////////////////////

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
    album_art_ready = false;
    if (track->album_art_size > 0 && visualizer == 0)
    {
        process_image(track, filename, 160); // fills frame_buffer
        album_art_ready = true;
    }
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
            case 'v':
            case 'V':
                visualizer = (visualizer + 1) % num_visualizations;
                if (visualizer == 0 && !album_art_ready && track->album_art_size > 0)
                {
                    process_image(track, filename, 160);
                    album_art_ready = true;
                }
                switch (visualizer)
                {
                case 0:
                    printf("\r\nAlbum Art Visualization\r\n");
                    break;
                case 1:
                    printf("\r\nScope Visualization\r\n");
                    break;
                case 2:
                    printf("\r\nSpectrum Analyzer Visualization\r\n");
                    break;
                case 3:
                    printf("\r\nLissajous Visualization\r\n");
                    break;
                case 4:
                    printf("\r\nMandala Visualization\r\n");
                    break;
                }
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
                printf("  Album Art Size: %lu\r\n", (unsigned long)track->album_art_size);
                printf("  Mime Type: %s\r\n", track->mime_type);
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
                album_art_ready = false;
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
