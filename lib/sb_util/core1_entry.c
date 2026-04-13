// #include <stdlib.h>
// #include <stdio.h>
// #include <string.h>
// #include "core1_entry.h"
// #include "pico/multicore.h"
// #include "lib/sb_util/sb_util.h"
// #include "lib/display/fft.h"
// #include "lib/adc/adc.h"
// #include "lib/sb_util/sb_util.h"
#include "core1_entry.h"
#include "lib/sb_util/global_vars.h"
#include "lib/adc/adc.h"
#include "lib/display/display.h"
#include "lib/led_driver/led_driver.h"
#include "lib/font/font.h"

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
