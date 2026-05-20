#include "espnow.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "protocol.h"
//#include "aggregator.h"

#define ESPNOW_MAXDELAY 512

static const char *TAG = "ESPNOW";

static QueueHandle_t espnow_queue = NULL;
static espnow_send_param_t *s_send_param = NULL;

static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

typedef struct {
    uint16_t short_addr;
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
} espnow_addr_map_t;

#define MAX_ESPNOW_NODES 32
static espnow_addr_map_t addr_map[MAX_ESPNOW_NODES];
static int addr_map_count = 0;

static void update_addr_map(uint16_t short_addr, const uint8_t *mac_addr) {
    for (int i = 0; i < addr_map_count; i++) {
        if (addr_map[i].short_addr == short_addr) {
            memcpy(addr_map[i].mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
            return;
        }
    }
    if (addr_map_count < MAX_ESPNOW_NODES) {
        addr_map[addr_map_count].short_addr = short_addr;
        memcpy(addr_map[addr_map_count].mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
        ESP_LOGI(TAG, "Mapped Short Addr 0x%04x to MAC "MACSTR, short_addr, MAC2STR(mac_addr));
        addr_map_count++;
    }
}

static bool get_mac_by_short(uint16_t short_addr, uint8_t *mac_addr) {
    for (int i = 0; i < addr_map_count; i++) {
        if (addr_map[i].short_addr == short_addr) {
            memcpy(mac_addr, addr_map[i].mac_addr, ESP_NOW_ETH_ALEN);
            return true;
        }
    }
    return false;
}

void espnow_send_command(serial_command_t cmd) {
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
    if (!get_mac_by_short(cmd.dest_short_addr, dest_mac)) {
        ESP_LOGE(TAG, "No MAC mapping found for short addr 0x%04x", cmd.dest_short_addr);
        return;
    }

    size_t payload_len = sizeof(espnow_data_t) + sizeof(downlink_payload_t);
    uint8_t *buf = malloc(payload_len);
    if (!buf) return;

    espnow_data_t *espnow_data = (espnow_data_t *)buf;
    downlink_payload_t *dl_payload = (downlink_payload_t *)espnow_data->payload;

    dl_payload->target_addr = cmd.dest_short_addr;
    dl_payload->msg_type = MSG_TYPE_CONFIG;
    
    if (cmd.command_type == CHANGE_PROTOCOL) {
        dl_payload->set_protocol = (uint8_t)cmd.command_value_3.protocol;
        dl_payload->sleep_interval = 0; // Default or from command
    } else {
        // Handle other commands or set defaults
        dl_payload->set_protocol = 0;
        dl_payload->sleep_interval = 60; // Example
    }

    espnow_data->crc = 0;
    espnow_data->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)espnow_data, payload_len);

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = CONFIG_ESPNOW_CHANNEL;
    peer.ifidx = ESPNOW_WIFI_IF;
    peer.encrypt = false;
    memcpy(peer.peer_addr, dest_mac, ESP_NOW_ETH_ALEN);
    if (!esp_now_is_peer_exist(dest_mac)) {
        esp_now_add_peer(&peer);
    }

    esp_err_t ret = esp_now_send(dest_mac, buf, payload_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send ESP-NOW command: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent ESP-NOW command to 0x%04x", cmd.dest_short_addr);
    }
    free(buf);
}

#define IS_BROADCAST_ADDR(addr) (memcmp(addr, broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

/* WiFi should start before using ESPNOW */
void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );
    ESP_ERROR_CHECK( esp_wifi_start());
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
#endif
}

void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    espnow_event_t evt;
    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    espnow_event_t evt;
    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t * mac_addr = recv_info->src_addr;
    uint8_t * des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (IS_BROADCAST_ADDR(des_addr)) {
        ESP_LOGD(TAG, "Receive broadcast ESPNOW data");
    } else {
        ESP_LOGD(TAG, "Receive unicast ESPNOW data");
    }

    evt.id = ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->is_broadcast = IS_BROADCAST_ADDR(des_addr);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* Parse received ESPNOW data. */
int espnow_data_parse(uint8_t *data, uint16_t data_len)
{
    espnow_data_t *buf = (espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return 1;
    }
    return -1;
}

/* Prepare ESPNOW data to be sent. */
void espnow_data_prepare(espnow_send_param_t *send_param)
{
    espnow_data_t *buf = (espnow_data_t *)send_param->buffer; 
    assert(send_param->len >= sizeof(espnow_data_t));
    buf->crc = 0;

    // Fill the payload with real data to be sent
    uplink_payload_t sensor_data = {0};
    memcpy(buf->payload, &sensor_data, sizeof(uplink_payload_t));
    
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

esp_err_t espnow_init(void)
{
    espnow_send_param_t *send_param;

    espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
    if (espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create queue fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );
#if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK( esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW) );
    ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) );
#endif
    /* Set primary master key. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vQueueDelete(espnow_queue);
        espnow_queue = NULL;
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    /* Initialize sending parameters. */
    send_param = malloc(sizeof(espnow_send_param_t));
    if (send_param == NULL) {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vQueueDelete(espnow_queue);
        espnow_queue = NULL;
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(send_param, 0, sizeof(espnow_send_param_t));
    send_param->count = CONFIG_ESPNOW_SEND_COUNT;
    send_param->delay = CONFIG_ESPNOW_SEND_DELAY;
    send_param->len = sizeof(uplink_payload_t) + sizeof(espnow_data_t);
    send_param->buffer = malloc(send_param->len);
    if (send_param->buffer == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(send_param);
        vQueueDelete(espnow_queue);
        espnow_queue = NULL;
        esp_now_deinit();
        return ESP_FAIL;
    }
    memcpy(send_param->dest_mac, broadcast_mac, ESP_NOW_ETH_ALEN);

    s_send_param = send_param; // Store globally for deinitialization

    xTaskCreate(espnow_task, "espnow_task", 2048, send_param, 4, NULL);
    return ESP_OK;
}

void espnow_deinit(void)
{
    if (s_send_param) {
        if (s_send_param->buffer) {
            free(s_send_param->buffer);
        }
        free(s_send_param);
        s_send_param = NULL;
    }
    if (espnow_queue) {
        vQueueDelete(espnow_queue);
        espnow_queue = NULL;
    }
    esp_now_deinit();
}

void espnow_task(void *pvParameter)
{
    espnow_event_t evt;
    int ret;

    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Start sending broadcast data");


    /* Start sending broadcast ESPNOW data. */
    espnow_send_param_t *send_param = (espnow_send_param_t *)pvParameter;

    espnow_data_prepare(send_param);

    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
        ESP_LOGE(TAG, "Send error");
        espnow_deinit();
        vTaskDelete(NULL);
    } else {
        ESP_LOGI(TAG, "Sent broadcast data");
        //vTaskDelete(NULL);
    }

    while (xQueueReceive(espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
        switch (evt.id) {
            case ESPNOW_SEND_CB:
            {
                ESP_LOGI(TAG, "Send callback, status: %d", evt.info.send_cb.status);
                break;
            }
            case ESPNOW_RECV_CB:
            {
                //ret = espnow_data_parse(evt.info.recv_cb.data, evt.info.recv_cb.data_len, &recv_state, &recv_seq, &recv_magic);
                ret = espnow_data_parse(evt.info.recv_cb.data, evt.info.recv_cb.data_len);
                ESP_LOGI(TAG, "Received data from "MACSTR" ", MAC2STR(evt.info.recv_cb.mac_addr));

                espnow_data_t *recv_buf = (espnow_data_t *)evt.info.recv_cb.data;
                size_t payload_len = evt.info.recv_cb.data_len - sizeof(espnow_data_t);
                
                if (evt.info.recv_cb.is_broadcast) {
                    ESP_LOGI(TAG, "Replying to broadcast discovery from "MACSTR, MAC2STR(evt.info.recv_cb.mac_addr));
                    
                    esp_now_peer_info_t peer;
                    memset(&peer, 0, sizeof(esp_now_peer_info_t));
                    peer.channel = CONFIG_ESPNOW_CHANNEL;
                    peer.ifidx = ESPNOW_WIFI_IF;
                    peer.encrypt = false;
                    memcpy(peer.peer_addr, evt.info.recv_cb.mac_addr, ESP_NOW_ETH_ALEN);
                    
                    if (!esp_now_is_peer_exist(peer.peer_addr)) {
                        esp_now_add_peer(&peer);
                    }
                    
                    size_t reply_len = sizeof(espnow_data_t) + sizeof(downlink_payload_t);
                    uint8_t *reply_buf = malloc(reply_len);
                    if (reply_buf) {
                        espnow_data_t *buf = (espnow_data_t *)reply_buf;
                        buf->crc = 0;
                        downlink_payload_t *dl_payload = (downlink_payload_t *)buf->payload;
                        memset(dl_payload, 0, sizeof(downlink_payload_t));
                        dl_payload->msg_type = MSG_TYPE_DISCOVERY_ACK; 
                        buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, reply_len);
                        
                        esp_now_send(evt.info.recv_cb.mac_addr, reply_buf, reply_len);
                        free(reply_buf);
                    }
                }

                if (payload_len == sizeof(downlink_payload_t)) {
                    downlink_payload_t *dl_payload = (downlink_payload_t *)recv_buf->payload;
                    if (dl_payload->msg_type == MSG_TYPE_CONFIG) {
                        ESP_LOGI(TAG, "Received MSG_TYPE_CONFIG: protocol=%d, sleep=%d", dl_payload->set_protocol, dl_payload->sleep_interval);
                        // agg_message_t cmd_msg;
                        // memset(&cmd_msg, 0, sizeof(agg_message_t));
                        // cmd_msg.event_type = EVENT_RECEIVED_COMMAND;
                        // cmd_msg.data.command.msg_type = dl_payload->msg_type;
                        // cmd_msg.data.command.set_protocol = dl_payload->set_protocol;
                        // cmd_msg.data.command.sleep_interval = dl_payload->sleep_interval;
                        // payload_manager_send_update(&cmd_msg);
                    }
                } else if (payload_len == sizeof(uplink_payload_t)) {
                    uplink_payload_t *payload = (uplink_payload_t *)recv_buf->payload;
                    ESP_LOGI(TAG, "Payload Data - ShortAddr: 0x%04x, Temp: %d, Accel X: %d, Y: %d, Z: %d, Curr: %d, Batt: %d", 
                                payload->short_addr, payload->temperature, payload->accel_x, payload->accel_y, payload->accel_z, payload->current_ua, payload->batt_level);
                    
                    // Update address map
                    update_addr_map(payload->short_addr, evt.info.recv_cb.mac_addr);
                } else {
                    ESP_LOGW(TAG, "Received ESP-NOW packet of unknown payload length: %d", payload_len);
                }

                free(evt.info.recv_cb.data);
                break;
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}