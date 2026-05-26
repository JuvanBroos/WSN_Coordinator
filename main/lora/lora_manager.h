#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include "protocol.h"
#include "serial/usb_serial.h"

void lora_init(void);
void lora_task(void *pvParameters);
void lora_send_command(serial_command_t *cmd);

#endif // LORA_MANAGER_H