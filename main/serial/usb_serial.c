#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "zigbee_task.h"
#include "espnow.h"
#include "usb_serial.h"
#include "protocol.h"

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

        /* Handle the command based on protocol */
        if (cmd.protocol == PROTO_ZIGBEE) {
            switch (cmd.command_type) {
                case CHANGE_REPORT_CONFIG:
                    ESP_LOGI(TAG, "ZIGBEE: CHANGE_REPORT_CONFIG received. Dest: 0x%04x", cmd.dest_short_addr);
                    zigbee_send_debug_config(cmd);
                    break;

                case READ_ATTRIBUTE:
                    ESP_LOGI(TAG, "ZIGBEE: READ_ATTRIBUTE 0x%04x to 0x%04x", cmd.command_value_3.read_attribute_id, cmd.dest_short_addr);
                    zigbee_read_attributes(cmd);
                    break;

                case CHANGE_PROTOCOL:
                    ESP_LOGI(TAG, "ZIGBEE: CHANGE_PROTOCOL to %u for 0x%04x", cmd.command_value_3.protocol, cmd.dest_short_addr);
                    zigbee_change_protocol(cmd);
                    break;

                default:
                    ESP_LOGW(TAG, "ZIGBEE: Unknown command_type 0x%02x", cmd.command_type);
                    break;
            }
        } else if (cmd.protocol == PROTO_WIFI) {
            ESP_LOGI(TAG, "ESPNOW: Command 0x%02x received for 0x%04x", cmd.command_type, cmd.dest_short_addr);
            espnow_send_command(cmd);
        } else {
            ESP_LOGW(TAG, "Unsupported protocol: 0x%02x", cmd.protocol);
        }
    }
}