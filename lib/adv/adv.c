#include "adv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void adv7180_init()
{
    // Array of {Register, Value}
    uint8_t config[][2] = {
        // 1. Wake up the chip
        {0x0F, 0x00},

        // 2. Select CVBS input on AIN1
        {0x00, 0x02},

        // 3. ADI Required Calibration Sequence (From Datasheet)
        {0x0E, 0x80},
        {0x9C, 0x00},
        {0x9C, 0xFF},
        {0x0E, 0x00},

        // 4. Enable Pixel and Sync Output Drivers
        {0x03, 0x0C},

        // 5. Extended Output Config (Standard Default)
        {0x04, 0x45}};

    int num_regs = sizeof(config) / sizeof(config[0]);

    for (int i = 0; i < num_regs; i++)
    {
        int ret = i2c_write_blocking(i2c0, ADV7180_ADDR, config[i], 2, false);

        if (ret < 0)
        {
            printf("FAIL: I2C NACK at i=%d (Error %d)\r\n", i, ret);
        }
        else
        {
            printf("SUCCESS: Wrote i=%d\r\n", i);
        }

        sleep_ms(2);
    }
}