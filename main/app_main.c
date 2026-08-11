#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"
#include "zigbee_task.h"
#include "serial/usb_serial.h"
#include "espnow/espnow.h"
//#include "lora_manager.h"
#include "protocol.h"
#include "lora_gw_state_mgr.h"
#include "driver/spi_master.h"
#include "sd_logger.h"
#include "driver/gpio.h"
#include "esp_coexist.h"
#include "esp_coex_i154.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern uint32_t g_dropped_serial_reports;
extern uint32_t g_dropped_sd_logs;

static void diagnostics_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        ESP_LOGI("DIAG", "--- Diagnostics Report ---");
        ESP_LOGI("DIAG", "Min Free Heap: %lu bytes", esp_get_minimum_free_heap_size());
        ESP_LOGI("DIAG", "Dropped Serial Packets: %lu", g_dropped_serial_reports);
        ESP_LOGI("DIAG", "Dropped SD Logs: %lu", g_dropped_sd_logs);

        // Run Time Stats (Requires CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
        char *stats_buffer = malloc(1024);
        if (stats_buffer) {
            vTaskGetRunTimeStats(stats_buffer);
            ESP_LOGI("DIAG", "Task Run Time Stats:\n%s", stats_buffer);
            free(stats_buffer);
        }
#endif
        ESP_LOGI("DIAG", "--------------------------");
    }
}

#if CONFIG_SPI2_HOST
#define HOST_ID SPI2_HOST
#elif CONFIG_SPI3_HOST
#define HOST_ID SPI3_HOST
#endif
//#define ZIGBEE_COORDINATOR
//#define COEXISTENCE_TEST

void app_main(void)
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Pre-emptively set both CS pins HIGH to prevent bus contention during initialization */
    gpio_set_direction(CONFIG_NSS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_NSS_GPIO, 1);
    gpio_set_direction(19, GPIO_MODE_OUTPUT); // SD Card CS
    gpio_set_level(19, 1);

    /* Reset LoRa module to ensure it releases the MISO line */
    gpio_set_direction(CONFIG_RST_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(CONFIG_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    /* Enable internal pull-up for MISO line only to prevent floating states */
    gpio_set_pull_mode(CONFIG_MISO_GPIO, GPIO_PULLUP_ONLY);

    /* Initialize SPI Bus for LoRa and SD Card */
    spi_bus_config_t spi_bus_config = {
        .sclk_io_num = CONFIG_SCLK_GPIO,
        .mosi_io_num = CONFIG_MOSI_GPIO,
        .miso_io_num = CONFIG_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092,
    };
    esp_err_t spi_ret = spi_bus_initialize(HOST_ID, &spi_bus_config, SPI_DMA_CH_AUTO);
    if (spi_ret == ESP_OK) {
        sd_logger_init(HOST_ID);
    } else {
        ESP_LOGE("APP_MAIN", "Failed to initialize SPI bus");
    }

    /*Start the Wifi and LoRa tasks first then the Zigbee task */
    #ifdef COEXISTENCE_TEST
    // esp_coex_prefer_t coex_pref = ESP_COEX_PREFER_WIFI;
    // esp_coex_preference_set(coex_pref);
    esp_coex_wifi_i154_enable();

    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);

    wifi_init();
    espnow_init();
    start_lora_task(NULL);
    
    #else

    #ifdef ZIGBEE_COORDINATOR
    /* Start the Zigbee network */
    ESP_LOGI("APP_MAIN", "Starting Zigbee task...");
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
    #else
    /* Start the ESPNOW network */
    wifi_init();
    espnow_init();

    /* Start the LoRa network */
    start_lora_task(NULL);
    #endif
    #endif

    /* Start the USB Serial JTAG task */
    xTaskCreate(usb_serial_jtag_task, "USB_Serial_JTAG", 4096, NULL, 5, NULL);

    /* Start the Serial Reporting task */
    xTaskCreate(serial_report_task, "Serial_Report", 4096, NULL, 5, NULL);

    /* Start the Diagnostics task */
    xTaskCreate(diagnostics_task, "Diagnostics", 4096, NULL, 2, NULL);
}
