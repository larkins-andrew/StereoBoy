#include <stdio.h>
#include "pico/stdlib.h"
#include "lib/buttons/buttons.h"

int main() {
    stdio_init_all();
    sleep_ms(2000); 

    printf("Button Test Demo Started\n");

    //Initialize buttons with a 10ms scan rate
    buttons_init(10);

    while (true) {
        //get buttons that were JUST pressed
        uint8_t pressed = buttons_get_just_pressed();

        if (pressed > 0) {
            printf("Button Event Detected! Raw Hex: 0x%02X\n", pressed);

            if (pressed & BTN_SELECT) printf(" -> SELECT Pressed\n");
            if (pressed & BTN_START)  printf(" -> START Pressed\n");
            if (pressed & BTN_A)      printf(" -> A Pressed\n");
            if (pressed & BTN_B)      printf(" -> B Pressed\n");
            if (pressed & BTN_R)      printf(" -> RIGHT Pressed\n");
            if (pressed & BTN_L)      printf(" -> LEFT Pressed\n");
            if (pressed & BTN_U)      printf(" -> UP Pressed\n");
            if (pressed & BTN_D)      printf(" -> DOWN Pressed\n");
            
            printf("-------------------------------\n");
        }
        sleep_ms(10);
    }

    return 0;
}