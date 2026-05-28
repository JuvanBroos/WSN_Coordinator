#pragma once
#include <stdint.h>

// --- Magic Bytes for Serial UART Framing ---
#define SERIAL_SYNC_1 0xAA
#define SERIAL_SYNC_2 0x55

// --- Protocol & Message Identifiers ---
// #define PROTO_WIFI   0x01
// #define PROTO_ZIGBEE 0x02
// #define PROTO_LORA   0x03

#define MSG_TYPE_DATA   0x10
#define MSG_TYPE_CONFIG 0x20
#define MSG_TYPE_DISCOVERY_ACK 0x30
#define MSG_TYPE_READ_REQ 0x40
#define MSG_TYPE_ACK    0x50

#define BROADCAST_ADDR  0xFFFF

typedef enum {
    PROTO_WIFI,
    PROTO_ZIGBEE,
    PROTO_LORA,
}protocol_to_run_t;

#define PROTOCOL_NO_CHANGE 0xFF

// --------------------------------------------------------
// UPLINK PAYLOAD (End Node -> Gateway -> Pi)
// --------------------------------------------------------
typedef struct __attribute__((packed)) {
    // Addressing (10 bytes)
    uint8_t  ieee_addr[6];  // 64-bit MAC address
    uint16_t short_addr;    // 16-bit Network address
    
    // System State (3 bytes)
    uint8_t  msg_type;      // Always MSG_TYPE_DATA (0x10)
    //uint8_t  active_proto;  // PROTO_WIFI, ZIGBEE, or LORA
    uint16_t  batt_level;    // Battery level in mV
    uint16_t  temperature;   // Temperature in Celsius (scaled by 100)
    
    // Sensor Data: GNSS (9 bytes)
    uint8_t  gnss_sats;     
    int32_t  latitude;      // Lat * 1,000,000 
    int32_t  longitude;     // Lon * 1,000,000 
    
    // Sensor Data: Environmental & Motion (8 bytes)
    uint16_t current_ua;    // Current draw
    uint16_t  accel_x;       // Raw X axis
    uint16_t  accel_y;       // Raw Y axis
    uint16_t  accel_z;       // Raw Z axis
} uplink_payload_t; 


// --------------------------------------------------------
// DOWNLINK PAYLOAD (Pi -> Gateway -> End Node)
// Total Size: 6 Bytes
// --------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint16_t target_addr;   // The 16-bit network address of the node to change
    uint8_t  msg_type;      // Always MSG_TYPE_CONFIG (0x20)
    uint8_t  set_protocol;  // PROTO_WIFI (0x01), ZIGBEE (0x02), or LORA (0x03)
    uint16_t sleep_interval;// Time between reports in seconds
} downlink_payload_t;


#define MAX_REPORT_PAYLOAD_LEN 64

typedef enum {
    REPORT_TYPE_SENSOR_DATA = 0x01,
    REPORT_TYPE_NODE_STATUS = 0x02,
    REPORT_TYPE_LOG_MESSAGE = 0x03,
} serial_report_type_t;

typedef struct __attribute__((packed)) {
    uint8_t sync1;          // 0xAA
    uint8_t sync2;          // 0x55
    uint8_t length;         // Total size of the payload following metadata
    uint8_t protocol;       // From protocol_to_run_t
    uint8_t report_type;    // From serial_report_type_t
    uint16_t src_short_addr;
    int8_t rssi;            // Signal strength
    uint32_t timestamp;     // Gateway uptime in ms
    uint8_t payload[MAX_REPORT_PAYLOAD_LEN]; 
    uint16_t checksum;      // CRC or simple sum
} serial_report_t;

// --------------------------------------------------------
// SERIAL UART FRAME HEADER (Gateway <-> Pi only)
// Total Size: 3 Bytes (Checksum is appended after payload)
// --------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t sync1;       // Always 0xAA
    uint8_t sync2;       // Always 0x55
    uint8_t length;      // Size of the payload following this header
} serial_header_t;