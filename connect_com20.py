#!/usr/bin/env python3
import serial
import time
import sys

def check_memory_info():
    """Connect to MicroPython board on COM20 and check memory info"""
    port = 'COM20'
    baudrate = 115200
    
    try:
        print(f"Attempting to connect to {port} at {baudrate} baud...")
        
        # Try to open the serial connection
        ser = serial.Serial(port, baudrate, timeout=2)
        print(f"✓ Successfully connected to {port}")
        
        # Give the board a moment to settle
        time.sleep(0.5)
        
        # Send Ctrl+C to interrupt any running program
        print("Sending interrupt signal...")
        ser.write(b'\x03')
        time.sleep(0.2)
        
        # Clear any buffered output
        ser.read_all()
        
        # Send commands to check memory
        commands = [
            "import gc",
            "gc.collect()",
            "print('=== MEMORY INFORMATION ===')",
            "free_mem = gc.mem_free()",
            "alloc_mem = gc.mem_alloc()",
            "total_heap = free_mem + alloc_mem",
            "print(f'Free memory: {free_mem:,} bytes ({free_mem/1024:.1f} KB)')",
            "print(f'Allocated memory: {alloc_mem:,} bytes ({alloc_mem/1024:.1f} KB)')",
            "print(f'Total heap: {total_heap:,} bytes ({total_heap/1024:.1f} KB, {total_heap/1024/1024:.2f} MB)')",
            "",
            "print('\\n=== OSPI RAM STATUS ===')",
            "if total_heap > 1000000:",
            "    print('✓ OSPI RAM appears to be WORKING (heap > 1MB)')",
            "elif total_heap > 500000:",
            "    print('? OSPI RAM may be working (heap > 500KB)')",
            "else:",
            "    print('✗ OSPI RAM may NOT be working (small heap)')",
            "",
            "print('\\n=== SYSTEM INFO ===')",
            "import sys",
            "print(f'Platform: {sys.platform}')",
            "print(f'MicroPython: {sys.implementation.name} {sys.implementation.version}')",
        ]
        
        print("\nSending commands to board...")
        print("=" * 50)
        
        for cmd in commands:
            if cmd.strip():  # Skip empty lines
                ser.write(cmd.encode() + b'\r\n')
                time.sleep(0.1)
                
                # Read response with timeout
                response = b''
                start_time = time.time()
                while time.time() - start_time < 1.5:
                    if ser.in_waiting:
                        data = ser.read(ser.in_waiting)
                        response += data
                        # Print immediately for real-time feedback
                        print(data.decode('utf-8', errors='ignore'), end='')
                    else:
                        time.sleep(0.05)
            else:
                time.sleep(0.1)  # Small delay for empty lines
                
        print("\n" + "=" * 50)
        print("Memory check completed!")
        
        ser.close()
        
    except serial.SerialException as e:
        if "PermissionError" in str(e) or "Access is denied" in str(e):
            print(f"❌ ERROR: COM20 is already in use by another application!")
            print("Please close any other terminal programs (PuTTY, Tera Term, etc.) that might be using COM20")
            print("Or check Windows Device Manager to see what's using the port")
        else:
            print(f"❌ Serial connection error: {e}")
        return False
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        return False
    
    return True

if __name__ == "__main__":
    print("MicroPython Board Memory Checker")
    print("Connecting to VK-RA6M5 board on COM20...")
    print()
    
    success = check_memory_info()
    
    if not success:
        print("\n💡 Troubleshooting tips:")
        print("1. Make sure no other programs are connected to COM20")
        print("2. Try unplugging and reconnecting the USB cable")
        print("3. Check Windows Device Manager for the correct COM port")
        print("4. Ensure the board is powered on and running MicroPython")
