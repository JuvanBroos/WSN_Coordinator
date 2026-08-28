#pragma once

#include <stdio.h>
#include "driver/ledc.h"


#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (14) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (4096) // Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_MAX_DUTY           (8192) // Maximum duty for 13 bits resolution
#define LEDC_MIN_DUTY           (0)    // Minimum duty is 0 
#define LEDC_FREQUENCY          (2) // Frequency in Hertz. Set frequency at 4 kHz

void led_init(void);
esp_err_t led_flash(uint32_t frequency_hz, uint32_t duty);
esp_err_t led_off(void);
esp_err_t led_on(void);
esp_err_t toggle_led(void);

