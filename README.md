# Wireless Sensor Network (WSN) Coordinator

This repository implements a robust Zigbee Coordinator for a Wireless Sensor Network (WSN) running on an ESP32-C6. Acting as the central gateway, it discovers, pairs, and receives telemetry data from remote sensor nodes using a custom Zigbee cluster (0xFF01).

## System Overview

Unlike standard Home Automation examples, this project is customized to continuously listen for structured telemetry updates from End Devices. These custom attributes map real-world embedded sensor data:
- **Location Data**: GNSS Satellite count, Latitude, Longitude.
- **Power Analytics**: Instantaneous Current (µA), Battery Voltage.
- **Inertial Data**: X, Y, Z axis readings from an accelerometer.
- **System Metrics**: Sleep intervals and active Protocol sets.

## Hardware Required

* 1x **ESP32-C6** Development Board (Target configured as the Coordinator Gateway).
* At least 1x Zigbee End Device running compatible firmware (broadcasting custom cluster attributes).
* USB connection to a host PC for monitoring and log parsing.

## Setup & Configuration

This project is built using ESP-IDF v5.0+ and relies on the Espressif ZBOSS and Zigbee SDK libraries (`~1.6.0`).

1. Set the correct target architecture for ESP32-C6:
   ```bash
   idf.py set-target esp32c6
   ```

2. Clean non-volatile storage (NVRAM) state. It is highly recommended to erase the flash to ensure there's no legacy networking data interfering with the formation of the new Zigbee network:
   ```bash
   idf.py -p PORT erase-flash
   ```

3. Build and flash the firmware:
   ```bash
   idf.py -p PORT build flash
   ```

## Application Flow & Data Acquisition

Upon boot, the Coordinator initializes the Zigbee stack, operates as a Trust Center, and forms an open PAN network. When remote end devices successfully associate and bind to the Coordinator, they begin reporting scheduled telemetry data.

The application logic intercepts these responses safely within the `esp_zb_task` thread. Attributes matching the `CUSTOM_CLUSTER_ID` (0xFF01) are explicitly parsed against defined IDs (`ATTR_GNSS_SATS`, `ATTR_LATITUDE`, `ATTR_CURRENT_UA`, etc.). The data payload is unpackaged based on its native Zigbee data type (e.g. `U8`, `U16`, `S16`, `S32`) and logged safely via serial.

## Multi-Protocol Support

The Gateway acts as a central hub for multiple wireless protocols, standardizing incoming data into a unified binary format for the host PC.

### 1. Zigbee Coordinator
The gateway forms a Zigbee network and listens for telemetry updates from remote sensor nodes using a custom Zigbee cluster (**0xFF01**). It maintains a "Shadow Table" of active nodes, aggregating individual attribute updates into complete state reports.

### 2. ESP-NOW Coordinator
The gateway also acts as an **Active ESP-NOW Coordinator**:
- **Discovery Responder**: Automatically replies to node discovery broadcasts with a unicast Discovery ACK.
- **Address Mapping**: Dynamically maps 16-bit virtual IDs (derived from MAC addresses) to full 48-bit hardware MACs, allowing the host PC to target ESP-NOW nodes using consistent 16-bit addressing.
- **Active Monitoring**: Receives unicast telemetry bursts and instantly routes them to the reporting system.

## Unified Serial Reporting System

To ensure reliable data delivery to the host PC, the gateway utilizes a **Dedicated Reporting Task**. All incoming data (regardless of protocol) is packed into a standardized binary frame before transmission over the USB Serial JTAG interface.

### Binary Frame Format (`serial_report_t`)
| Offset | Field | Type | Description |
| :--- | :--- | :--- | :--- |
| 0 | Sync1 | uint8 | Always `0xAA` |
| 1 | Sync2 | uint8 | Always `0x55` |
| 2 | Length | uint8 | Payload length |
| 3 | Protocol | uint8 | 0=ESP-NOW, 1=Zigbee, 2=LoRa |
| 4 | Type | uint8 | 1=Sensor Data, 2=Status, 3=Log |
| 5 | Src Addr | uint16 | 16-bit Source identifier |
| 7 | RSSI | int8 | Signal strength in dBm |
| 8 | Timestamp| uint32 | Gateway uptime in ms |
| 12 | Payload | uint8[64]| Protocol-specific data |
| 76 | Checksum | uint16 | Simple sum of all preceding bytes |

## Serial Command System

The gateway accepts structured binary commands from the host PC to control remote nodes. Commands are protocol-aware and routed based on the `protocol` field.

### Command Structure (`serial_command_t`)
Commands follow the `0xAA 0x55` framing and include a protocol selector (`0` for ESP-NOW, `1` for Zigbee, `2` for LoRa) to ensure the gateway uses the correct radio and addressing scheme. Supported commands include:
- `CHANGE_PROTOCOL (0x02)`: Instructs the target node to gracefully switch to a new wireless protocol.
- `CHANGE_SLEEP_INTERVAL (0x03)`: Instructs the target node to update its transmission delay dynamically without rebooting.

## Python Dashboard (`gui_monitor.py`)

The project includes a high-performance **Multi-Protocol Gateway Dashboard** built with `customtkinter`. It provides a real-time visualization of the entire sensor network.

### Features
- **Split-Screen Layout**: Top panel shows a live **Sensor Data Table**; bottom panel shows a **Live Log Stream**; the right panel features a **Command Execution Form**.
- **Remote Commands**: Easily dispatch configuration changes (like Sleep Interval or Active Protocol) to specific nodes by entering their Hex ID in the right-hand panel. The GUI packs this into a binary `serial_command_t` and routes it via the gateway.
- **Dynamic Node Discovery**: Automatically adds rows as new nodes (Zigbee, LoRa, or ESP-NOW) check in.
- **High Fidelity**: Decodes binary `serial_report_t` frames for precise data rendering.

**Usage:**
```bash
python gui_monitor.py -p <SERIAL_PORT> -b 115200
```

