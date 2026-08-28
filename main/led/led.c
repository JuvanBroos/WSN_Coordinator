#include <stdio.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"


void led_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = LEDC_MIN_DUTY, // Start with LED off
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

esp_err_t led_flash(uint32_t frequency_hz, uint32_t duty) {
    esp_err_t err;
    err = ledc_set_freq(LEDC_MODE, LEDC_TIMER, frequency_hz);
    if (err != ESP_OK) {
        return err;
    }
    err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        return err;
    }
    err = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t led_off(void) {
    return led_flash(LEDC_FREQUENCY, LEDC_MIN_DUTY); // Set duty to 0 to turn off the LED
}   

esp_err_t led_on(void) {
    return led_flash(LEDC_FREQUENCY, LEDC_MAX_DUTY); // Set duty to LEDC_DUTY to turn on the LED at the configured brightness
}
