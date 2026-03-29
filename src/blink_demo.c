// #include "lib/sb_util/sb_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hw_config.h"
// #include "hardware/i2c.h"
// #include "lib/display/display.h"

int main()
{
    int i = 0;
    gpio_init(24);
    gpio_set_dir(24, GPIO_OUT);

    while(1){
        gpio_put(24, i%2);
        i = (i + 1) % 10;
        printf("%d", i);
        sleep_ms(250);
    }

}
