#pragma once

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "ra01s.h"
#include "serial/usb_serial.h"
#include "lora_interrupt.h"

typedef enum {
    LORA_STATE_OFF,
    LORA_STATE_LISTEN_CONT,
    LORA_STATE_TX_ACK,
    LORA_STATE_TX_CMD,
} lora_state_t; 

typedef struct {
	bool is_active;           // Are we currently expecting an ACK?
	uint16_t target_addr;     // Who are we expecting the ACK from?
	uint8_t retries;          // How many times have we tried?
	serial_command_t cmd;     // The payload we are retrying
	TimerHandle_t ack_timer;  // The FreeRTOS timer
} waiting_for_ack_t;

#define ACK_WAIT_TIME 500 // Time to wait for an ACK after sending 
#define DEFERRAL_TIME 10

#define MAX_ACK_RETRIES 3 // Max number of times to retry sending a message if ACK is not received

void start_lora_task(void *pvParameters);
void lora_queue_command(serial_command_t *cmd);