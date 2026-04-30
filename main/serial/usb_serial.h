#pragma once

#include <stdint.h>

#define SERIAL_SYNC_1 0xAA
#define SERIAL_SYNC_2 0x55
#define MAX_PAYLOAD_LEN 256

typedef enum {
    CHANGE_REPORT_CONFIG = 0x00,
    READ_ATTRIBUTE = 0x01,
    CHANGE_PROTOCOL = 0x02,
    PLACEHOLDER_1 = 0x04,
} serial_command_type_t;

typedef enum {
    PLACEHOLDER = 0x00,
    MIN_TIME = 0x01,
    MAX_TIME = 0x02,
    REPORTABLE_CHANGE = 0x03,
} serial_report_config_options_t;


#define PROTO_WIFI 0
#define PROTO_ZIGBEE 1
#define PROTO_LORA 2


typedef struct serial_command_s {
    uint8_t sync1; // Should be SERIAL_SYNC_1
    uint8_t sync2; // Should be SERIAL_SYNC_2
    uint16_t dest_short_addr; // Short address of the target device for this command
    uint8_t command_type;
    uint8_t report_config_option; // Only valid if command_type is CHANGE_REPORT_CONFIG
    uint16_t command_value_1_min;
    uint16_t command_value_2_max;
    union {
        uint16_t report_config_delta;
        uint16_t read_attribute_id;
        uint16_t protocol;
    }command_value_3;

} serial_command_t;// Struct of command that is sent by the host over USB Serial JTAG

void usb_serial_jtag_task(void *arg);