#include "ra01s_int.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "RA01S_INT";
static QueueHandle_t s_isr_queue = NULL;

static void IRAM_ATTR lora_gpio_isr_handler(void *arg) {
    if (s_isr_queue) {
        lora_event_t evt = LORA_EVENT_DIO1_INTR;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(s_isr_queue, &evt, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

esp_err_t lora_int_init(QueueHandle_t isr_queue) {
    if (isr_queue == NULL) {
        ESP_LOGE(TAG, "Queue cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    s_isr_queue = isr_queue;

#ifndef CONFIG_DIO1_GPIO
    ESP_LOGE(TAG, "CONFIG_DIO1_GPIO is not defined in Kconfig!");
    return ESP_ERR_INVALID_ARG;
#else
    int dio1_pin = CONFIG_DIO1_GPIO;
    if (dio1_pin < 0) {
        ESP_LOGE(TAG, "DIO1_GPIO is not configured correctly in menuconfig (-1)");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing DIO1 interrupt on GPIO %d", dio1_pin);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << dio1_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    // Install gpio isr service. Ignore ESP_ERR_INVALID_STATE as it means it's already installed.
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service");
        return err;
    }

    err = gpio_isr_handler_add(dio1_pin, lora_gpio_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler for pin %d", dio1_pin);
    }
    return err;
#endif
}
