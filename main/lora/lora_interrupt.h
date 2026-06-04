#ifndef LORA_INTERRUPT_H
#define LORA_INTERRUPT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "serial/usb_serial.h"

// The DIO1 hardware pin triggers on multiple events (RX_DONE, TX_DONE, etc.).
// The ISR simply wakes the task, and the task must call GetIrqStatus() over SPI
// to determine the exact cause.
typedef enum {
    LORA_EVENT_DIO1_INTR,
    //LORA_EVENT_BEGIN,
    LORA_EVENT_RX_DONE,
    LORA_EVENT_TX_DONE,
    LORA_EVENT_TIMEOUT_RTOS, // The retry timer
    LORA_EVENT_SERIAL_CMD,   // NEW: A command from the GUI
 } lora_event_type_t;

 typedef struct {
    lora_event_type_t type;
    serial_command_t cmd_data; // Only populated if type == LORA_EVENT_SERIAL_CMD
} lora_queue_item_t;


/**
 * @brief Initializes the DIO1 GPIO pin as an interrupt and binds it to the provided queue.
 * @param isr_queue Queue to send events to from the ISR. Must be created before calling this.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t lora_int_init(QueueHandle_t isr_queue);

#endif // LORA_INTERRUPT_H