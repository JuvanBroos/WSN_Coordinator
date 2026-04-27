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
            print("Type your payload and press Enter. (Type 'q' or 'quit' to exit)")
            
            while True:
                user_input = input("\nPayload> ")
                if user_input.lower() in ['q', 'quit']:
                    print("Exiting...")
                    break
                    
                if not user_input:
                    continue

                # Construct the packet: Sync1 (0xAA) + Payload + Sync2 (0x55)
                payload_bytes = user_input.encode('utf-8')
                packet = bytearray([0xAA]) + bytearray(payload_bytes) + bytearray([0x55])
                
                print(f"Sending (Hex):  {packet.hex(' ')}")
                ser.write(packet)
                
                # Brief pause to allow the ESP32 to process and echo
                time.sleep(0.1)
                
                # Read the incoming echo
                if ser.in_waiting > 0:
                    response = ser.read(ser.in_waiting)
                    print(f"Received (Hex): {response.hex(' ')}")
                    try:
                        # Extract the payload between the sync words to display as a string
                        if response[0] == 0xAA and response[-1] == 0x55:
                            decoded_str = response[1:-1].decode('utf-8')
                            print(f"Received (Str): {decoded_str}")
                    except Exception:
                        pass # Ignore decoding errors if the payload isn't valid UTF-8
                else:
                    print("No response received.")
                
    except serial.SerialException as e:
        print(f"Serial port error: {e}")

if __name__ == "__main__":
    test_serial_echo()