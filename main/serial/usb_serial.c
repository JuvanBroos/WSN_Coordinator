#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

static const char *TAG = "USB_SERIAL";

#define SERIAL_SYNC_1 0xAA
#define SERIAL_SYNC_2 0x55
#define MAX_PAYLOAD_LEN 256

// State machine states for packet parsing
typedef enum {
    STATE_WAIT_SYNC1,
    STATE_READ_PAYLOAD,
} rx_state_t;

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

    ESP_LOGI(TAG, "USB Serial JTAG initialized. Waiting for Sync1 (0xAA)...");

    uint8_t rx_buf[64];
    uint8_t packet_buf[MAX_PAYLOAD_LEN + 2]; // Sync1 + Payload + Sync2 max allocation
    uint16_t packet_idx = 0;
    rx_state_t state = STATE_WAIT_SYNC1;

    // 2. Infinite task loop processing the stream
    while (1) {
        // Read chunks of data (non-blocking with a short timeout)
        int bytes_read = usb_serial_jtag_read_bytes(rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        
        if (bytes_read > 0) {
            for (int i = 0; i < bytes_read; i++) {
                uint8_t byte = rx_buf[i];

                switch (state) {
                    case STATE_WAIT_SYNC1:
                        if (byte == SERIAL_SYNC_1) {
                            packet_idx = 0;
                            packet_buf[packet_idx++] = byte;
                            state = STATE_READ_PAYLOAD;
                        }
                        break;

                    case STATE_READ_PAYLOAD:
                        packet_buf[packet_idx++] = byte;
                        
                        if (byte == SERIAL_SYNC_2) {
                            // Valid packet received ending with SYNC_BYTE_2
                            ESP_LOGI(TAG, "Packet received (Len: %d). Echoing back...", packet_idx);
                            
                            // Echo the packet back
                            usb_serial_jtag_write_bytes(packet_buf, packet_idx, portMAX_DELAY);
                            
                            // Reset state for next packet
                            state = STATE_WAIT_SYNC1;
                        } else if (packet_idx >= sizeof(packet_buf)) {
                            // Buffer overflow before reaching Sync 2, invalid packet. Reset and ignore.
                            ESP_LOGW(TAG, "Packet buffer overflow. Discarding.");
                            state = STATE_WAIT_SYNC1;
                        }
                        break;
                }
            }
        }
    }
}