#include "sb_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "sd_card.h"
#include "hw_config.h"
#include "lib/dac/dac.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "pico/multicore.h"
// #include "scripting/output.h"
#include "lib/display/display.h"
#include "lib/display/picojpeg.h"
#include "lib/font/font.h"
#include "lib/led_driver/led_driver.h"

#include "lib/display/fft.h"
#include "lib/display/lissajous.h"
#include "filehelper.h"
#include "lib/buttons/buttons.h"

#include "lib/pot/pot.h"

#define MAX_FILENAME_LEN 256 // max filaname character length
#define MAX_TRACKS 128       // max number of mp3 files in sd card

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

// I2C1 for LED Driver
#define PIN_I2C1_SDA 42
#define PIN_I2C1_SCL 43

// Display and oscope stuff
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define BG_COLOR 0x0000   // Black

// Center at 0.65V (ADC is 12-bit, 0-3.3V)
#define ADC_CH 5

// Updated Constants for Split View
#define ADC_BIAS_CENTER 1551
#define ADC_RANGE_PKPK 1613
#define TARGET_HEIGHT 60 // Height of each individual wave (reduced to prevent overlap)

#define OFFSET_L 150 // Bottom half-ish
#define OFFSET_R 90  // Top half-ish

#define ADC_CH_L 6
#define ADC_CH_R 5

#define WAVE_L_COLOR 0x051C
#define WAVE_R_COLOR 0x0693
#define FFT_L_COLOR_DARK 0x0600
#define FFT_R_COLOR_DARK 0x05FF
#define FFT_L_COLOR_LIGHT 0x8FF1
#define FFT_R_COLOR_LIGHT 0xAFFF
#define IMG_WIDTH 160
#define IMG_HEIGHT 160

uint16_t num_tracks = 0; // number of tracks in current directory
bool potCheck;
uint16_t frame_buffer[240 * 240];
static uint16_t img_buffer[IMG_WIDTH * IMG_HEIGHT];
static uint16_t column_buf[240];
static int dma_chan = -1;
static dma_channel_config dcc;
pca9685_t vu_meter;
/*******************visualizations not scope*******************/
#define HISTORY_SIZE 256
cplx audio_history_l[HISTORY_SIZE];
cplx audio_history_r[HISTORY_SIZE];
int history_index = 0;
int visualizer = 1;
volatile bool loading_songs = false;
int num_visualizations = 6;
bool album_art_ready = false;
/*******************visualizations not scope*******************/

/* =========================================================
   PRIVATE HELPERS (static)
   ========================================================= */

static int jukebox(vs1053_t *player, track_info_t *track, st7789_t *display);

/* Text Display Stuff */
mutex_t text_buff_mtx;
semaphore_t text_sem;

char text_buff_temp[120];
struct Node *head = NULL;

void printLL()
{
    struct Node *n = head;
    while (n != NULL)
    {
        printf("%p: %s", n, n->str);
        n = n->next;
    }
}

/* Text Display Stuff */

/* =========================================================
   PUBLIC API
   ========================================================= */
void clear_framebuffer()
{
    mutex_enter_blocking(&text_buff_mtx);
    memset(frame_buffer, 0, sizeof(frame_buffer));
    mutex_exit(&text_buff_mtx);
}

void set_visualizer(int num)
{
    visualizer = num;
}

void pause_core1()
{
    loading_songs = true;
}
void resume_core1()
{
    loading_songs = false;
}

void set_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    frame_buffer[y * SCREEN_WIDTH + x] = color;
}

void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color)
{
    const struct Font *f = find_font_char(c);
    if (f == NULL)
        return;
    for (uint8_t row = 0; row < font_height; row++)
    {
        for (uint8_t col = 0; col < font_width; col++)
        {
            if (f->code[row * font_width + col] == 1)
            {
                set_pixel(x + col, y + row, color);
            }
            else
            {
                set_pixel(x + col, y + row, BLACK);
            }
        }
    }
}

void st7789_draw_string(uint16_t x, uint16_t y, const char *text, uint16_t color)
{
    uint16_t start_x = x;
    uint16_t start_y = y;

    for (int i = 0; text[i] != '\0' && i < 30; i++)
    {
        // if (text[i] == '\n' || text[i] == '\r') {
        //     start_x = x;
        //     start_y += 10;
        // }
        if (start_x < SCREEN_WIDTH - font_width && start_y < SCREEN_HEIGHT - font_height)
        {
            lcd_draw_char(start_x, start_y, text[i], color);
            start_x += font_width;
        }
        else
        {
            break;
        }
    }
}

void app_node(char *str)
{
    if (sem_available(&text_sem) >= 10)
    {
        return;
    }
    mutex_enter_blocking(&text_buff_mtx);

    struct Node *n = calloc(1, sizeof(struct Node));
    if (n == NULL)
    {
        printf("Error using calloc in app_node, freeing LL!");
        n = head;
        struct Node *prev = head;
        while (n != NULL)
        {
            prev = n;
            n = n->next;
            free(prev);
        }
        mutex_exit(&text_buff_mtx);
        // sleep_ms(100000);
        return;
    }

    n->next = NULL;
    strncpy(n->str, str, sizeof(n->str));

    if (head == NULL)
    {
        head = n;
    }
    else
    {
        struct Node *prev = head;
        while (prev->next != NULL)
        {
            prev = prev->next;
        }
        prev->next = n;
    }
    mutex_exit(&text_buff_mtx);
    sem_release(&text_sem);
    return;
}

void dprint(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsprintf(text_buff_temp, fmt, args);
    va_end(args);
    app_node(text_buff_temp);
#ifdef DEBUG
    printf("dprint: \'%s\' | strlen:%d sem_avail:%d\r\n", text_buff_temp, strlen(text_buff_temp), sem_available(&text_sem));
#endif
    return;
}

// Helper function to sample audio and update the LEDs
static void process_audio_batch()
{
    static int history_ptr = 0;
    uint16_t max_dev_l = 0;
    uint16_t max_dev_r = 0;

    for (int i = 0; i < 32; i++)
    {
        // Read Left ONCE
        adc_select_input(ADC_CH_L);
        uint16_t raw_l = adc_read();
        audio_history_l[history_ptr] = (cplx)raw_l;

        // Read Right ONCE
        adc_select_input(ADC_CH_R);
        uint16_t raw_r = adc_read();
        audio_history_r[history_ptr] = (cplx)raw_r;

        // calculate absolute deviation from the DC bias center //find a way to remove this, subtract
        uint16_t dev_l = abs((int)raw_l - ADC_BIAS_CENTER);
        uint16_t dev_r = abs((int)raw_r - ADC_BIAS_CENTER);

        // Onny take max value in each batch
        if (dev_l > max_dev_l)
            max_dev_l = dev_l;
        if (dev_r > max_dev_r)
            max_dev_r = dev_r;

        history_ptr = (history_ptr + 1) % HISTORY_SIZE;
        sleep_us(10);
    }

    // Re-add the bias so the VU meter math processes the peak correctly
    pca9685_update_vu(&vu_meter, ADC_BIAS_CENTER + max_dev_l, ADC_BIAS_CENTER + max_dev_r);
}

// This is the main loop for Core 1
void core1_entry()
{
    while (1)
    {
        switch (visualizer)
        {
        case 0: // Album Art
            if (album_art_ready)
            {
                // Draw art once
                album_art_centered();
                st7789_set_cursor(0, 0);
                st7789_ramwr();
                spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
                spi_write16_blocking(spi0, frame_buffer, 240 * 240);

                // Lock into an LED-only
                while (visualizer == 0)
                {
                    adc_select_input(ADC_CH_L);
                    uint16_t raw_l = adc_read();

                    adc_select_input(ADC_CH_R);
                    uint16_t raw_r = adc_read();

                    pca9685_update_vu(&vu_meter, raw_l, raw_r);
                    // sleep_ms(16); // Throttle to ~60FPS
                }
            }
            break;

        case 1: // Oscilloscope
            update_scope_core1();
            break;

        case 2: // FFT
            process_audio_batch();

            memset(frame_buffer, 0, sizeof(frame_buffer));
            draw_bins(60);

            st7789_set_cursor(0, 0);
            st7789_ramwr();
            spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
            spi_write16_blocking(spi0, frame_buffer, 240 * 240);
            break;

        case 3: // Lissajous
            process_audio_batch();
            draw_lissajous();
            break;

        case 4: // Lissajous connected
            process_audio_batch();
            draw_lissajous_connected();
            break;

        case 5:
            if (sem_acquire_timeout_ms(&text_sem, 10))
            {
                printf(" core1: aquired lock\r\n");

                memmove(&frame_buffer, &frame_buffer[SCREEN_WIDTH * (font_height)], sizeof(uint16_t) * (SCREEN_WIDTH) * (SCREEN_HEIGHT - font_height));
                memset(&frame_buffer[SCREEN_WIDTH * (SCREEN_HEIGHT - font_height)], 0, sizeof(uint16_t) * (SCREEN_WIDTH) * (font_height));
                mutex_enter_blocking(&text_buff_mtx);

                if (head == NULL)
                {
                    printf("Err! Core 1 head is NULL");
                    mutex_exit(&text_buff_mtx);
                    continue;
                }
                printf("core 1: %s | %d\r\n", head->str, strlen(text_buff_temp));
                st7789_draw_string(1, SCREEN_HEIGHT - font_height - 5, head->str, WHITE);
                struct Node *n = head;
                head = head->next;
                if (n != NULL)
                {
                    free(n);
                }
                mutex_exit(&text_buff_mtx);
                st7789_set_cursor(0, 0);
                st7789_ramwr();
                spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
                spi_write16_blocking(spi0, frame_buffer, 240 * 240);
                // sleep_ms(1000);
                printf(" core 1 finished print\r\n");
            }
            break;

        default:
            visualizer = 0;
            break;
        }
    }
}

void sb_display_init(st7789_t *display)
{
    st7789_init(display, SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("Display initialized!\r\n");

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
    // sleep_ms(500);

    multicore_launch_core1(core1_entry);
    printf("CORE 1 LAUNCHED!\r\n");
}

void sb_hw_init(vs1053_t *player, st7789_t *display)
{
    mutex_init(&text_buff_mtx);
    sem_init(&text_sem, 0, 255);

    // set SPI1 for codec and SD card
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);

    // set I2C0 for DAC at 400KHz
    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(PIN_I2C0_SCL, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C0_SDA, GPIO_FUNC_I2C);
    // gpio_pull_up(PIN_I2C0_SCL);
    // gpio_pull_up(PIN_I2C0_SDA);
    dprint("SPI0 and I2C0 initialized.");
    printf("SPI0 and I2C0 initialized.\r\n");

    // set I2C1 for PCA9685 at 400KHz
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(PIN_I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C1_SCL, GPIO_FUNC_I2C);
    // gpio_pull_up(PIN_I2C1_SDA);
    // gpio_pull_up(PIN_I2C1_SCL);
    printf("I2C1 initialized.\r\n");

    // LED driver init
    if (pca9685_init(&vu_meter, i2c1, 0x40))
    {
        printf("PCA9685 LED Driver initialized!\r\n");
    }
    else
    {
        printf("WARNING: PCA9685 Init Failed!\r\n");
    }

    if (!sd_init_driver())
    {
        while (1)
        {
            dprint("SD init failed");
            printf("SD init failed\r\n");
        }
    }
    else
    {
        dprint("SD card initialized!");
        printf("SD card initialized!\r\n");
    }

    FRESULT fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK)
    {
        while (1)
        {
            dprint("SD Mount failed: %d", fr);
            printf("SD Mount failed: %d\n", fr);
        }
    }
    else
    {
        dprint("SD card mounted!");
        printf("SD card mounted!\r\n");
    }

    adc_init();        // Inside sb_hw_init
    adc_gpio_init(46); // Left
    adc_gpio_init(45); // Right
    adc_select_input(ADC_CH);

    printf("Oscope ADC initialized!\r\n");
    dprint("Oscope ADC initialized!");

    sb_display_init(display);
    printf("test point 1");

    vs1053_init(player);
    printf("test point 2");

    printf("VS1053 initialized.\r\n");
    dprint("VS1053 initialized.");
    vs1053_set_volume(player, 0x01, 0x01); // chnged from 0 (0x00) to -12dB (0x0202) to -6dB (0x0101)
    printf("VS1053 volume set to max!\r\n");
    dprint("VS1053 volume set to max!");

    // Enable I2S output
    vs1053_enable_i2s(player);
    printf("VS1053 I2S enabled.\r\n");
    dprint("VS1053 I2S enabled.");

    // initialize DAC
    dac_init(i2c0);
    dac_interrupt_init();
    printf("DAC intialized.\r\n");
    dprint("DAC intialized.");

    printf("Audio init complete.\r\n");
    dprint("Audio init complete.");
    printf("\r\nScanning directory...\r\n");
    dprint("Scanning directory...");

    // Initialize buttons with a 10ms scan rate
    buttons_init(10);
    printf("\r\nButtons intializedr\n");

    pot_init();
    printf("\r\npot intialized\r\n");

    dprint("Finished sb_hw_init");
    printf("\r\nFinished sb_hw_init\r\n");
}

int sb_scan_tracks(track_info_t *tracks, int max_tracks)
{
    dprint("start of nsb_scan_tracks heartbeat");
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
            dprint("Read song %d", count);
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

    dprint("end of sb_scan_tracks heartbeat");
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

int sb_play_track(vs1053_t *player, track_info_t *track, st7789_t *display)
{
    album_art_ready = false;
    int exitCode = jukebox(player, track, display);
    return exitCode;
}

/* =========================================================
   COPY YOUR EXISTING FUNCTIONS BELOW
   (unchanged, just made static)
   ========================================================= */

void update_scope_core1()
{
    static int x = 0;
    static int last_y_l = OFFSET_L;
    static int last_y_r = OFFSET_R;
    static int led_throttle = 0;

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
        // ensures that LED do not use too many cycles
        //  if (led_throttle++ % 2 == 0)
        pca9685_update_vu(&vu_meter, raw_l, raw_r);
    }
}

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
    {
        return;
    }

    if (f_read(&fil, header, 10, &br) != FR_OK || br != 10)
    {
        goto out;
    }

    if (memcmp(header, "ID3", 3) != 0)
    {
        goto out;
    }

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
        {
            memset(img_buffer, 0, sizeof(img_buffer));
            goto out;
        }

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
                {
                    goto out;
                }
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


uint16_t *playStatus;
int jukebox(vs1053_t *player, track_info_t *track, st7789_t *display)
{
    FIL fil;             // file object
    UINT br;             // pointer to number of bytes read
    uint8_t buffer[512]; // buffer read from file

    char *filename = track->filename;
    uint16_t sampleSpeed = track->samplespeed;
    uint16_t bitRate = track->bitrate;
    uint32_t skip_bits = bitRate * 256; // bitrate * 1024 / 4 = approx. 2 seconds
    int exitType = 0;
    sci_write(player, 0x05, sampleSpeed + 1); // initialize codec sampling speed (+1 at the end for stereo)

    // status bits for player state and warp effect
    paused = false;
    warping = false;
    stopped = 0;

    // Progress Bar
    int progress_bar = 0;
    int prev_progress_bar = 0;

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
        return exitType;
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

    int selected_band = 0;
    int currEq = 0;
    dac_eq_init(sampleSpeed); // init with default sample rate
    uint8_t current_volume = 0;
    uint8_t smoothed_adc = 0;
    // This while loop continuously scans for key inputs while playing audio.
    // Warping is achieved by continuously sending audio bytes after pause point until warp duration is met.
    uint8_t old_volume;
    uint8_t vol_check = 0;
    read_lwbt();
    while (1)
    {
        // //read input
        if (vol_check < 30){
            vol_check = (vol_check + 1) % 31;
        }
        else {
            adc_select_input(POT_ADC_CHANNEL);
            uint16_t raw_adc = adc_read() * 0x60 / 4096;

            //moving average
            // smoothed_adc = ((smoothed_adc * 7) + raw_adc) / 8;

            // // Squares the ADC value to create an audio curve, then scales to MAX_DAC_VOL
            // uint32_t adc_squared = (uint32_t)smoothed_adc * smoothed_adc;
            // uint8_t new_volume = (uint8_t)((adc_squared * MAX_DAC_VOL) / (4095 * 4095));
            if (abs(raw_adc - old_volume) < 3) {
                dac_set_volume(raw_adc);
                // printf("pot vol %d\n\t", raw_adc);
            }
            old_volume = raw_adc;
        }
        // // Only send an I2C command to the DAC if the volume changed by >1 step.
        // if (abs(new_volume - current_volume) > 1) {
        //     current_volume = new_volume;
        //     dac_set_volume(current_volume);
        // }

        // --- 2. MUSIC FEEDING (Priority) ---
        // The rest of your jukebox logic remains here...
        int c = getchar_timeout_us(0); // nonblocking getchar

        // get value from buttons
        if (c == PICO_ERROR_TIMEOUT)
        {
            char btn_char = buttons_map_to_char_jukebox(selected_band);
            if (btn_char != 0)
                c = (int)btn_char; // Inject the button character into the logic
        }
        long song_pos = f_tell(&fil);
        float progress = (float)(song_pos - track->audio_start) / (float)(track->audio_end - track->audio_start);
        if (progress < 0.0f)
            progress = 0.0f;
        if (progress > 1.0f)
            progress = 1.0f;
        prev_progress_bar = progress_bar;
        progress_bar = 240 * progress;
        bool update_bar = prev_progress_bar != progress_bar;

        if (visualizer == 0 && update_bar)
        {
            // ST7789 uses 16-bit RGB565 colors
            uint16_t color_red = 0xF800;
            uint16_t color_gray = 0x8410; // Medium gray

            // Draw the bottom 10 rows (assuming screen height is 240)
            for (int y = 230; y < 240; y++)
            {
                for (int x = 0; x < 240; x++)
                {
                    if (x < progress_bar)
                    {
                        frame_buffer[y * 240 + x] = color_red; // Played part
                    }
                    else
                    {
                        frame_buffer[y * 240 + x] = color_gray; // Remaining part
                    }
                }
            }
            st7789_set_cursor(0, 0);
            st7789_ramwr();
            spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
            spi_write16_blocking(spi0, frame_buffer, 240 * 240);
        }

        if (c != PICO_ERROR_TIMEOUT)
        {
            long pos = f_tell(&fil);
            // bool headphonesIn = dac_read(0, 0x43) & 0x20;
            // printf("Headphone prescence: %d\r\n", headphonesIn);
            absolute_time_t now = get_absolute_time();

            // EQ START
            //  Select the band (keys 0-5)
            if (c >= '0' && c <= '5')
            {
                selected_band = c - '0';
                printf("\nSelected Band: %d Hz\n", dac_eq_get_freq(selected_band));
            }

            // Adjust the band (+ or -)
            if (c == '+' || c == '=')
            {
                dac_eq_adjust(selected_band, 0.5f, sampleSpeed); // Boost
                printf("Band %d Gain: %.1f dB\n", selected_band, dac_eq_get_gain(selected_band));
            }
            if (c == '-')
            {
                dac_eq_adjust(selected_band, -0.5f, sampleSpeed); // Cut
                printf("Band %d Gain: %.1f dB\n", selected_band, dac_eq_get_gain(selected_band));
            }
            // EQ END

            switch (c)
            {
            // new **
            case 'n':
            case 'N':
                exitType = 1;
                vs1053_set_play_speed(player, 0); // hard pause
                printf("\r\n Going to next song....\r\n");
                f_close(&fil);
                vs1053_stop(player);
                return exitType;
            case 'o':
            case 'O':
                exitType = 2;
                vs1053_set_play_speed(player, 0); // hard pause
                printf("\r\n Going to next song....\r\n");
                f_close(&fil);
                vs1053_stop(player);
                return exitType;
            // not new below
            case 'p':
            case 'P':
                paused = !paused;                      // set paused flag
                warp_start_time = get_absolute_time(); // get timestamp for warp start
                warp_start_transport = transport;      //
                warp_target = paused ? 0.0f : 1.0f;
                warping = true;
                //draw pause ICON for text or album art visualizer
                if (visualizer == 0 || visualizer == 5)
                {
                    if (paused)
                        playStatus = pause_icon;
                    else
                        playStatus = play_icon;
                    for (int y = 0; y < 20; y++)
                    {
                        uint16_t *dst = &frame_buffer[y * SCREEN_WIDTH];
                        uint16_t *src = &playStatus[y * 20];
                        memcpy(dst, src, 20 * sizeof(uint16_t));
                    }
                    st7789_set_cursor(0, 0);
                    st7789_ramwr();
                    spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
                    spi_write16_blocking(spi0, frame_buffer, 240 * 240);
                }

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
                visualizer = (visualizer + 1) % (num_visualizations - 1);
                if (visualizer == 0 && !album_art_ready && track->album_art_size > 0)
                {
                    process_image(track, filename, 160);
                    album_art_ready = true;
                    printf("changing visualizer");
                    if (paused)
                        playStatus = pause_icon;
                    else
                        playStatus = play_icon;

                    for (int y = 0; y < 20; y++)
                    {
                        uint16_t *dst = &frame_buffer[y * SCREEN_WIDTH];
                        uint16_t *src = &playStatus[y * 20];
                        memcpy(dst, src, 20 * sizeof(uint16_t));
                    }
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
                case 5:
                    dprint("Text Display");
                    printf("\r\nText Display\r\n");
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
                    exitType = 0;
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nStopping....\r\n");
                    f_close(&fil);
                    vs1053_stop(player);
                    return exitType;
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
                    exitType = 0;
                    vs1053_set_play_speed(player, 0); // hard pause
                    printf("\r\nStopping....\r\n");
                    f_close(&fil);
                    vs1053_stop(player);
                    return exitType;
                }
            }
        }

        // Always feed decoder unless fully paused
        if (!paused || warping)
        {
            if (f_read(&fil, buffer, sizeof(buffer), &br) != FR_OK || br == 0)
            {
                exitType = 1; // Default return when no bytes read (end of song)
                break;
            }

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
                    return 0;
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
    // exitType = 0; //plays next song if song just ends
    return exitType;
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
    // for (int y = 0; y < 20; y++)
    // {
    //     uint16_t *dst = &frame_buffer[y * SCREEN_WIDTH];
    //     uint16_t *src = &playStatus[y * 20];
    //     memcpy(dst, src, 20 * sizeof(uint16_t));
    // }
    // st7789_set_cursor(0, 0);
    // st7789_ramwr();
    // spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    // spi_write16_blocking(spi0, frame_buffer, 240 * 240);
}

// ---- Init GPIO interrupt ----
void dac_interrupt_init(void)
{
    gpio_init(3);
    gpio_set_dir(3, GPIO_IN);
    gpio_pull_up(3); // INT is usually open-drain

    gpio_set_irq_enabled_with_callback(
        3,
        GPIO_IRQ_EDGE_RISE, // active-low interrupt
        true,
        &dac_int_callback);
}
