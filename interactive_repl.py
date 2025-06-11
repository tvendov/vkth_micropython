#!/usr/bin/env python3
import serial
import time
import sys

def interactive_repl():
    """Connect to MicroPython board and establish interactive REPL"""
    port = 'COM20'
    baudrate = 115200
    
    try:
        print(f"Connecting to {port} at {baudrate} baud...")
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"✓ Connected to {port}")
        
        # Try to get the board's attention
        print("\nTrying to establish communication...")
        
        # Send multiple interrupt signals
        for i in range(3):
            ser.write(b'\x03')  # Ctrl+C
            time.sleep(0.2)
        
        # Send soft reset
        print("Sending soft reset...")
        ser.write(b'\x04')  # Ctrl+D
        time.sleep(2)
        
        # Read any initial output
        initial_output = ser.read_all()
        if initial_output:
            print("Initial board output:")
            print(initial_output.decode('utf-8', errors='ignore'))
        
        # Try to get a prompt
        print("\nTrying to get MicroPython prompt...")
        ser.write(b'\r\n')
        time.sleep(0.5)
        
        # Send a simple command to test
        test_commands = [
            b'\r\n',
            b'print("Hello from MicroPython!")\r\n',
            b'1+1\r\n',
            b'import sys\r\n',
            b'sys.platform\r\n'
        ]
        
        for cmd in test_commands:
            print(f"Sending: {cmd.decode('utf-8', errors='ignore').strip()}")
            ser.write(cmd)
            time.sleep(0.5)
            
            # Read response
            response = ser.read_all()
            if response:
                print(f"Response: {response.decode('utf-8', errors='ignore')}")
            else:
                print("No response")
            print("-" * 30)
        
        # Now try memory commands
        print("\nTrying memory check commands...")
        memory_commands = [
            b'import gc\r\n',
            b'gc.collect()\r\n',
            b'gc.mem_free()\r\n',
            b'gc.mem_alloc()\r\n',
            b'gc.mem_free() + gc.mem_alloc()\r\n'
        ]
        
        for cmd in memory_commands:
            print(f"Sending: {cmd.decode('utf-8', errors='ignore').strip()}")
            ser.write(cmd)
            time.sleep(0.8)
            
            response = ser.read_all()
            if response:
                print(f"Response: {response.decode('utf-8', errors='ignore')}")
            else:
                print("No response")
            print("-" * 30)
        
        ser.close()
        print("\nConnection closed.")
        
    except serial.SerialException as e:
        print(f"❌ Serial error: {e}")
        return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False
    
    return True

if __name__ == "__main__":
    print("MicroPython Interactive REPL Test")
    print("=" * 40)
    interactive_repl()
