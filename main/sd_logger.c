#include "sd_logger.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <sys/unistd.h>
#include <sys/stat.h>

static const char *TAG = "SD_LOGGER";

#define MOUNT_POINT "/sdcard"
#define SD_CS_PIN 19 // As specified by user for SD card CS

static QueueHandle_t sd_log_queue = NULL;
static char current_log_filename[64];
static sdmmc_card_t *sd_card = NULL;

static void sd_logger_task(void *arg)
{
    serial_report_t report;
    int consec_failures = 0;
    
    while (1) {
        if (xQueueReceive(sd_log_queue, &report, portMAX_DELAY) == pdTRUE) {
            // Unpack payload if it is sensor data
            if (report.report_type == REPORT_TYPE_SENSOR_DATA) {
                uplink_payload_t payload;
                // Double check length is what we expect
                if (report.length == sizeof(uplink_payload_t)) {
                    memcpy(&payload, report.payload, sizeof(uplink_payload_t));
                    
                    // Format CSV
                    const char *proto_str = "UNK";
                    if (report.protocol == PROTO_WIFI) proto_str = "ESP-NOW";
                    else if (report.protocol == PROTO_ZIGBEE) proto_str = "Zigbee";
                    else if (report.protocol == PROTO_LORA) proto_str = "LoRa";
                    
                    char row[256];
                    snprintf(row, sizeof(row), "%lu,0x%04X,%s,%d,%d,%.2f,%d,%.6f,%.6f,%d/%d/%d\n",
                             report.timestamp,
                             report.src_short_addr,
                             proto_str,
                             report.rssi,
                             payload.batt_level,
                             payload.temperature / 10.0f - 273.15f,
                             payload.gnss_sats,
                             payload.latitude / 1000000.0f,
                             payload.longitude / 1000000.0f,
                             payload.accel_x, payload.accel_y, payload.accel_z);
                    
                    FILE *f = fopen(current_log_filename, "a");
                    if (f == NULL) {
                        ESP_LOGE(TAG, "Failed to open file %s for appending", current_log_filename);
                        consec_failures++;
                    } else {
                        if (fprintf(f, "%s", row) < 0) {
                            ESP_LOGE(TAG, "Failed to write to file");
                            consec_failures++;
                        } else {
                            consec_failures = 0; // Success
                        }
                        fclose(f);
                    }
                    
                    if (consec_failures >= 3) {
                        ESP_LOGW(TAG, "Consecutive SD card failures. Unmounting and halting SD logging.");
                        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, sd_card);
                        sd_card = NULL;
                        vTaskSuspend(NULL);
                    }
                }
            }
        }
    }
}

bool sd_logger_init(int host_id)
{
    ESP_LOGI(TAG, "Initializing SD card via SPI");
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // Attach to the already initialized SPI bus
    sdmmc_host_t host_config = SDSPI_HOST_DEFAULT();
    host_config.slot = host_id; // Explicitly assign host
    host_config.max_freq_khz = SDMMC_FREQ_PROBING; // 400 kHz for maximum compatibility
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host_id;
    slot_config.gpio_cs = SD_CS_PIN;
    
    esp_err_t ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host_config, &slot_config, &mount_config, &sd_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGW(TAG, "Failed to mount filesystem (card may not be present).");
        } else {
            ESP_LOGW(TAG, "Failed to initialize the card (%s).", esp_err_to_name(ret));
        }
        return false;
    }
    
    ESP_LOGI(TAG, "SD Card mounted successfully.");
    sdmmc_card_print_info(stdout, sd_card);
    
    // Generate a unique filename using NVS boot_count
    nvs_handle_t nvs_handle;
    int32_t boot_count = 0;
    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        nvs_get_i32(nvs_handle, "boot_count", &boot_count);
        boot_count++;
        nvs_set_i32(nvs_handle, "boot_count", boot_count);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Error opening NVS handle for boot_count");
    }
    
    snprintf(current_log_filename, sizeof(current_log_filename), MOUNT_POINT"/log_%04ld.csv", boot_count);
    ESP_LOGI(TAG, "Logging to: %s", current_log_filename);
    
    // Write CSV header for new file
    FILE *f = fopen(current_log_filename, "a");
    if (f != NULL) {
        fprintf(f, "Timestamp (ms),Node ID,Protocol,RSSI,Batt (mV),Temp (C),GNSS Sats,Latitude,Longitude,Accel X/Y/Z\n");
        fclose(f);
    }
    
    // Create queue and task
    sd_log_queue = xQueueCreate(20, sizeof(serial_report_t));
    if (sd_log_queue != NULL) {
        xTaskCreate(sd_logger_task, "sd_logger", 4096, NULL, 3, NULL); // Priority 3, lower than main tasks
    }
    
    return true;
}

uint32_t g_dropped_sd_logs = 0;

void sd_logger_queue_report(serial_report_t *report)
{
    if (sd_log_queue != NULL) {
        // Enqueue but don't block for long if full (to prevent blocking callers)
        if (xQueueSend(sd_log_queue, report, pdMS_TO_TICKS(10)) != pdTRUE) {
            g_dropped_sd_logs++;
        }
    }
}

