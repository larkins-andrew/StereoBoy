#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "ff.h"    // FatFS  
#include "pico/stdio.h"

// SPI pins
#define PIN_MISO 28
#define PIN_MOSI 27
#define PIN_SCK  30
#define PIN_CS   32

FATFS fs;

int main() {
    stdio_init_all();

    // --- Init SPI ---
    spi_init(spi1, 10 * 1000 * 1000);  // 10 MHz
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);

    // --- CS pin ---
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // --- Mount filesystem ---
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        printf("f_mount failed: %d\n", fr);
        return 0;
    }

    // --- Open (create/overwrite) file ---
    FIL file;
    fr = f_open(&file, "hello_world.txt", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("f_open failed: %d\n", fr);
        return 0;
    }

    // --- Write data ---
    UINT bw;
    const char *msg = "hello world!!!";
    fr = f_write(&file, msg, strlen(msg), &bw);
    if (fr != FR_OK || bw != strlen(msg)) {
        printf("f_write failed\n");
        f_close(&file);
        return 0;
    }

    // --- Close file ---
    f_close(&file);
    printf("Write complete!\n");

    // Program ends here
    while (1) tight_loop_contents();
}
