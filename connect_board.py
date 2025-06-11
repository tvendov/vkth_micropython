"""
Simple Board Connection Script
=============================

Connect to the RA6M5 board and provide an interactive session.
"""

import serial
import time
import threading
import sys

def read_from_board(ser):
    """Read data from board continuously"""
    while True:
        try:
            if ser.in_waiting:
                data = ser.read_all().decode('utf-8', errors='ignore')
                if data:
                    print(data, end='')
        except:
            break
        time.sleep(0.01)

def main():
    """Main connection function"""
    print("Connecting to RA6M5 board on COM4...")
    
    try:
        ser = serial.Serial('COM4', 115200, timeout=1)
        print("Connected! Press Ctrl+C to exit.")
        print("="*50)
        
        # Start reading thread
        read_thread = threading.Thread(target=read_from_board, args=(ser,), daemon=True)
        read_thread.start()
        
        # Send initial commands
        ser.write(b'\x03')  # Ctrl+C
        time.sleep(0.5)
        ser.write(b'\r\n')
        time.sleep(0.5)
        
        print("Board connected. You can now type commands.")
        print("Try these commands:")
        print("  import network")
        print("  lan = network.LAN()")
        print("  lan.active(True)")
        print("  lan.ifconfig()")
        print("")
        
        # Interactive mode
        while True:
            try:
                user_input = input()
                if user_input.strip():
                    ser.write(user_input.encode() + b'\r\n')
            except KeyboardInterrupt:
                break
            except EOFError:
                break
                
    except Exception as e:
        print(f"Connection failed: {e}")
    finally:
        try:
            ser.close()
        except:
            pass
        print("\nConnection closed.")

if __name__ == "__main__":
    main()
