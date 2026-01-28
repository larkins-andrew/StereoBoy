#include <stdio.h>
#include <string.h>
#include <stdlib.h> // Required for qsort
#include "lib/dac/dac.h"
#include "lib/display/display.h"
#include "lib/led_driver/led_driver.h"
#include "lib/codec/codec.h"


int main()
{

    stdio_init_all();

    sleep_ms(5000);

    // set SPI0 for codec and SD card
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);

    // set I2C0 for DAC
    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(PIN_I2C0_SCL, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C0_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C0_SCL);
    gpio_pull_up(PIN_I2C0_SDA);

    printf("SPI0 and I2C0 initialized.\r\n");

    if (!sd_init_driver())
    {
        printf("SD init failed\r\n");
        while (1)
            ;
    }

    FATFS fs;
    if (f_mount(&fs, "0:", 1) != FR_OK)
    {
        printf("Mount failed\r\n");
        while (1)
            ;
    }

    vs1053_t player = {
        .spi = spi1,
        .cs = PIN_CS,
        .dcs = PIN_DCS,
        .dreq = PIN_DREQ,
        .rst = PIN_RST};

    vs1053_init(&player);
    printf("VS1053 initialized.\r\n");
    vs1053_set_volume(&player, 0x00, 0x00);
    printf("VS1053 volume set to max!\r\n");

    // Enable I2S output
    vs1053_enable_i2s(&player);
    printf("VS1053 I2S enabled.\r\n");

    // initialize DAC
    dac_init(i2c0);
    printf("DAC intialized.\r\n");

    printf("Reset reg: 0x%02X\n", readReg(i2c0, 0x01));
    printf("OT flag:   0x%02X\n", readReg(i2c0, 0x03));
    printf("NDAC:      0x%02X\n", readReg(i2c0, 0x0B));
    printf("MDAC:      0x%02X\n", readReg(i2c0, 0x0C));
    printf("DAC flag:  0x%02X\n", readReg(i2c0, 0x25));

    printf("Audio init complete.\r\n");
    printf("\r\nScanning directory...\r\n");

    // --- Scan MP3 files ---
    DIR dir;
    FILINFO fno;
    track_info_t tracks[MAX_TRACKS];
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

    ////////////////////////////DISPLAY/////////////////////////////
    PIO pio = pio0;
    uint sm = 0;
    gpio_init(PIN_CS_DISPLAY);
    gpio_init(PIN_DC);
    gpio_init(PIN_RESET);
    gpio_init(PIN_BL);
    gpio_set_dir(PIN_CS_DISPLAY, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_set_dir(PIN_BL, GPIO_OUT);
    gpio_put(PIN_CS_DISPLAY, 1);
    gpio_put(PIN_RESET, 1);
    lcd_init(pio, sm, st7789_init_seq);
    gpio_put(PIN_BL, 1);
    // lcd_draw_circle(120,120, 16, GREEN);
    // lcd_draw_circle_fill(120, 180, 33, rgbto565(0xFF3399));
    lcd_draw_string(0, 0, "Shubham Was Here", BLUE);
    // lcd_draw_char(10, 10, 'B', CYAN);
    lcd_update(pio, sm);
    // lcd_draw_progress_bar(pio, sm, 200, 46);

    ///////////////////////////DISPLAY END///////////////////////////

    //////////////////////////LED DRIVER/////////////////////////////

    // GPIO Init
    gpio_init(LED_HEARTBEAT);
    gpio_set_dir(LED_HEARTBEAT, GPIO_OUT);
    gpio_init(LED_FOUND);
    gpio_set_dir(LED_FOUND, GPIO_OUT);

    gpio_init(SDA_PIN);
    gpio_set_dir(SDA_PIN, GPIO_IN);
    gpio_init(SCL_PIN);
    gpio_set_dir(SCL_PIN, GPIO_IN);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(2000);

    // --- CONNECTION TEST ---
    i2c_start();
    bool ack = i2c_write_byte(PCA_ADDR << 1);
    i2c_stop();

    if (!ack)
    {
        // FAILED: Frantic Blink
        while (true)
        {
            gpio_put(LED_HEARTBEAT, 1);
            sleep_ms(50);
            gpio_put(LED_HEARTBEAT, 0);
            sleep_ms(50);
        }
    }

    // SUCCESS
    gpio_put(LED_FOUND, 1);
    pca_init();

    //////////////////////////LED END////////////////////////////////
    while (1)
    {
        //////////////////////// LED DRIVER //////////////////////////
        gpio_put(LED_HEARTBEAT, 1); // Heartbeat ON
        // We step by 32 because bit-banging is slow.
        // 0 -> 4096
        for (int i = 0; i < 4096; i += 128)
        {
            // ON=0, OFF=i means the LED is ON for 'i' ticks out of 4096
            for (int j=0; j<16; j++){
                pca_set_pwm(j, 0, i);
            }
        }

        // FADE DOWN (Get Dimmer)
        gpio_put(LED_HEARTBEAT, 0); // Heartbeat OFF
        // 4095 -> 0
        for (int i = 4095; i >= 0; i -= 128)
        {
            
            pca_set_pwm(TARGET_CHANNEL, 0, i);
        }
        /////////////////////// LED END ///////////////////

        // --- Print menu ---
        printf("\r\nAvailable tracks:\r\n");
        for (int i = 0; i < count; i++)
        {
            printf("\r\n[%d] %s - %s\r\n", i + 1, tracks[i].artist, tracks[i].title);
            printf("     Album: %s\r\n", tracks[i].album);
        }

        char input[8];
        int choice = 0;

        while (choice < 1 || choice > count)
        {
            printf("\r\nSelect track (1-%d): ", count);

            int idx = 0;
            memset(input, 0, sizeof(input));

            while (1)
            {
                int c = getchar(); // blocking read
                if (c == '\r' || c == '\n')
                { // Enter pressed
                    printf("\r\n");
                    break;
                }
                if (idx < sizeof(input) - 1)
                {
                    input[idx++] = c;
                    putchar(c); // echo typed char
                }
            }

            choice = atoi(input);
            if (choice < 1 || choice > count)
                printf("Invalid. Try again.");
        }

        track_info_t *sel = &tracks[choice - 1];
        // track_info_t *sel_next = &tracks[choice];

        printf("\r\n\r\nNow playing:\r\n");
        printf("  Title : %s\r\n", sel->title);
        printf("  Artist: %s\r\n", sel->artist);
        printf("  Album : %s\r\n\r\n", sel->album);

        play_file(&player, sel->filename);
        // play_file(&player, sel_next->filename);

        printf("\r\nPlayback finished.\r\n");
    };
}
