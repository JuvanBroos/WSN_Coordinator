#include "lora_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "ra01s.h"
#include "ra01s_int.h"
#include <string.h>

static const char *TAG = "LORA_MGR";
static QueueHandle_t s_isr_queue = NULL;
static SemaphoreHandle_t s_lora_spi_mutex = NULL;

void lora_init(void) {
    s_lora_spi_mutex = xSemaphoreCreateMutex();
    s_isr_queue = xQueueCreate(10, sizeof(lora_event_t));

    LoRaInit(); // Initialize LoRa SPI and Pins
    
    int8_t txPowerInDbm = CONFIG_TX_POWER;
    uint32_t frequencyInHz = CONFIG_LORA_FREQUENCY_MHZ * 1000000;
    float tcxoVoltage = CONFIG_USE_TCXO * 3.3; 
    bool useRegulatorLDO = CONFIG_USE_TCXO; 

    if (LoRaBegin(frequencyInHz, txPowerInDbm, tcxoVoltage, useRegulatorLDO) != 0) {
        ESP_LOGE(TAG, "Does not recognize the module");
        return;
    }

    bool invertIrq = false;
#ifdef CONFIG_INVERT_IQ
    invertIrq = true * CONFIG_INVERT_IQ;
#endif

    LoRaConfig(CONFIG_SF_RATE, CONFIG_BANDWIDTH, CONFIG_CODING_RATE, CONFIG_PREAMBLE_LENGTH, CONFIG_PAYLOAD_LENGTH, CONFIG_CRC_ON, invertIrq);
    
    if (lora_int_init(s_isr_queue) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LoRa interrupt wrapper");
    }

    ESP_LOGI(TAG, "LoRa Gateway Initialized");
}

void lora_send_command(serial_command_t *cmd) {
    if (!s_lora_spi_mutex) return;
    
    uint8_t buf[255];
    downlink_payload_t *dl_payload = (downlink_payload_t *)buf;
    memset(dl_payload, 0, sizeof(downlink_payload_t));
    
    dl_payload->target_addr = cmd->dest_short_addr;
    
    if (cmd->command_type == CHANGE_PROTOCOL) {
        dl_payload->msg_type = MSG_TYPE_CONFIG;
        dl_payload->set_protocol = (uint8_t)cmd->command_value_3.protocol;
        dl_payload->sleep_interval = 0; 
    } else if (cmd->command_type == CHANGE_SLEEP_INTERVAL) {
        dl_payload->msg_type = MSG_TYPE_CONFIG;
        dl_payload->set_protocol = PROTOCOL_NO_CHANGE;
        dl_payload->sleep_interval = cmd->command_value_3.sleep_interval;
    } else if (cmd->command_type == READ_ATTRIBUTE) {
        dl_payload->msg_type = MSG_TYPE_READ_REQ;
        dl_payload->set_protocol = PROTOCOL_NO_CHANGE;
        dl_payload->sleep_interval = 0;
    } else {
        dl_payload->msg_type = MSG_TYPE_CONFIG;
        dl_payload->set_protocol = PROTOCOL_NO_CHANGE;
        dl_payload->sleep_interval = 0; 
    }

    uint8_t txLen = sizeof(downlink_payload_t);
    
    xSemaphoreTake(s_lora_spi_mutex, portMAX_DELAY);
    bool success = LoRaSend(buf, txLen, SX126x_TXMODE_ASYNC);
    xSemaphoreGive(s_lora_spi_mutex);

    if (success) {
        ESP_LOGI(TAG, "Downlink command queued for 0x%04x via LoRa", cmd->dest_short_addr);
    } else {
        ESP_LOGE(TAG, "Failed to send downlink command via LoRa");
    }
}

void lora_task(void *pvParameters) {
    ESP_LOGI(TAG, "LoRa Task Started");
    
    xSemaphoreTake(s_lora_spi_mutex, portMAX_DELAY);
    SetRx(0xFFFFFF); // Continuous RX mode
    xSemaphoreGive(s_lora_spi_mutex);

    lora_event_t evt;
    while(1) {
        if (xQueueReceive(s_isr_queue, &evt, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(s_lora_spi_mutex, portMAX_DELAY);
            uint16_t irq = GetIrqStatus();
            
            if (irq & SX126X_IRQ_RX_DONE) {
                uint8_t rxBuf[255];
                uint8_t rxLen = LoRaReceive(rxBuf, sizeof(rxBuf));
                
                if (rxLen > 0) {
                    ESP_LOGI(TAG, "%d byte packet received", rxLen);
                    int8_t rssi, snr;
                    GetPacketStatus(&rssi, &snr);
                    ESP_LOGI(TAG, "rssi=%d[dBm] snr=%d[dB]", rssi, snr);
                    
                    if (rxLen == sizeof(uplink_payload_t)) {
                        uplink_payload_t *payload = (uplink_payload_t *)rxBuf;
                        ESP_LOGI(TAG, "Payload Data - ShortAddr: 0x%04x, Temp: %d", payload->short_addr, payload->temperature);
                        
                        // Send serial report to GUI
                        serial_report_t report = {0};
                        report.protocol = PROTO_LORA;
                        report.report_type = REPORT_TYPE_SENSOR_DATA;
                        report.src_short_addr = payload->short_addr;
                        report.rssi = rssi;
                        report.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                        report.length = sizeof(uplink_payload_t);
                        memcpy(report.payload, payload, sizeof(uplink_payload_t));
                        gateway_send_report(&report);
                    } else {
                        ESP_LOGW(TAG, "Unknown payload length: %d", rxLen);
                    }
                }
            }
            
            if (irq & SX126X_IRQ_TX_DONE) {
                ESP_LOGI(TAG, "TX_DONE interrupt");
                ReceiveMode(); // Resets txActive and enters Continuous RX
            }
            
            ClearIrqStatus(irq);
            
            // Ensure we are back in RX mode if we didn't call ReceiveMode()
            if (!(irq & SX126X_IRQ_TX_DONE)) {
                 SetRx(0xFFFFFF);
            }

            xSemaphoreGive(s_lora_spi_mutex);
        }
    }
}