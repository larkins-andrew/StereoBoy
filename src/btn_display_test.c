#include <stdio.h>
#include <string.h>
#include <stdlib.h> // Required for qsort
#include "pico/stdlib.h"
#include "hw_config.h"
#include "lib/display/display.h"
#include "lib/btns/buttons.h"


int main(){


    buttons_init(10);

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
    // lcd_draw_string(0, 0, "Shubham Was Here", BLUE);
    // lcd_draw_char(10, 10, 'B', CYAN);
    lcd_update(pio, sm);
    // lcd_draw_progress_bar(pio, sm, 200, 46);
    ///////////////////////////DISPLAY END///////////////////////////

    char * chars[] = {"a", "b", "c", "d"};
    int i = 0;
    char buff [10] = "0";
    while (true){
        // clear_framebuffer();
        // i = (i+1)%4;
        if (buttons_get_just_pressed() & BTN_A){
            i = i+1;
            sprintf(buff, "%d", i);
            print_screen(buff);
        }
        // lcd_draw_string(0,0,chars[i%4], WHITE);
        // lcd_draw_string(0,10,chars[(i+1)%4], WHITE);
        // lcd_draw_string(0,20,chars[(i+2)%4], WHITE);
        // lcd_draw_string(0,30,chars[(i+3)%4], WHITE);
        // lcd_draw_string(0,40,buff, WHITE);
        // lcd_update(pio, sm);
    }



}