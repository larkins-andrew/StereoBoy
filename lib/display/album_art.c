
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
