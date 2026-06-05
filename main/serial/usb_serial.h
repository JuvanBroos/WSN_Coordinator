#pragma once

#include <stdint.h>
#include "protocol.h"

#define SERIAL_SYNC_1 0xAA
#define SERIAL_SYNC_2 0x55
#define MAX_PAYLOAD_LEN 256

typedef enum {
    CHANGE_REPORT_CONFIG = 0x00,
    READ_ATTRIBUTE = 0x01,
    CHANGE_PROTOCOL = 0x02,
    CHANGE_SLEEP_INTERVAL = 0x03,
    START_GNSS = 0x04,
} serial_command_type_t;

typedef struct __attribute__((packed)) serial_command_s {
    uint8_t sync1;            // Should be SERIAL_SYNC_1
    uint8_t sync2;            // Should be SERIAL_SYNC_2
    uint8_t protocol;         // From protocol_to_run_t
    uint8_t command_type;     // From serial_command_type_t
    uint16_t dest_short_addr; // Short address of the target device for this command
    uint16_t command_value_1_min;
    uint16_t command_value_2_max;
    union {
        uint16_t report_config_delta;
        uint16_t read_attribute_id;
        uint16_t protocol;
        uint16_t sleep_interval;
    }command_value_3;
} serial_command_t;// Struct of command that is sent by the host over USB Serial JTAG

void usb_serial_jtag_task(void *arg);
void serial_report_task(void *arg);
void gateway_send_report(serial_report_t *report);