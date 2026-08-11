import customtkinter as ctk
from tkinter import ttk
import serial
import serial.tools.list_ports
import threading
import time
import sys
import argparse
import struct
import os
import csv
from datetime import datetime

# Protocol Enums (must match protocol.h)
PROTO_WIFI = 0
PROTO_ZIGBEE = 1
PROTO_LORA = 2

REPORT_TYPE_SENSOR_DATA = 0x01
REPORT_TYPE_NODE_STATUS = 0x02
REPORT_TYPE_LOG_MESSAGE = 0x03

# serial_report_t format: <BBBBBHbI64sH
# < : Little-endian
# B : sync1 (uint8)
# B : sync2 (uint8)
# B : length (uint8)
# B : protocol (uint8)
# B : report_type (uint8)
# H : src_short_addr (uint16)
# b : rssi (int8)
# I : timestamp (uint32)
# 64s : payload (uint8[64])
# H : checksum (uint16)
REPORT_STRUCT_FMT = '<BBBBBHbI64sH'
REPORT_SIZE = struct.calcsize(REPORT_STRUCT_FMT)

# uplink_payload_t format: <6sHBHHBiiHHHH
# 6s : ieee_addr (uint8[6])
# H  : short_addr (uint16)
# B  : msg_type (uint8)
# H  : batt_level (uint16)
# H  : temperature (uint16)
# B  : gnss_sats (uint8)
# i  : latitude (int32)
# i  : longitude (int32)
# H  : tx_count (uint16)
# H  : accel_x (uint16)
# H  : accel_y (uint16)
# H  : accel_z (uint16)
UPLINK_PAYLOAD_FMT = '<6sHBHHBiiHHHH'
UPLINK_PAYLOAD_SIZE = struct.calcsize(UPLINK_PAYLOAD_FMT)

# serial_command_t format: <BBBBHHHH
# < : Little-endian
# B : sync1 (0xAA)
# B : sync2 (0x55)
# B : protocol
# B : command_type
# H : dest_short_addr
# H : command_value_1_min
# H : command_value_2_max
# H : command_value_3 (union: protocol or sleep_interval)
COMMAND_STRUCT_FMT = '<BBBBHHHH'
COMMAND_SIZE = struct.calcsize(COMMAND_STRUCT_FMT)

class GatewayMonitorApp(ctk.CTk):
    def __init__(self, ports_config, baudrate):
        super().__init__()

        self.title("Multi-Protocol Gateway Dashboard")
        self.geometry("1200x800")
        
        # Configure grid
        self.grid_columnconfigure(0, weight=3)
        self.grid_columnconfigure(1, weight=1) # Right: Command Panel
        self.grid_rowconfigure(0, weight=1) # Top: Table
        self.grid_rowconfigure(1, weight=1) # Bottom: Logs

        # --- Top Panel: Sensor Table ---
        self.table_frame = ctk.CTkFrame(self)
        self.table_frame.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")
        
        self.table_label = ctk.CTkLabel(self.table_frame, text="Active Sensor Nodes", font=ctk.CTkFont(size=16, weight="bold"))
        self.table_label.pack(pady=5)

        # Treeview for Table
        style = ttk.Style()
        style.theme_use("default")
        style.configure("Treeview", background="#2b2b2b", foreground="white", fieldbackground="#2b2b2b", borderwidth=0)
        style.map("Treeview", background=[('selected', '#347083')])

        self.tree = ttk.Treeview(self.table_frame, columns=(
            "ID", "Proto", "RSSI", "Tx Count", "Batt", "Temp", "GNSS", "Lat", "Lon", "Accel X/Y/Z", "Last Seen"
        ), show="headings")
        
        self.tree.heading("ID", text="Node ID")
        self.tree.heading("Proto", text="Protocol")
        self.tree.heading("RSSI", text="RSSI")
        self.tree.heading("Tx Count", text="Tx Count")
        self.tree.heading("Batt", text="Batt (mV)")
        self.tree.heading("Temp", text="Temp (°C)")
        self.tree.heading("GNSS", text="Sats")
        self.tree.heading("Lat", text="Latitude")
        self.tree.heading("Lon", text="Longitude")
        self.tree.heading("Accel X/Y/Z", text="Accel X/Y/Z")
        self.tree.heading("Last Seen", text="Last Seen")

        for col in self.tree["columns"]:
            self.tree.column(col, width=100, anchor="center")
        
        self.tree.pack(expand=True, fill="both", padx=5, pady=5)

        # --- Bottom Panel: Log Stream ---
        self.log_frame = ctk.CTkFrame(self)
        self.log_frame.grid(row=1, column=0, padx=10, pady=10, sticky="nsew")

        self.log_label = ctk.CTkLabel(self.log_frame, text="Live Log Stream", font=ctk.CTkFont(size=16, weight="bold"))
        self.log_label.pack(pady=5)

        self.log_textbox = ctk.CTkTextbox(self.log_frame, state="disabled", wrap="word", font=ctk.CTkFont(family="Consolas", size=12))
        self.log_textbox.pack(expand=True, fill="both", padx=5, pady=5)

        # --- Right Panel: Command Form ---
        self.command_frame = ctk.CTkFrame(self)
        self.command_frame.grid(row=0, column=1, rowspan=2, padx=10, pady=10, sticky="nsew")
        
        self.command_label = ctk.CTkLabel(self.command_frame, text="Send Command", font=ctk.CTkFont(size=16, weight="bold"))
        self.command_label.pack(pady=10)

        # Target Address
        self.target_addr_label = ctk.CTkLabel(self.command_frame, text="Target Address (Hex):")
        self.target_addr_label.pack(pady=(10, 0))
        self.target_addr_entry = ctk.CTkEntry(self.command_frame, placeholder_text="e.g. FFFF")
        self.target_addr_entry.pack(pady=5)
        
        # Command Type
        self.cmd_type_label = ctk.CTkLabel(self.command_frame, text="Command Type:")
        self.cmd_type_label.pack(pady=(10, 0))
        self.cmd_type_var = ctk.StringVar(value="Change Protocol")
        self.cmd_type_combo = ctk.CTkOptionMenu(self.command_frame, variable=self.cmd_type_var, values=["Change Protocol", "Change Sleep Interval", "Read Attributes", "Trigger GNSS"])
        self.cmd_type_combo.pack(pady=5)

        # Protocol
        self.proto_label = ctk.CTkLabel(self.command_frame, text="Protocol:")
        self.proto_label.pack(pady=(10, 0))
        self.proto_var = ctk.StringVar(value="WiFi/ESP-NOW")
        self.proto_combo = ctk.CTkOptionMenu(self.command_frame, variable=self.proto_var, values=["WiFi/ESP-NOW", "Zigbee", "LoRa"])
        self.proto_combo.pack(pady=5)

        # Sleep Interval
        self.sleep_label = ctk.CTkLabel(self.command_frame, text="Sleep Interval (s):")
        self.sleep_label.pack(pady=(10, 0))
        self.sleep_entry = ctk.CTkEntry(self.command_frame, placeholder_text="e.g. 10")
        self.sleep_entry.pack(pady=5)

        # Send Button
        self.send_btn = ctk.CTkButton(self.command_frame, text="Execute Command", command=self.send_command)
        self.send_btn.pack(pady=20)

        # Serial setup
        self.ports_config = ports_config
        self.baudrate = baudrate
        self.serial_conns = {}
        self.stop_event = threading.Event()
        
        self.nodes = {} # short_addr -> treeview_id

        self.setup_csv_logging()

        self.setup_csv_logging()
        self.start_serial_threads()

    def setup_csv_logging(self):
        os.makedirs("logs", exist_ok=True)
        filename = datetime.now().strftime("%Y%m%d_%H%M%S.csv")
        self.csv_filepath = os.path.join("logs", filename)
        self.csv_lock = threading.Lock()
        
        try:
            with open(self.csv_filepath, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(["Timestamp", "Node ID", "Protocol", "RSSI", "Tx Count", "Batt (mV)", "Temp (C)", "GNSS Sats", "Latitude", "Longitude", "Accel X/Y/Z"])
            self.append_log(f"Started CSV logging to {self.csv_filepath}")
        except Exception as e:
            self.append_log(f"CRITICAL: Failed to setup CSV logging: {e}")

    def start_serial_threads(self):
        opened_ports = {}
        for proto, port in self.ports_config.items():
            if port:
                if port not in opened_ports:
                    try:
                        conn = serial.Serial(port, self.baudrate, timeout=0.1)
                        self.serial_conns[proto] = conn
                        opened_ports[port] = conn
                        thread = threading.Thread(target=self.read_serial_data, args=(conn, proto), daemon=True)
                        thread.start()
                        proto_name = {PROTO_WIFI: "WiFi/ESP-NOW", PROTO_ZIGBEE: "Zigbee", PROTO_LORA: "LoRa"}.get(proto, "UNK")
                        self.append_log(f"Opened {port} for {proto_name} at {self.baudrate} bps")
                    except Exception as e:
                        self.append_log(f"CRITICAL: Failed to open serial port {port}: {e}")
                else:
                    self.serial_conns[proto] = opened_ports[port]
                    proto_name = {PROTO_WIFI: "WiFi/ESP-NOW", PROTO_ZIGBEE: "Zigbee", PROTO_LORA: "LoRa"}.get(proto, "UNK")
                    self.append_log(f"Shared {port} for {proto_name} at {self.baudrate} bps")

    def read_serial_data(self, conn, expected_proto):
        proto_name_str = {PROTO_WIFI: "WiFi/ESP-NOW", PROTO_ZIGBEE: "Zigbee", PROTO_LORA: "LoRa"}.get(expected_proto, "UNK")
        buffer = bytearray()
        while not self.stop_event.is_set():
            if conn and conn.is_open:
                try:
                    chunk = conn.read(1024)
                    if chunk:
                        buffer.extend(chunk)
                        
                        # Process buffer for binary frames
                        while len(buffer) >= REPORT_SIZE:
                            # Look for sync bytes
                            if buffer[0] == 0xAA and buffer[1] == 0x55:
                                frame = buffer[:REPORT_SIZE]
                                self.parse_binary_frame(frame)
                                buffer = buffer[REPORT_SIZE:]
                            else:
                                # Not a frame, check if it's a standard log line
                                try:
                                    end_idx = buffer.find(b'\n')
                                    if end_idx != -1:
                                        line_bytes = buffer[:end_idx+1]
                                        line = line_bytes.decode('utf-8', errors='ignore').strip()
                                        if line:
                                            self.after(0, self.append_log, f"[{proto_name_str}] {line}")
                                        buffer = buffer[end_idx+1:]
                                    else:
                                        # No newline yet, wait or shift if buffer too large
                                        if len(buffer) > 2048:
                                            buffer = buffer[1:]
                                        break
                                except:
                                    buffer = buffer[1:]
                except Exception as e:
                    print(f"Read error: {e}")
                    time.sleep(1)
            else:
                time.sleep(0.5)

    def parse_binary_frame(self, frame):
        try:
            # Checksum verification
            calc_sum = sum(frame[:-2]) & 0xFFFF
            recv_sum = struct.unpack('<H', frame[-2:])[0]
            
            if calc_sum != recv_sum:
                return

            # Unpack the report
            # <BBBBBHbI64sH
            _, _, length, protocol, report_type, src_addr, rssi, ts, payload_raw, _ = struct.unpack(REPORT_STRUCT_FMT, frame)
            
            proto_str = {PROTO_WIFI: "ESP-NOW", PROTO_ZIGBEE: "Zigbee", PROTO_LORA: "LoRa"}.get(protocol, "UNK")
            
            if report_type == REPORT_TYPE_SENSOR_DATA:
                self.parse_sensor_data(src_addr, proto_str, rssi, payload_raw)
            elif report_type == REPORT_TYPE_NODE_STATUS:
                self.after(0, self.append_log, f"[{proto_str}] Node 0x{src_addr:04X} Status Update")
            elif report_type == REPORT_TYPE_LOG_MESSAGE:
                msg = payload_raw.decode('utf-8', errors='ignore').strip('\x00')
                self.after(0, self.append_log, f"[{proto_str}] 0x{src_addr:04X}: {msg}")
                
        except Exception as e:
            print(f"Parse error: {e}")

    def parse_sensor_data(self, src_addr, proto_str, rssi, payload_raw):
        try:
            # Unpack uplink_payload_t
            # <6sHBHHBiiHHHHHH
            ieee_raw, _, _, batt, temp, sats, lat, lon, curr, ax, ay, az = struct.unpack(UPLINK_PAYLOAD_FMT, payload_raw[:UPLINK_PAYLOAD_SIZE])
            
            now = datetime.now()
            time_str = now.strftime('%H:%M:%S.%f')[:-3]
            full_timestamp = now.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
            
            data = {
                "ID": f"0x{src_addr:04X}",
                "Proto": proto_str,
                "RSSI": f"{rssi} dBm",
                "Tx Count": f"{curr}",
                "Batt": f"{batt*2}",
                "Temp": f"{temp/10.0-273.15:.2f}",
                "GNSS": f"{sats}",
                "Lat": f"{lat/1000000.0:.6f}",
                "Lon": f"{lon/1000000.0:.6f}",
                "Accel X/Y/Z": f"{(ax-1650)/330:.2f}/{(ay-1650)/330:.2f}/{(az-1650)/330:.2f}",
                "Last Seen": time_str
            }
            
            # Log to CSV
            with self.csv_lock:
                try:
                    with open(self.csv_filepath, 'a', newline='') as f:
                        writer = csv.writer(f)
                        writer.writerow([
                            full_timestamp, data["ID"], data["Proto"], data["RSSI"], data["Tx Count"],
                            data["Batt"], data["Temp"], data["GNSS"], 
                            data["Lat"], data["Lon"], data["Accel X/Y/Z"]
                        ])
                except Exception as e:
                    print(f"CSV write error: {e}")
            
            self.after(0, self.update_table, data)
        except Exception as e:
            print(f"Uplink unpack error: {e}")

    def update_table(self, data):
        node_id = data["ID"]
        values = [data[col] for col in self.tree["columns"]]
        
        if node_id in self.nodes:
            self.tree.item(self.nodes[node_id], values=values)
        else:
            item_id = self.tree.insert("", "end", values=values)
            self.nodes[node_id] = item_id

    def append_log(self, text):
        self.log_textbox.configure(state="normal")
        self.log_textbox.insert("end", f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] {text}\n")
        self.log_textbox.see("end")
        self.log_textbox.configure(state="disabled")

    def send_command(self):
        try:
            target_addr_str = self.target_addr_entry.get().strip()
            if not target_addr_str:
                self.append_log("ERROR: Target Address required.")
                return
            target_addr = int(target_addr_str, 16)

            cmd_type_str = self.cmd_type_var.get()
            proto_str = self.proto_var.get()
            
            # Map enum values
            if cmd_type_str == "Change Protocol":
                cmd_type = 0x02
            elif cmd_type_str == "Change Sleep Interval":
                cmd_type = 0x03
            elif cmd_type_str == "Read Attributes":
                cmd_type = 0x01
            elif cmd_type_str == "Trigger GNSS":
                cmd_type = 0x04
            else:
                cmd_type = 0x02
            
            target_proto_enum = {"WiFi/ESP-NOW": PROTO_WIFI, "Zigbee": PROTO_ZIGBEE, "LoRa": PROTO_LORA}.get(proto_str, PROTO_WIFI)
            
            # Determine route protocol from table (default to PROTO_WIFI if not found)
            route_proto = PROTO_WIFI
            for child in self.tree.get_children():
                if self.tree.item(child)["values"][0] == f"0x{target_addr:04X}":
                    p_str = self.tree.item(child)["values"][1]
                    if p_str == "Zigbee": route_proto = PROTO_ZIGBEE
                    elif p_str == "LoRa": route_proto = PROTO_LORA
                    break
            
            command_value_1_min = 0
            command_value_2_max = 0
            command_value_3 = 0
            if cmd_type == 0x02: # CHANGE_PROTOCOL
                command_value_3 = target_proto_enum
            elif cmd_type == 0x03: # CHANGE_SLEEP_INTERVAL
                sleep_str = self.sleep_entry.get().strip()
                if not sleep_str:
                    self.append_log("ERROR: Sleep Interval required.")
                    return
                sleep_val = int(sleep_str)
                if route_proto == PROTO_ZIGBEE:
                    command_value_1_min = 0
                    command_value_2_max = sleep_val
                    command_value_3 = 1000 # Make this 1 to use the zigbee counter for rapid reporting
                else:
                    command_value_3 = sleep_val
            elif cmd_type == 0x01: # READ_ATTRIBUTE
                command_value_3 = 0 # Default attribute ID 0 (All)
            
            if route_proto not in self.serial_conns or not self.serial_conns[route_proto].is_open:
                proto_name = {PROTO_WIFI: "WiFi/ESP-NOW", PROTO_ZIGBEE: "Zigbee", PROTO_LORA: "LoRa"}.get(route_proto, "UNK")
                self.append_log(f"ERROR: Cannot route command to 0x{target_addr:04X}. No active serial connection for {proto_name}.")
                return
            
            frame = struct.pack(COMMAND_STRUCT_FMT,
                0xAA, 0x55,
                route_proto,
                cmd_type,
                target_addr,
                command_value_1_min,
                command_value_2_max,
                command_value_3
            )
            
            self.serial_conns[route_proto].write(frame)
            proto_name = {PROTO_WIFI: "WiFi/ESP-NOW", PROTO_ZIGBEE: "Zigbee", PROTO_LORA: "LoRa"}.get(route_proto, "UNK")
            self.append_log(f"Sent command '{cmd_type_str}' to 0x{target_addr:04X} routed via {proto_name}")
            self.append_log(f"Command values: {command_value_1_min}, {command_value_2_max}, {command_value_3}")

        except ValueError as ve:
             self.append_log(f"ERROR: Invalid number format. {ve}")
        except Exception as e:
            self.append_log(f"ERROR formatting command: {e}")

    def on_closing(self):
        self.stop_event.set()
        closed_conns = set()
        for conn in self.serial_conns.values():
            if conn and conn.is_open and conn not in closed_conns:
                conn.close()
                closed_conns.add(conn)
        self.destroy()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Multi-Protocol Gateway Dashboard")
    parser.add_argument("--wifi", type=str, help="Serial Port for WiFi/ESP-NOW Gateway (e.g., COM3)")
    parser.add_argument("--zigbee", type=str, help="Serial Port for Zigbee Gateway (e.g., COM4)")
    parser.add_argument("--lora", type=str, help="Serial Port for LoRa Gateway (e.g., COM5)")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baudrate")
    args = parser.parse_args()

    ports_config = {}
    if args.wifi: ports_config[PROTO_WIFI] = args.wifi
    if args.zigbee: ports_config[PROTO_ZIGBEE] = args.zigbee
    if args.lora: ports_config[PROTO_LORA] = args.lora

    if not ports_config:
        # Fallback to single port mode if none specified
        port = serial.tools.list_ports.comports()[-1].device if serial.tools.list_ports.comports() else None
        if not port:
            print("No serial ports detected and no ports specified.")
            sys.exit(1)
        print(f"No specific protocol ports provided. Defaulting to port {port} for WiFi.")
        ports_config[PROTO_WIFI] = port

    ctk.set_appearance_mode("Dark")
    app = GatewayMonitorApp(ports_config=ports_config, baudrate=args.baudrate)
    app.protocol("WM_DELETE_WINDOW", app.on_closing)
    app.mainloop()
