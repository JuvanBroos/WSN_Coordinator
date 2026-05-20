import customtkinter as ctk
from tkinter import ttk
import serial
import serial.tools.list_ports
import threading
import time
import sys
import argparse
import struct

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
# H  : current_ua (uint16)
# H  : accel_x (uint16)
# H  : accel_y (uint16)
# H  : accel_z (uint16)
UPLINK_PAYLOAD_FMT = '<6sHBHHBiiHHHH'
UPLINK_PAYLOAD_SIZE = struct.calcsize(UPLINK_PAYLOAD_FMT)

class GatewayMonitorApp(ctk.CTk):
    def __init__(self, port, baudrate):
        super().__init__()

        self.title("Multi-Protocol Gateway Dashboard")
        self.geometry("1200x800")
        
        # Configure grid
        self.grid_columnconfigure(0, weight=1)
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
            "ID", "Proto", "RSSI", "Batt", "Temp", "GNSS", "Lat", "Lon", "Accel X/Y/Z", "Last Seen"
        ), show="headings")
        
        self.tree.heading("ID", text="Node ID")
        self.tree.heading("Proto", text="Protocol")
        self.tree.heading("RSSI", text="RSSI")
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

        # Serial setup
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.stop_event = threading.Event()
        
        self.nodes = {} # short_addr -> treeview_id

        self.start_serial_thread()

    def start_serial_thread(self):
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.thread = threading.Thread(target=self.read_serial_data, daemon=True)
            self.thread.start()
            self.append_log(f"Opened {self.port} at {self.baudrate} bps")
        except Exception as e:
            self.append_log(f"CRITICAL: Failed to open serial port: {e}")

    def read_serial_data(self):
        buffer = bytearray()
        while not self.stop_event.is_set():
            if self.serial_conn and self.serial_conn.is_open:
                try:
                    chunk = self.serial_conn.read(1024)
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
                                            self.after(0, self.append_log, line)
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
                # self.after(0, self.append_log, f"DEBUG: Checksum mismatch (calc: {calc_sum:04X}, recv: {recv_sum:04X})")
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
            
            data = {
                "ID": f"0x{src_addr:04X}",
                "Proto": proto_str,
                "RSSI": f"{rssi} dBm",
                "Batt": f"{batt}",
                "Temp": f"{temp/10.0-273.15:.2f}",
                "GNSS": f"{sats}",
                "Lat": f"{lat/1000000.0:.6f}",
                "Lon": f"{lon/1000000.0:.6f}",
                "Accel X/Y/Z": f"{ax}/{ay}/{az}",
                "Last Seen": time.strftime('%H:%M:%S')
            }
            
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
        self.log_textbox.insert("end", f"[{time.strftime('%H:%M:%S')}] {text}\n")
        self.log_textbox.see("end")
        self.log_textbox.configure(state="disabled")

    def on_closing(self):
        self.stop_event.set()
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
        self.destroy()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Multi-Protocol Gateway Dashboard")
    parser.add_argument("-p", "--port", type=str, help="Serial Port")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baudrate")
    args = parser.parse_args()

    port = args.port or (serial.tools.list_ports.comports()[-1].device if serial.tools.list_ports.comports() else None)
    if not port:
        print("No serial ports detected.")
        sys.exit(1)

    ctk.set_appearance_mode("Dark")
    app = GatewayMonitorApp(port=port, baudrate=args.baudrate)
    app.protocol("WM_DELETE_WINDOW", app.on_closing)
    app.mainloop()
