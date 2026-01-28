#include "codec.h"

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

void get_mp3_metadata(const char *filename, track_info_t *track)
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

out:
    f_close(&fil);
}

bool paused = false;
bool fast_forward = false;
bool audio_rewind = false;
uint16_t normal_speed = 1; // 1 = normal
uint16_t ff_speed = 3;     // 3x speed

void play_file(vs1053_t *player, const char *filename)
{
    FIL fil;
    UINT br;
    uint8_t buffer[512];
    paused = false;

    absolute_time_t last_skip_time = get_absolute_time();

    if (f_open(&fil, filename, FA_READ) != FR_OK)
    {
        printf("Failed to open %s\r\n", filename);
        return;
    }

    // Jump straight to audio
    uint32_t start = find_audio_start(&fil);
    f_lseek(&fil, start);

    while (1)
    {
        int c = getchar_timeout_us(0); // non-blocking

        if (c != PICO_ERROR_TIMEOUT)
        {
            long pos = f_tell(&fil);
            absolute_time_t now = get_absolute_time();

            switch (c)
            {
            case 'p':
            case 'P':
                paused = !paused;
                printf("\r\n%s\r\n", paused ? "Paused" : "Resumed");
                vs1053_set_play_speed(player, paused ? 0 : normal_speed);
                break;

            case 'f':
            case 'F':
                if (absolute_time_diff_us(last_skip_time, now) >= SKIP_INTERVAL_MS * 1000)
                {
                    pos += 100 * 1024; // fast-forward
                    if (pos > f_size(&fil))
                        pos = f_size(&fil) - 1;
                    f_lseek(&fil, pos);
                    printf("\r\nFast-forwarded ~100KB\r\n");
                    last_skip_time = now;
                }
                break;

            case 'r':
            case 'R':
                if (absolute_time_diff_us(last_skip_time, now) >= SKIP_INTERVAL_MS * 1000)
                {
                    pos -= 100 * 1024; // rewind
                    if (pos < 0)
                        pos = 0;
                    f_lseek(&fil, pos);
                    printf("\r\nRewound ~100KB\r\n");
                    last_skip_time = now;
                }
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
            case 's':
            case 'S':
                printf("\r\nStopped. Returning to menu.\r\n");
                f_close(&fil);
                return; // Exit play_file immediately
            }
        }

        if (!paused)
        {
            if (f_read(&fil, buffer, sizeof(buffer), &br) != FR_OK || br == 0)
                break;

            vs1053_play_data(player, buffer, br);
        }
        else
        {
            sleep_ms(50);
        }
    }

    vs1053_set_play_speed(player, normal_speed);
    f_close(&fil);
}

uint8_t readReg(i2c_inst_t *i2c, uint8_t reg)
{
    uint8_t val;
    i2c_write_blocking(i2c, 0x18, &reg, 1, true);
    i2c_read_blocking(i2c, 0x18, &val, 1, false);
    return val;
}

// Helper for qsort
int compare_filenames(const void *a, const void *b)
{
    const track_info_t *ta = (const track_info_t *)a;
    const track_info_t *tb = (const track_info_t *)b;
    return strcasecmp(ta->filename, tb->filename);
}
