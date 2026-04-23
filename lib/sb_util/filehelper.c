#include "lib/sb_util/sb_util.h"
// Convert syncsafe integer (ID3 size format)

uint32_t syncsafe_to_uint(const uint8_t *b)
{
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3];
}

void read_text_frame(FIL *fil, uint32_t frame_size, char *out, size_t out_size)
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

uint32_t find_audio_start(FIL *fil)
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

void get_mp3_header(FIL *fil, track_info_t *track)
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

        track->audio_start = f_tell(fil) - 4;

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

void get_mp3_metadata(const char *filename, track_info_t *track)
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
    uint32_t file_size = f_size(&fil); // Get total file size
    track->audio_end = file_size;      // Default to end of file

    // Check for a 128-byte ID3v1 tag at the end of the file
    if (file_size > 128)
    {
        uint8_t tag_buf[3];
        f_lseek(&fil, file_size - 128);
        if (f_read(&fil, tag_buf, 3, &br) == FR_OK && br == 3)
        {
            if (memcmp(tag_buf, "TAG", 3) == 0)
            {
                track->audio_end = file_size - 128; // Audio ends before the ID3v1 tag
            }
        }
    }

out:
    f_close(&fil);
}

// Helper for qsort
int compare_filenames(const void *a, const void *b)
{
    const track_info_t *ta = (const track_info_t *)a;
    const track_info_t *tb = (const track_info_t *)b;
    return strcasecmp(ta->filename, tb->filename);
}
