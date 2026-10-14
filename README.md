# WSN Coordinator

ESP-IDF firmware for an ESP32-C6 gateway that receives telemetry from wireless sensor nodes and forwards normalized reports to a host computer. The repository contains implementations for Zigbee, ESP-NOW, and LoRa/SX1262, plus USB Serial/JTAG reporting, optional SPI SD-card CSV logging, and a Python monitoring dashboard.

> **Current runtime configuration:** the checked-in `app_main.c` defines `ZIGBEE_COORDINATOR`, so a normal build starts the Zigbee task, USB Serial/JTAG tasks, and SD-card initialization. The ESP-NOW and LoRa implementations are present in the tree, but are not started by the default application path unless the startup configuration is changed.

## Repository layout

```text
.
├── main/
│   ├── app_main.c                 Startup, SPI bus, tasks, and diagnostics
│   ├── protocol.h                 Shared payload, report, and protocol definitions
│   ├── zigbee_task/               Zigbee coordinator and custom-cluster handling
│   ├── espnow/                    ESP-NOW receive, discovery, address mapping, and commands
│   ├── lora/                      SX1262 interrupt handling and LoRa gateway state machine
│   ├── serial/                    USB Serial/JTAG command input and report output
│   ├── sd_logger.c                Optional SPI SD-card CSV logger
│   └── led/                       Activity LED support
├── components/ra01s/              SX126x SPI driver and Kconfig options
├── gui_monitor.py                 CustomTkinter multi-port dashboard
├── monitor.py                     Text monitor for Zigbee log output
├── echo.py                        Simple USB Serial/JTAG echo test utility
├── partitions.csv                 Custom NVS, application, and Zigbee storage partitions
├── sdkconfig.defaults              Default ESP-IDF/Zigbee settings
└── CMakeLists.txt                 ESP-IDF project definition
```

## Hardware

The firmware is configured for:

- ESP32-C6 development board.
- A Zigbee sensor node compatible with the custom cluster described below.
- Optional SX1262/RA-01S LoRa hardware connected over SPI.
- Optional SPI microSD card. The SD card chip-select is hard-coded to GPIO 19.
- USB connection to the host computer through the ESP32-C6 USB Serial/JTAG interface.

The LoRa GPIOs and radio settings are configurable through `idf.py menuconfig`. The checked-in `sdkconfig` selects SPI2 and currently uses 868 MHz, SF9, bandwidth setting 3, coding rate 4/5, CRC enabled, TCXO enabled, and up to 22 dBm transmit power. Verify these settings and local radio regulations before using the hardware.

## Software requirements

- ESP-IDF **5.4.1** is the checked-in configuration; `main/idf_component.yml` requires ESP-IDF `>=5.0.0`.
- ESP-IDF Component Manager dependencies:
  - `espressif/esp-zboss-lib` `~1.6.0`
  - `espressif/esp-zigbee-lib` `~1.6.0`
- Python tools require `pyserial`; `gui_monitor.py` additionally requires `customtkinter`.

There is no `requirements.txt` in this repository, so install the Python packages manually, for example:

```bash
python -m pip install pyserial customtkinter
```

## Build and flash

From the repository root, with ESP-IDF exported:

```bash
idf.py set-target esp32c6
idf.py menuconfig
idf.py build
idf.py -p PORT flash monitor
```

For a clean Zigbee/NVS state, erase the flash before flashing:

```bash
idf.py -p PORT erase-flash
idf.py -p PORT build flash monitor
```

Use `idf.py menuconfig` to review the SX1262 pins/frequency, ESP-NOW settings, Zigbee settings, and the selected application startup path. The project uses a custom partition table from `partitions.csv` and a 4 MB flash configuration in the checked-in `sdkconfig`.

## Runtime startup

`app_main()` performs the following initialization:

1. Initializes NVS and the activity LED.
2. Initializes a shared SPI bus for the LoRa radio and SD card.
3. Attempts to mount the SD card and starts the CSV logger when mounting succeeds.
4. Starts the Zigbee coordinator because `ZIGBEE_COORDINATOR` is defined.
5. Starts the USB Serial/JTAG command task and serial report task.
6. A diagnostics task exists but is only started when `RUNTIME_TEST` is enabled at compile time.

The alternative startup branches in `app_main.c` can start ESP-NOW and LoRa, and a coexistence test branch can start all three wireless stacks, but those branches are disabled in the checked-in source.

## Zigbee coordinator

The Zigbee implementation is based on the ESP-Zigbee HA thermostat example, configured as a coordinator on primary channel 13 with up to 10 children. When a device announces itself, the coordinator searches for a temperature-measurement endpoint and requests bindings for the standard temperature cluster and custom cluster `0xFF01`.

The custom cluster attributes implemented in `main/zigbee_task/zigbee_task.c` are:

| Attribute | ID | Type | Stored field |
|---|---:|---|---|
| GNSS satellites | `0x0001` | U8 | `gnss_sats` |
| Latitude | `0x0002` | S32 | `latitude`, degrees × 1,000,000 |
| Longitude | `0x0003` | S32 | `longitude`, degrees × 1,000,000 |
| Current | `0x0004` | U16 | `current_ua` |
| Accelerometer X/Y/Z | `0x0005`–`0x0007` | U16 | `accel_x/y/z` |
| Set protocol | `0x0008` | defined but not parsed in reports | — |
| Sleep interval | `0x0009` | written by commands | — |
| Battery voltage | `0x000A` | U16 | `batt_level` |
| Temperature | `0x000B` | U16 | `temperature` |
| Start GNSS | `0x000C` | written by command | — |

Read responses and attribute reports update a per-node shadow table with a maximum of 32 Zigbee nodes. Each successfully parsed custom-cluster attribute generates a sensor report.

## ESP-NOW implementation

The ESP-NOW code uses the configured Wi-Fi channel, CRC16 validation, a receive queue, and a maximum address map of 32 nodes. It responds to broadcast discovery packets with `MSG_TYPE_DISCOVERY_ACK`, maps each node's 16-bit short address to its six-byte MAC address, accepts sensor uplinks, and routes them to the shared report queue. Host commands are converted to ESP-NOW downlink payloads and sent to known MAC mappings.

The checked-in configuration selects station mode, channel 1, long-range Wi-Fi enabled, and power save disabled. These settings are only used if the ESP-NOW startup path is enabled.

## LoRa/SX1262 implementation

`components/ra01s` provides the SX126x SPI driver. `main/lora` adds a GPIO DIO1 ISR that posts events to a FreeRTOS queue and a LoRa state-machine task that:

- Keeps the radio in continuous receive mode.
- Parses fixed-size `uplink_payload_t` frames.
- Sends an immediate `MSG_TYPE_ACK` for sensor-data uplinks.
- Reports valid sensor data with RSSI and uptime.
- Sends host commands synchronously and waits up to 500 ms for an ACK.
- Retries a command up to three times, deferring one additional serial command while waiting.

The shared SPI bus is initialized in `app_main.c`; the radio driver adds the SX1262 device at 9 MHz. The implementation does not contain the `s_lora_spi_mutex` described by the previous README.

## Shared data structures and serial protocol

The wire structures are packed C structs in `main/protocol.h` and `main/serial/usb_serial.h`.

### Sensor uplink: `uplink_payload_t`

The payload is 30 bytes, little-endian, and contains:

- 6-byte IEEE/MAC address.
- 16-bit short address.
- 8-bit message type.
- Battery level in mV.
- Temperature in the node's scaled integer representation.
- GNSS satellite count.
- Signed latitude and longitude in degrees × 1,000,000.
- Current and raw X/Y/Z accelerometer values.

### Gateway report: `serial_report_t`

Reports are written as 78-byte binary frames to USB Serial/JTAG:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Sync byte `0xAA` |
| 1 | 1 | Sync byte `0x55` |
| 2 | 1 | Payload length; sensor reports use `sizeof(uplink_payload_t)` |
| 3 | 1 | Protocol: `0` ESP-NOW, `1` Zigbee, `2` LoRa |
| 4 | 1 | Report type: `1` sensor data, `2` node status, `3` log message |
| 5 | 2 | Source short address |
| 7 | 1 | RSSI in dBm |
| 8 | 4 | Gateway uptime in milliseconds |
| 12 | 64 | Protocol-specific payload area |
| 76 | 2 | 16-bit sum of bytes 0–75 |

`serial_report_task()` calculates the checksum and sends the complete packed structure. Reports are queued with a depth of 20; dropped reports are counted in `g_dropped_serial_reports`.

### Host command: `serial_command_t`

Commands are 12-byte packed structures beginning with `0xAA 0x55`. The command task validates the sync bytes and routes commands by protocol:

| Command | Value |
|---|---:|
| `CHANGE_REPORT_CONFIG` | `0x00` |
| `READ_ATTRIBUTE` | `0x01` |
| `CHANGE_PROTOCOL` | `0x02` |
| `CHANGE_SLEEP_INTERVAL` | `0x03` |
| `START_GNSS` | `0x04` |

Zigbee commands issue ZCL read, write, or reporting-configuration requests. ESP-NOW and LoRa commands are converted to their respective downlink payloads.

## SD-card logging

When the SD card mounts successfully, `sd_logger.c` creates `/sdcard/log_N.csv`, where `N` increments in NVS under the `storage/boot_count` key. Sensor reports are queued to a 20-entry logger queue and written as CSV rows containing timestamp, node ID, protocol, RSSI, battery, temperature, GNSS, location, and accelerometer values. SD logging stops after three consecutive file or write failures.

## Python tools

### Dashboard

`gui_monitor.py` is a CustomTkinter dashboard that can read one or more serial ports, validate report checksums, display sensor data, send commands, and write local CSV/TXT logs.

```bash
python gui_monitor.py --wifi PORT --zigbee PORT --lora PORT --baudrate 115200
```

All protocol-port arguments are optional. If none are supplied, the script selects the last detected serial port and treats it as the ESP-NOW/Wi-Fi port. If the same physical port is supplied for multiple protocols, the dashboard shares the connection.

### Text monitor

`monitor.py` parses matching Zigbee attribute-report log lines:

```bash
python monitor.py -p PORT -b 115200
python monitor.py -p PORT -b 115200 --all
```

### Echo test

`echo.py` is a development utility with a hard-coded default port of `COM6`; edit `COM_PORT` before use:

```bash
python echo.py
```

## Known limitations and maintenance notes

- The default firmware startup is Zigbee-only despite the repository containing ESP-NOW and LoRa code.
- The GUI and firmware must use the same packed structure definitions. Check `gui_monitor.py` whenever `uplink_payload_t` or `serial_report_t` changes.
- `sdkconfig` is generated by ESP-IDF; use `idf.py menuconfig` rather than editing it manually.
- No automated test suite is included.
- The repository contains a checked-in `__pycache__` artifact; it is not required to build or run the project.
