#include "sb_util.h"

#include "core1_entry.h"
#include "filehelper.h"
#include "firmware.h"

// SPI1 configuration for codec & sd card
    //TODO: Moved to vs1053.h
// #define PIN_SCK 30
// #define PIN_MOSI 28
// #define PIN_MISO 31
// #define PIN_CS 32

// TODO: Moved to sb_init (don't think this is global?)
// static FATFS fs;

// Codec control signals
#define PIN_DCS 33
#define PIN_DREQ 29
#define PIN_RST 27

// I2C0 for DAC
    // TODO: moved to dac.h
// #define PIN_I2C0_SCL 21
// #define PIN_I2C0_SDA 20

// Center at 0.65V (ADC is 12-bit, 0-3.3V)
    // TODO: Moved to adc.h
// #define ADC_CH 5
// #define ADC_CH_L 6
// #define ADC_CH_R 5
// Progress Bar
int progress_bar = 0;
int prev_progress_bar = 0;
uint16_t played_progres_color = 0xFFFF;
uint16_t background_progress_color = 0x0000;


uint16_t num_tracks = 0; // number of tracks in current directory
bool potCheck;
uint16_t frame_buffer[240 * 240];
uint16_t img_buffer[IMG_WIDTH * IMG_HEIGHT];
// static uint16_t column_buf[240];
//TODO: moved to sb_init.c
// static int dma_chan = -1;
// static dma_channel_config dcc;
pca9685_t vu_meter;
/*******************visualizations not scope*******************/
#define HISTORY_SIZE 256
cplx audio_history_l[HISTORY_SIZE];
cplx audio_history_r[HISTORY_SIZE];
int history_index = 0;
int visualizer = 1;
int num_visualizations = 6;
bool album_art_ready = false;
/*******************visualizations not scope*******************/

/* =========================================================
   PRIVATE HELPERS (static)
   ========================================================= */

static int jukebox(vs1053_t *player, track_info_t *track, st7789_t *display);


// TODO: Moved to core1_entry.c
// /* Text Display Stuff */
// mutex_t text_buff_mtx;
// semaphore_t text_sem;

// char text_buff_temp[120];
// struct Node *head = NULL;

// void printLL()
// {
//     struct Node *n = head;
//     while (n != NULL)
//     {
//         printf("%p: %s", n, n->str);
//         n = n->next;
//     }
// }

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

volatile uint16_t potVal = 0;

#define PAUSE_WARP_US 600000   // 0.7 seconds for pause
#define RESUME_WARP_US 1200000 // 1.2 seconds for resume
#define SKIP_INTERVAL_MS 100   // minimum interval between FF/RW jumps


uint16_t *playStatus = empty_icon;
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
    uint8_t vol_check = 10;
    uint8_t old_volume = 0;
    read_lwbt();
    while (1)
    {
        // janky counter for volume sampling
        if (vol_check == 10) {
            uint16_t vol = (uint32_t)potVal * 0x60 / 4096;

            // Only update DAC if the change is larger than the noise (hysteresis)
            if (abs((int)vol - (int)old_volume) >= 2) { 
                dac_set_volume(vol);
                old_volume = vol; // Only update "old" when the DAC actually changes
            }
            vol_check = 0;
        } else {
            vol_check++;
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
                        frame_buffer[y * 240 + x] = played_progres_color; // Played part
                    }
                    else
                    {
                        frame_buffer[y * 240 + x] = background_progress_color; // Remaining part
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
                if (paused) playStatus = pause_icon;
                else playStatus = play_icon;
                //draw pause ICON for text or album art visualizer
                if (visualizer == 0 || visualizer == 5)
                {
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
                // dac_decrease_volume(8);
                if (absolute_time_diff_us(last_skip_time, now) >= SKIP_INTERVAL_MS * 1000)
                {
                    pos += skip_bits;
                    if (pos > f_size(&fil))
                        pos = f_size(&fil) - 1;
                    f_lseek(&fil, pos);
                    printf("\r\nFast-forwarded ~2s\r\n");
                    last_skip_time = now;
                }
                // dac_increase_volume(8);
                break;
            case 'r':
            case 'R':
                // dac_decrease_volume(8);
                if (absolute_time_diff_us(last_skip_time, now) >= SKIP_INTERVAL_MS * 1000)
                {
                    pos -= skip_bits;
                    if (pos < 0)
                        pos = 0;
                    f_lseek(&fil, pos);
                    printf("\r\nRewound ~2s\r\n");
                    last_skip_time = now;
                }
                // dac_increase_volume(8);
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
            case 'l':
            case 'L':
                pca9685_sleep(&vu_meter, 1);
                printf("\r\nVU meter powered off.\r\n");
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
        dac_write(1, 0x20, 0b00000110); // shut down speaker driver
        // pause without warping
        paused = 1;
        warping = 0;
        // Reg 0x1F: HP Drivers power up
        dac_write(1, 0x1F, 0xC0);
        // Reg 0x28/0x29: HPL/R Driver unmute
        dac_write(1, 0x28, 0x06);
        dac_write(1, 0x29, 0x06);
        printf("Headphones plugged in! Paused and switching to stereo headphones.\n");
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
