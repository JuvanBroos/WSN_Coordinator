#ifndef RA01S_INT_H
#define RA01S_INT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// The DIO1 hardware pin triggers on multiple events (RX_DONE, TX_DONE, etc.).
// The ISR simply wakes the task, and the task must call GetIrqStatus() over SPI
// to determine the exact cause.
typedef enum {
    LORA_EVENT_DIO1_INTR
} lora_event_t;

/**
 * @brief Initializes the DIO1 GPIO pin as an interrupt and binds it to the provided queue.
 * @param isr_queue Queue to send events to from the ISR. Must be created before calling this.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t lora_int_init(QueueHandle_t isr_queue);

#endif // RA01S_INT_H
