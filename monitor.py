import serial
import re
import argparse
import time
from collections import defaultdict

def main():
    parser = argparse.ArgumentParser(description="Monitor ESP32 Zigbee Coordinator logs")
    parser.add_argument("-p", "--port", required=True, help="COM port (e.g., COM3, /dev/ttyUSB0)")
    parser.add_argument("-b", "--baud", default=115200, type=int, help="Baud rate (default: 115200)")
    parser.add_argument("-a", "--all", action="store_true", help="Print all logs, not just reports")
    args = parser.parse_args()

    # Dictionary to keep track of sensor data
    # Structure: { "0x1234": { "last_seen": timestamp, "attributes": { "Cluster:0x... Attr:0x...": "Type:0x..." } } }
    sensors = defaultdict(lambda: {"last_seen": 0, "attributes": {}})

    # Regex to match: Report src(0x1234 ep:1) -> dst(ep:1), cluster(0x0402), attr(0x0000), type(0x29)
    report_pattern = re.compile(
        r"Report src\(0x([0-9a-fA-F]+)\s+ep:(\d+)\)\s+->\s+dst\(ep:(\d+)\),\s+cluster\(0x([0-9a-fA-F]+)\),\s+attr\(0x([0-9a-fA-F]+)\),\s+type\(0x([0-9a-fA-F]+)\)"
    )

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
        print(f"Listening on {args.port} at {args.baud} baud...\n")
    except Exception as e:
        print(f"Error opening serial port {args.port}: {e}")
        return

    try:
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue

            match = report_pattern.search(line)
            if match:
                src_addr = f"0x{match.group(1).upper()}"
                src_ep = match.group(2)
                dst_ep = match.group(3)
                cluster = f"0x{match.group(4).upper()}"
                attr = f"0x{match.group(5).upper()}"
                attr_type = f"0x{match.group(6).upper()}"

                # Update sensor state
                sensors[src_addr]["last_seen"] = time.time()
                attr_key = f"Ep:{src_ep} | Cluster:{cluster} | Attr:{attr}"
                sensors[src_addr]["attributes"][attr_key] = attr_type

                # Clear screen or just print separated block
                print("\n" + "="*60)
                print(f"🔔 UPDATE FOR SENSOR: {src_addr}")
                print("="*60)
                
                # Print summary of all known sensors
                for sensor, data in sensors.items():
                    time_str = time.strftime('%H:%M:%S', time.localtime(data['last_seen']))
                    print(f"📍 Sensor [{sensor}]  (Last report: {time_str})")
                    for a_key, a_type in data["attributes"].items():
                        print(f"    └─ {a_key} => Type: {a_type}")
                print("="*60)
                
            elif args.all:
                # Print non-matching lines if requested
                print(line)

    except KeyboardInterrupt:
        print("\nExiting monitor...")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
