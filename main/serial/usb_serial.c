#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "zigbee_task.h"
#include "usb_serial.h"

static const char *TAG = "USB_SERIAL";

void usb_serial_jtag_task(void *arg)
{
    // 1. Initialize USB Serial JTAG Driver
    usb_serial_jtag_driver_config_t usb_config = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    
    // Install the driver
    esp_err_t ret = usb_serial_jtag_driver_install(&usb_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB Serial JTAG driver: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

     ESP_LOGI(TAG, "USB Serial JTAG initialized. Waiting for serial_command_t packets...");

     /* Ensure the packed command struct has the expected size to avoid
         receiving unexpected padding from compiler alignment. */
     //_Static_assert(sizeof(serial_command_t) == 8, "serial_command_t must be 8 bytes");

     /* Read whole command structs at once into this variable */
     serial_command_t cmd = {0};
     size_t cmd_size = sizeof(serial_command_t);

    // 2. Infinite task loop processing the stream
    while (1) {
        int bytes_read = usb_serial_jtag_read_bytes((uint8_t *)&cmd, cmd_size, portMAX_DELAY);

        if (bytes_read != (int)cmd_size) {
            ESP_LOGW(TAG, "Partial read: %d bytes (expected %d). Skipping.", bytes_read, (int)cmd_size);
            continue;
        }

        /* Validate sync bytes */
        if (cmd.sync1 != SERIAL_SYNC_1 || cmd.sync2 != SERIAL_SYNC_2) {
            ESP_LOGW(TAG, "Invalid sync bytes: 0x%02x 0x%02x", cmd.sync1, cmd.sync2);
            continue;
        }

        /* Handle the command based on the typed enum in usb_serial.h */
        switch (cmd.command_type) {
            case CHANGE_REPORT_CONFIG:
                ESP_LOGI(TAG, "CHANGE_REPORT_CONFIG received. Dest: 0x%04x, min: %u, max: %u, delta: %u",
                         cmd.dest_short_addr, cmd.command_value_1_min, cmd.command_value_2_max, cmd.command_value_3.report_config_delta);
                //zigbee_send_test_config(cmd);
                zigbee_send_debug_config(cmd);
                break;

            case READ_ATTRIBUTE:
                ESP_LOGI(TAG, "READ_ATTRIBUTE 0x%04x received to 0x%04x", cmd.command_value_3.read_attribute_id, cmd.dest_short_addr);
                zigbee_read_attributes(cmd);
                break;

            case CHANGE_PROTOCOL:
                ESP_LOGI(TAG, "CHANGE_PROTOCOL received (proto: %u). No handler implemented yet.", cmd.command_value_3.protocol);
                zigbee_change_protocol(cmd);
                break;

            default:
                ESP_LOGW(TAG, "Unknown command_type 0x%02x", cmd.command_type);
                break;
        }
    }
}