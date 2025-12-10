#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "ff.h"    // FatFS  
#include "pico/stdio.h"
#include <string.h>
#include "sd_driver/sd_card.h"


// SPI pins
#define PIN_MISO 28
#define PIN_MOSI 27
#define PIN_SCK  26
#define PIN_CS   29

#define PIN_LED  25
#define PIN_LED2 24

FATFS fs;

void error_blink() {
    while (true) {
        gpio_put(PIN_LED2, 1);
        sleep_ms(100);
        gpio_put(PIN_LED2, 0);
        sleep_ms(100);
    }
}

//binary light flash to report error code
void report_error(int error_code) {
    while (true) {
        // Long OFF pause to mark the start of the sequence
        gpio_put(PIN_LED, 0);
        sleep_ms(2000); 

        // Blink 'error_code' times
        for (int i = 0; i < error_code; i++) {
            gpio_put(PIN_LED, 1);
            sleep_ms(200);
            gpio_put(PIN_LED, 0);
            sleep_ms(200);
        }
    }
}

int main() {
    stdio_init_all();
    // gpio_pull_up(28);
    // --- WIGGLE TEST START ---
// Comment this block out once you confirm the scope works!
// gpio_init(29); // CS
// gpio_set_dir(29, GPIO_OUT);
// gpio_init(27); // MOSI
// gpio_set_dir(27, GPIO_OUT);
// gpio_init(26); // SCK
// gpio_set_dir(26, GPIO_OUT);

// while (true) {
//     // Toggle pins High/Low every 1ms (500Hz square wave)
//     gpio_put(29, 1); 
//     gpio_put(27, 1);
//     gpio_put(26, 1);
//     sleep_ms(1);
    
//     gpio_put(29, 0); 
//     gpio_put(27, 0);
//     gpio_put(26, 0);
//     sleep_ms(1);
// }
// --- WIGGLE TEST END ---




    // --- Init LED ---
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0); // Ensure it's off initially
    
    gpio_init(PIN_LED2);
    gpio_set_dir(PIN_LED2, GPIO_OUT);
    gpio_put(PIN_LED2, 0); // Ensure it's off initially

    // --- Init SPI ---
    // spi_init(spi1, 1000 * 1000);  // 1 MHz
    // gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    // gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    // gpio_set_function(PIN_SCK, GPIO_FUNC_SPI); // Ensure this is wired to GP26
    
    // // --- CS pin ---
    // gpio_init(PIN_CS);
    // gpio_set_dir(PIN_CS, GPIO_OUT);
    // gpio_put(PIN_CS, 1);

    gpio_put(PIN_LED, 1); // ON
    sleep_ms(1000);       // Wait 1 second
    gpio_put(PIN_LED, 0); // OFF

    // // Check how many drives the hardware config reported
    // size_t num_cards = sd_get_num();
    // if (num_cards == 0) {
    //     // If this blinks, hwconfig.c is not linked properly
    //     while(1) {
    //         gpio_put(PIN_LED, 1); sleep_ms(50);
    //         gpio_put(PIN_LED, 0); sleep_ms(50);
    //     }
    // }

    // --- Mount filesystem ---
    FRESULT fr = f_mount(&fs, "0:", 1); // 
    gpio_put(PIN_LED, 0); // Turn off the "Busy" LED

    if (fr != FR_OK) {
        report_error(fr); // PASS THE ERROR CODE HERE
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