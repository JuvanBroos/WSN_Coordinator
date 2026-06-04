#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"
#include "zigbee_task.h"
#include "serial/usb_serial.h"
#include "espnow/espnow.h"
#include "lora_manager.h"
#include "protocol.h"
#include "lora_gw_state_mgr.h"

void app_main(void)
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Start the Zigbee network */
    //ESP_LOGI("APP_MAIN", "Starting Zigbee task...");
    //xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);

    /* Start the ESPNOW network */
    wifi_init();
    espnow_init();

    /* Start the LoRa network */
    // lora_init();
    // xTaskCreate(lora_task, "LoRa_Task", 4096, NULL, 5, NULL);
    start_lora_task(NULL);

    /* Start the USB Serial JTAG task */
    xTaskCreate(usb_serial_jtag_task, "USB_Serial_JTAG", 4096, NULL, 5, NULL);

    /* Start the Serial Reporting task */
    xTaskCreate(serial_report_task, "Serial_Report", 4096, NULL, 5, NULL);

    //zigbee_read_attributes();
    vTaskDelay(pdMS_TO_TICKS(30000)); // Delay to ensure Zigbee task is up before sending test config
    //zigbee_send_test_config();
}
