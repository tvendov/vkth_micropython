#!/usr/bin/env python3
import serial
import time
import sys

def connect_to_board(port='COM20', baudrate=115200):
    """Connect to MicroPython board and check memory info"""
    try:
        # Open serial connection
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"Connected to {port} at {baudrate} baud")
        
        # Send Ctrl+C to interrupt any running program
        ser.write(b'\x03')
        time.sleep(0.1)
        
        # Send Ctrl+D to soft reset
        ser.write(b'\x04')
        time.sleep(2)
        
        # Clear any existing output
        ser.read_all()
        
        # Send memory check commands
        commands = [
            "import gc",
            "gc.collect()",
            "print('=== Memory Information ===')",
            "print('Free memory:', gc.mem_free())",
            "print('Allocated memory:', gc.mem_alloc())",
            "print('Total heap size:', gc.mem_free() + gc.mem_alloc())",
            "",
            "print('\\n=== System Information ===')",
            "import sys",
            "print('Platform:', sys.platform)",
            "print('Implementation:', sys.implementation)",
            "",
            "print('\\n=== OSPI RAM Check ===')",
            "try:",
            "    import machine",
            "    print('Machine module loaded successfully')",
            "    # Check if OSPI RAM is available by looking at heap size",
            "    total_heap = gc.mem_free() + gc.mem_alloc()",
            "    print(f'Total heap size: {total_heap} bytes ({total_heap/1024:.1f} KB)')",
            "    if total_heap > 500000:  # More than 500KB suggests OSPI RAM is working",
            "        print('OSPI RAM appears to be working (large heap detected)')",
            "    else:",
            "        print('OSPI RAM may not be working (small heap detected)')",
            "except Exception as e:",
            "    print('Error checking OSPI:', e)",
            "",
            "print('\\n=== Available Modules ===')",
            "help('modules')",
        ]
        
        # Send each command
        for cmd in commands:
            if cmd.strip():  # Skip empty lines
                print(f">>> {cmd}")
                ser.write(cmd.encode() + b'\r\n')
                time.sleep(0.2)
                
                # Read response
                response = b''
                start_time = time.time()
                while time.time() - start_time < 2:  # 2 second timeout
                    if ser.in_waiting:
                        data = ser.read(ser.in_waiting)
                        response += data
                        print(data.decode('utf-8', errors='ignore'), end='')
                    else:
                        time.sleep(0.1)
            else:
                print()  # Empty line
                
        ser.close()
        print("\nConnection closed.")
        
    except serial.SerialException as e:
        print(f"Serial connection error: {e}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False
    
    return True

if __name__ == "__main__":
    # Try different common COM ports if COM20 doesn't work
    ports_to_try = ['COM20', 'COM3', 'COM4', 'COM5', 'COM6', 'COM7', 'COM8', 'COM9', 'COM10']
    
    for port in ports_to_try:
        print(f"Trying to connect to {port}...")
        if connect_to_board(port):
            break
        print(f"Failed to connect to {port}")
    else:
        print("Could not connect to any COM port")
