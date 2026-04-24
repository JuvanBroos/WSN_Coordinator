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

The C code intercepts these responses safely within the `esp_zb_task` thread. Attributes matching the `CUSTOM_CLUSTER_ID` (0xFF01) are explicitly parsed against defined IDs (`ATTR_GNSS_SATS`, `ATTR_LATITUDE`, `ATTR_CURRENT_UA`, etc.). The data payload is unpackaged based on its native Zigbee data type (e.g. `U8`, `U16`, `S16`, `S32`) and logged safely via serial.

## Python Monitoring Tools

Due to continuous data streams across multiple nodes, raw serial feeds can be overwhelming. To evaluate node telemetry effectively, two monitoring tools are included alongside the C project.

### Dependencies
Ensure you have the required Python dependencies installed:
```bash
pip install pyserial customtkinter
```

### 1. GUI Monitor (`gui_monitor.py`)
Provides a rich Graphical User Interface built on `customtkinter`. It dynamically generates tabular structures reflecting active remote nodes based on incoming serial logs, rendering real-time updates for:
- Sensor IDs and their network timestamps.
- Columnar visualization of custom attribute metrics continually updated upon serial reception.

**Usage:**
*(Replace `COMx` with your specific serial port)*
```bash
python gui_monitor.py -p COMx -b 115200
```
*(Note: Use `python gui_monitor.py --help` for possible arguments based on the implementation, but port bindings map manually in most systems)*

### 2. CLI Monitor (`monitor.py`)
Provides structured, command-line analytics without the overhead of a graphical interface. Use this tool for headless monitoring, debugging, or logging raw cluster payload updates textually.

**Usage:**
```bash
python monitor.py --port COMx --baud 115200
```

## Diagnostics and Troubleshooting

- **No Data in UI/Monitor**: Verify that the remote sensor nodes are properly commissioned. Look for `ZDO signal: NWK Device Associated (0x12)` via standard `idf.py monitor` logs to guarantee pairing.
- **Type Mismatch Warnings**: The C logic acts defensively. If an End Device sends an expected attribute but with an unexpected Zigbee data type (e.g., `S16` instead of `S32`), the Coordinator securely traps the error and throws a `type mismatch` warning instead of arbitrarily casting memory pointers. Verify the attribute definitions on the End Device source code.
- **Network Formation Fails**: Confirm ZBOSS library compatibility, perform `idf.py fullclean`, and erase NVRAM. Ensure the development board antenna is correctly connected.
