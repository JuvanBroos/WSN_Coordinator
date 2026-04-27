import customtkinter as ctk
import serial
import serial.tools.list_ports
import re
import threading
import time
import sys
import argparse

# Define all possible attributes defined in the C code to create stable columns
ATTRIBUTES = [
    "GNSS_SATS", "LATITUDE", "LONGITUDE", "CURRENT_UA",
    "ACCEL_X", "ACCEL_Y", "ACCEL_Z", "SET_PROTOCOL", 
    "SLEEP_INTERVAL", "BATTERY_VOLTAGE"
]

class ZigbeeMonitorApp(ctk.CTk):
    def __init__(self, port, baudrate):
        super().__init__()

        self.title("Zigbee Gateway Monitor")
        self.geometry("1100x400")
        
        # Configure grid for the main window
        self.grid_rowconfigure(1, weight=1)
        self.grid_columnconfigure(0, weight=1)

        # Header 
        self.header_frame = ctk.CTkFrame(self, fg_color="transparent")
        self.header_frame.grid(row=0, column=0, padx=20, pady=10, sticky="ew")
        self.header_label = ctk.CTkLabel(self.header_frame, text=f"Listening on {port} @ {baudrate} bps", font=ctk.CTkFont(size=20, weight="bold"))
        self.header_label.pack(side="left")

        # Add right-aligned test button to header
        self.test_button = ctk.CTkButton(
            self.header_frame, 
            text="Send Test Config",
            command=self.send_test_zcl_command,
            fg_color="#e74c3c",
            hover_color="#c0392b"
        )
        self.test_button.pack(side="right", padx=10)

        self.read_attr_button = ctk.CTkButton(
            self.header_frame, 
            text="Read Attributes",
            command=self.send_read_attributes_command,
            fg_color="#3498db",
            hover_color="#2980b9"
        )
        self.read_attr_button.pack(side="right", padx=10)

        # Scrollable Frame for the Data Table
        self.table_frame = ctk.CTkScrollableFrame(self)
        self.table_frame.grid(row=1, column=0, padx=20, pady=(0, 20), sticky="nsew")

        # Data structure: self.sensors[sensor_addr] = { attr_name: { "value": str, "label_widget": CTkLabel } }
        self.sensors = {}
        
        # Columns tracked dynamically, starting with Sensor ID and Last Seen
        self.columns = ["Sensor ID", "Last Seen"] + ATTRIBUTES
        
        # Draw table headers
        for col_idx, col_name in enumerate(self.columns):
            lbl = ctk.CTkLabel(self.table_frame, text=col_name, font=ctk.CTkFont(weight="bold"), width=90, anchor="w")
            lbl.grid(row=0, column=col_idx, padx=5, pady=5)

        # Regex logic for fetching the data
        # Example: I (12345) ESP_ZB_THERMOSTAT: Custom attr ACCEL_X (0x0005, S16): -100, from 0x1A2B
        self.log_pattern = re.compile(
            r"Custom attr (\w+)\s+\(0x[0-9a-fA-F]+,\s+[^\)]+\):\s+([^,]+),\s+from\s+0x([0-9a-fA-F]+)"
        )

        # Serial port setup
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.stop_event = threading.Event()

        self.start_serial_thread()

    def start_serial_thread(self):
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            self.thread = threading.Thread(target=self.read_serial_data, daemon=True)
            self.thread.start()
        except serial.SerialException as e:
            self.header_label.configure(text=f"Error opening {self.port}: {e}", text_color="red")

    def read_serial_data(self):
        while not self.stop_event.is_set():
            if self.serial_conn and self.serial_conn.is_open:
                try:
                    line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        self.parse_line(line)
                except Exception as e:
                    print(f"Serial read error: {e}")
                    time.sleep(1)

    def parse_line(self, line):
        match = self.log_pattern.search(line)
        if match:
            attr_name = match.group(1)
            value = match.group(2)
            src_addr = f"0x{match.group(3).upper()}"

            # Pass the GUI update back to the main thread securely
            self.after(0, self.update_sensor_data, src_addr, attr_name, value)

    def update_sensor_data(self, src_addr, attr_name, value):
        row_idx = len(self.sensors) + 1 # +1 for header

        if src_addr not in self.sensors:
            self.sensors[src_addr] = {}
            
            # Create a label for Sensor ID
            id_lbl = ctk.CTkLabel(self.table_frame, text=src_addr, anchor="w", text_color="#3498db", font=ctk.CTkFont(weight="bold"))
            id_lbl.grid(row=row_idx, column=self.columns.index("Sensor ID"), padx=5, pady=2)
            self.sensors[src_addr]["Sensor ID"] = id_lbl

            # Create a label for Last Seen
            time_lbl = ctk.CTkLabel(self.table_frame, text="", anchor="w")
            time_lbl.grid(row=row_idx, column=self.columns.index("Last Seen"), padx=5, pady=2)
            self.sensors[src_addr]["Last Seen"] = time_lbl

            # Create empty labels for all attributes
            for attr in ATTRIBUTES:
                lbl = ctk.CTkLabel(self.table_frame, text="-", anchor="w", text_color="gray")
                lbl.grid(row=row_idx, column=self.columns.index(attr), padx=5, pady=2)
                self.sensors[src_addr][attr] = lbl

        # Update Last Seen timestamp
        current_time = time.strftime('%H:%M:%S')
        self.sensors[src_addr]["Last Seen"].configure(text=current_time)

        # Update Attribute
        if attr_name in self.sensors[src_addr]:
            # Highlight briefly by turning it green
            lbl = self.sensors[src_addr][attr_name]
            lbl.configure(text=value, text_color="#2ecc71")
            
            # After 1 second, revert the color back to standard text color
            self.after(1000, lambda l=lbl: l.configure(text_color=("black", "white")))

    def send_test_zcl_command(self):
        if self.serial_conn and self.serial_conn.is_open:
            try:
                # 0xAA = Sync1, 0x01 = Command ID, 0x55 = Sync2
                payload = bytearray([0xAA, 0x01, 0x55])
                self.serial_conn.write(payload)
                print("Sent hardcoded test configuration command (0xAA 0x01 0x55)")
            except Exception as e:
                print(f"Failed to send command: {e}")
        else:
            print("Error: Serial connection is not open.")

    def send_read_attributes_command(self):
        if self.serial_conn and self.serial_conn.is_open:
            try:
                # 0xAA = Sync1, 0x02 = Command ID, 0x55 = Sync2
                payload = bytearray([0xAA, 0x02, 0x55])
                self.serial_conn.write(payload)
                print("Sent read attributes command (0xAA 0x02 0x55)")
            except Exception as e:
                print(f"Failed to send read attributes command: {e}")
        else:
            print("Error: Serial connection is not open.")

    def on_closing(self):
        self.stop_event.set()
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
        self.destroy()


def get_available_ports():
    return [port.device for port in serial.tools.list_ports.comports()]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Graphical Zigbee Gateway Monitor")
    parser.add_argument("-p", "--port", type=str, help="Serial GUI Port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baudrate (default 115200)")
    args = parser.parse_args()

    # If no port provided, try to auto-detect or request user input.
    port = args.port
    if not port:
        ports = get_available_ports()
        if not ports:
            print("No serial ports detected. Please specify via -p flag.")
            sys.exit(1)
        port = ports[-1] # Usually the latest device plugged in (like a USB-UART)

    ctk.set_appearance_mode("System")
    ctk.set_default_color_theme("blue")
    
    app = ZigbeeMonitorApp(port=port, baudrate=args.baudrate)
    app.protocol("WM_DELETE_WINDOW", app.on_closing)
    app.mainloop()
