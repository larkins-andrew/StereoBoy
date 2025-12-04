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

#define PIN_LED  25

FATFS fs;

void error_blink() {
    while (true) {
        gpio_put(PIN_LED, 1);
        sleep_ms(100);
        gpio_put(PIN_LED, 0);
        sleep_ms(100);
    }
}

int main() {
    stdio_init_all();
    
    // --- Init LED ---
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0); // Ensure it's off initially
    
    // --- Init SPI ---
    spi_init(spi1, 1000 * 1000);  // 1 MHz
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI); // Ensure this is wired to GP26
    
    // --- CS pin ---
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    
    // --- Mount filesystem ---
    FRESULT fr = f_mount(&fs, "", 1);
    gpio_put(PIN_LED, 1);
    if (fr != FR_OK) {
        error_blink(); // Fast blink if mount fails
        return 0;
    }

    // --- Open (create/overwrite) file ---
    FIL file;
    fr = f_open(&file, "hello_world3.txt", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        error_blink(); // Fast blink if open fails
        return 0;
    }

    // --- Write data ---
    UINT bw;
    const char *msg = "hello world!!!";
    fr = f_write(&file, msg, strlen(msg), &bw);
    if (fr != FR_OK || bw != strlen(msg)) {
        f_close(&file);
        error_blink(); // Fast blink if write fails
        return 0;
    }

    // --- Close file ---
    // This physically saves the data to the SD card
    f_close(&file);

    // --- SUCCESS INDICATOR ---
    // Turn on LED solid to indicate safe completion

    // Program ends here, LED stays on
    while (1) tight_loop_contents();
}