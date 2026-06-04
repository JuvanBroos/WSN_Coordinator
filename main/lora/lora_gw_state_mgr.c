#include "lora_gw_state_mgr.h"
#include "esp_log.h"
#include "ra01s.h"
#include "lora_interrupt.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"
#include "protocol.h"
#include "string.h"
#include "esp_random.h"
#include "serial/usb_serial.h"

static const char *TAG = "LORA_TASK";
static QueueHandle_t isr_queue = NULL;
//static QueueHandle_t event_queue = NULL; // Queue for state machine events
//lora_state_t system_state = LORA_STATE_OFF;
uint8_t rxbuf[255];
uint8_t rxlen = 0;


static waiting_for_ack_t waiting_for_ack_node;

TimerHandle_t deferral_timer = NULL;
TimerHandle_t ack_timeout_timer;

lora_queue_item_t s_deferred_event; // Used to hold an event that is being deferred when we are busy and can't process it right now

static void deferral_timer_callback(TimerHandle_t timer){
	xQueueSendToBack(isr_queue, &s_deferred_event, 0); 
}

static void ack_timeout_timer_callback(TimerHandle_t xTimer) {
    lora_queue_item_t timeout_event;
    timeout_event.type = LORA_EVENT_TIMEOUT_RTOS;
    xQueueSend(isr_queue, &timeout_event, portMAX_DELAY);
}

static void defer_event(lora_queue_item_t *item) {
	memcpy(&s_deferred_event, item, sizeof(lora_queue_item_t));
	xTimerStart(deferral_timer, 0);
}

static uint8_t lora_read_rx_message(uint8_t *pData, int16_t len) //Copy of LoRaReceive() without the GetIrqStatus() call, since that is now handled in the process event function
{
	uint8_t rxLen = 0;
	rxLen = ReadBuffer(pData, len);
	
	return rxLen;
}

static bool parse_rx_message(uint8_t *rxbuf, uint8_t rxLen, uplink_payload_t *ul_payload) {
    if (rxLen != sizeof(uplink_payload_t)) {
        ESP_LOGW(TAG, "Received packet of unexpected length: %d", rxLen);
        return false;
    }
    memcpy(ul_payload, rxbuf, sizeof(uplink_payload_t));
    return true;
}

static bool is_sensor_data(uplink_payload_t *ul_payload) {
    if (!parse_rx_message(rxbuf, rxlen, ul_payload)) {
        return false;
    }
    return ul_payload->msg_type == MSG_TYPE_DATA;
}

static void report_data(uplink_payload_t *ul_payload) {
    // Send serial report to GUI
    serial_report_t report = {0};
    report.protocol = PROTO_LORA;
    report.report_type = REPORT_TYPE_SENSOR_DATA;
    report.src_short_addr = ul_payload->short_addr;
    report.rssi = 50;//rssi;
    report.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    report.length = sizeof(uplink_payload_t);
    memcpy(report.payload, ul_payload, sizeof(uplink_payload_t));
    gateway_send_report(&report);
}

static void setup_waiting_ack_node(serial_command_t *cmd) {
    waiting_for_ack_node.is_active = true;
    waiting_for_ack_node.target_addr = cmd->dest_short_addr;
    waiting_for_ack_node.retries = 0;
    memcpy(&waiting_for_ack_node.cmd, cmd, sizeof(serial_command_t));
    waiting_for_ack_node.ack_timer = ack_timeout_timer;
    
}

static bool is_valid_ack_for_waiting_node(uint16_t target_addr) {
    return waiting_for_ack_node.is_active && waiting_for_ack_node.target_addr == target_addr;
}

static void clear_waiting_node() {
    xTimerStop(waiting_for_ack_node.ack_timer, 0);
    memset(&waiting_for_ack_node, 0, sizeof(waiting_for_ack_node));
    
}

static void lora_send_command(serial_command_t *cmd) {
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
        // s_pending_read_req = true;
        // s_pending_read_target = cmd->dest_short_addr;
    } else {
        dl_payload->msg_type = MSG_TYPE_CONFIG;
        dl_payload->set_protocol = PROTOCOL_NO_CHANGE;
        dl_payload->sleep_interval = 0; 
    }

    uint8_t txLen = sizeof(downlink_payload_t);

    LoRaSend(buf, txLen, SX126x_TXMODE_SYNC);
}

static void lora_send_ack(uint16_t target_short_addr) {
    uint8_t txbuf[255];
    downlink_payload_t *dl_payload = (downlink_payload_t *)txbuf;
    memset(dl_payload, 0, sizeof(downlink_payload_t));
    dl_payload->msg_type = MSG_TYPE_ACK;
    dl_payload->target_addr = target_short_addr;
    dl_payload->set_protocol = PROTOCOL_NO_CHANGE;
    dl_payload->sleep_interval = 0;
    uint8_t txlen = sizeof(downlink_payload_t);
    LoRaSend(txbuf, txlen, SX126x_TXMODE_SYNC);
}

void lora_queue_command(serial_command_t *cmd) {
    lora_queue_item_t item;
    item.type = LORA_EVENT_SERIAL_CMD;
    memcpy(&item.cmd_data, cmd, sizeof(serial_command_t));
    xQueueSend(isr_queue, &item, portMAX_DELAY);
}

static bool process_event(lora_event_type_t *evt) {
    lora_event_type_t event_type = *evt;
    if (*evt == LORA_EVENT_DIO1_INTR) {
        uint16_t irq = GetIrqStatus();
        if (irq & SX126X_IRQ_RX_DONE) {
            event_type = LORA_EVENT_RX_DONE;
            rxlen = lora_read_rx_message(rxbuf, sizeof(rxbuf));
            ClearIrqStatus(SX126X_IRQ_RX_DONE); // Clear only RX_DONE
        } else if (irq & SX126X_IRQ_TIMEOUT) {
            ESP_LOGE(TAG, "Timeout occurred on the SX1262!");
            ReceiveMode(); // Reset txActive flag in driver
            ClearIrqStatus(SX126X_IRQ_TIMEOUT);
            return false;
        } else {
            // Unrecognized event, clear all IRQs to recover
            ClearIrqStatus(irq);
            return false;
        }
    } else if (*evt == LORA_EVENT_TIMEOUT_RTOS) {
        event_type = LORA_EVENT_TIMEOUT_RTOS;
    // } else if (*evt == LORA_EVENT_BEGIN) {
    //     event_type = LORA_EVENT_BEGIN;
    } else if (*evt == LORA_EVENT_SERIAL_CMD) {
        event_type = LORA_EVENT_SERIAL_CMD;
    }

    *evt = event_type;
    return true;
}

static void start_lora(void) {
	LoRaInit(); // Initialize LoRa
	int8_t txPowerInDbm = CONFIG_TX_POWER;
	uint32_t frequencyInHz = CONFIG_LORA_FREQUENCY_MHZ * 1000000;
	float tcxoVoltage = CONFIG_USE_TCXO*3.3; // use TCXO if enabled, otherwise set to 0.0
	bool useRegulatorLDO = CONFIG_USE_TCXO; // use DCDC + LDO if TCXO is enabled, otherwise use only LDO in all modes
	
	if (LoRaBegin(frequencyInHz, txPowerInDbm, tcxoVoltage, useRegulatorLDO) != 0) {
		ESP_LOGE(TAG, "Does not recognize the module");
		vTaskDelete(NULL);
	}
	bool invertIrq = false;
	#ifdef CONFIG_INVERT_IQ
		invertIrq = true*CONFIG_INVERT_IQ;
	#endif
	LoRaConfig(CONFIG_SF_RATE, CONFIG_BANDWIDTH, CONFIG_CODING_RATE, CONFIG_PREAMBLE_LENGTH, CONFIG_PAYLOAD_LENGTH, CONFIG_CRC_ON, invertIrq);

    // create the freeRTOS timer for deferral delays
    deferral_timer = xTimerCreate("deferral_timer", pdMS_TO_TICKS(DEFERRAL_TIME), pdFALSE, NULL, deferral_timer_callback);

    // create the freeRTOS timer for ACK timeouts
    ack_timeout_timer = xTimerCreate("ack_timeout_timer", pdMS_TO_TICKS(ACK_WAIT_TIME), pdFALSE, NULL, ack_timeout_timer_callback);

	ESP_LOGI(TAG, "Start");
}

void lora_state_machine_task(void *pvParameters) {

    lora_queue_item_t q_item;

    start_lora();

    while(1) {
        xQueueReceive(isr_queue, &q_item, portMAX_DELAY);
        process_event(&q_item.type); //parse IRQs depending on the trigger

        switch(q_item.type) {
            case LORA_EVENT_SERIAL_CMD:
                // Only accept a new command if we aren't currently tracking one
                if (!waiting_for_ack_node.is_active) {
                    setup_waiting_ack_node(&q_item.cmd_data);
                    lora_send_command(&q_item.cmd_data); //sending with mode sync so it only moves on after transmission
                    xTimerStart(waiting_for_ack_node.ack_timer, 0);
                    ESP_LOGI(TAG, "Sent command to node 0x%04X, waiting for ACK", q_item.cmd_data.dest_short_addr);
                } else {
                    // We are busy waiting for an ACK for a previous command.
                    defer_event(&q_item);
                }
                break;
            case LORA_EVENT_RX_DONE: {
                uplink_payload_t payload;
                if (parse_rx_message(rxbuf, rxlen, &payload)) {
                    if (payload.msg_type == MSG_TYPE_DATA) {
                        lora_send_ack(payload.short_addr);
                        report_data(&payload);
                    } else if (payload.msg_type == MSG_TYPE_ACK) {
                        if (is_valid_ack_for_waiting_node(payload.short_addr)) {
                            ESP_LOGI(TAG, "ACK received from 0x%04X", payload.short_addr);
                            clear_waiting_node();
                            //stay in listen continuously
                        } else {
                            ESP_LOGW(TAG, "ACK received from unexpected node 0x%04X", payload.short_addr);
                        }
                    } else {
                        ESP_LOGW(TAG, "Received unknown msg_type 0x%02X from 0x%04X", payload.msg_type, payload.short_addr);
                    }
                } else {
                        ESP_LOGW(TAG, "Received packet with invalid length: %d", rxlen);
                }
                break;
            }
            case LORA_EVENT_TIMEOUT_RTOS: 
                if (!waiting_for_ack_node.is_active){
                    //timeout but we were not waiting for something, error
                    break;
                }
                if (waiting_for_ack_node.retries < MAX_ACK_RETRIES) {
                    waiting_for_ack_node.retries++;
                    ESP_LOGW(TAG, "ACK Timeout! Retrying command to 0x%04X (retry %d/%d)", 
                                waiting_for_ack_node.target_addr, waiting_for_ack_node.retries, MAX_ACK_RETRIES);
                    lora_send_command(&waiting_for_ack_node.cmd);
                    xTimerStart(waiting_for_ack_node.ack_timer, 0);
                } else {
                    ESP_LOGE(TAG, "Failed to receive ACK from 0x%04X after %d retries", 
                                waiting_for_ack_node.target_addr, MAX_ACK_RETRIES);
                    clear_waiting_node(); // Failed to send command, keep listeinig
                }
                break;
            default:
                ESP_LOGW(TAG, "Event received in LISTEN_CONT state that is not handled: %d", q_item.type);
                break;
        }

    }
}

void start_lora_task(void *pvParameters) {
    //system_state = LORA_STATE_OFF;

    // Initialise DIO1 interrupt and queue
    isr_queue = xQueueCreate(10, sizeof(lora_queue_item_t));
    if (lora_int_init(isr_queue) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LoRa interrupt wrapper");
    }

    // lora_queue_item_t start_event;
    // start_event.type = LORA_EVENT_BEGIN;
    // xQueueSend(isr_queue, &start_event, portMAX_DELAY);

    xTaskCreate(lora_state_machine_task, "lora_state_machine_task", 4096, pvParameters, 5, NULL);
}