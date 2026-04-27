import serial
import time

# Replace 'COM3' with the COM port of your ESP32's USB JTAG interface
# You can find this in Windows Device Manager under "Ports (COM & LPT)"
COM_PORT = 'COM6' 
BAUD_RATE = 115200 # The actual baud rate is ignored by USB JTAG, but required by pyserial

def test_serial_echo():
    try:
        # Open the serial port
        with serial.Serial(COM_PORT, BAUD_RATE, timeout=1) as ser:
            print(f"Connected to {COM_PORT}")
            
            # Construct the packet: Sync1 (0xAA) + Payload + Sync2 (0x55)
            # Example payload: Hello in hex (0x48, 0x65, 0x6C, 0x6C, 0x6F)
            packet = bytearray([0xAA, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x55])
            
            print(f"Sending (Hex):  {packet.hex(' ')}")
            ser.write(packet)
            
            # Brief pause to allow the ESP32 to process and echo
            time.sleep(0.1)
            
            # Read the incoming echo
            if ser.in_waiting > 0:
                response = ser.read(ser.in_waiting)
                print(f"Received (Hex): {response.hex(' ')}")
            else:
                print("No response received.")
                
    except serial.SerialException as e:
        print(f"Serial port error: {e}")

if __name__ == "__main__":
    test_serial_echo()