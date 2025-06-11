#!/usr/bin/env python3
import serial
import time

def run_memory_test():
    try:
        # Connect to COM20
        ser = serial.Serial('COM20', 115200, timeout=2)
        print('✓ Connected to COM20')
        
        # Reset board
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        print('\n=== MEMORY ALLOCATION TEST ===')
        
        # Memory test commands
        commands = [
            'import gc',
            'gc.collect()',
            'print("Initial free:", gc.mem_free())',
            'obj1 = bytearray(1024*1024)',
            'print("obj1 addr: 0x{:08x}".format(id(obj1)))',
            'obj2 = bytearray(1024*1024)', 
            'print("obj2 addr: 0x{:08x}".format(id(obj2)))',
            'obj3 = bytearray(1024*1024)',
            'print("obj3 addr: 0x{:08x}".format(id(obj3)))',
            'gc.collect()',
            'print("Free after 3MB:", gc.mem_free())',
            'print("Allocated:", gc.mem_alloc())',
            'for addr, name in [(id(obj1), "obj1"), (id(obj2), "obj2"), (id(obj3), "obj3")]:\n    if addr < 0x20000000:\n        region = "Flash/ROM"\n    elif addr < 0x30000000:\n        region = "Internal SRAM"\n    elif addr < 0x70000000:\n        region = "External RAM (OSPI)"\n    else:\n        region = "Other"\n    print("{}: 0x{:08x} -> {}".format(name, addr, region))'
        ]
        
        for cmd in commands:
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(0.8)
            
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip():
                        print(clean)
        
        ser.close()
        print('\n✓ Test completed successfully!')
        
    except serial.SerialException as e:
        if "Access is denied" in str(e):
            print("❌ COM20 is in use by TeraTerm. Please close TeraTerm first.")
        else:
            print(f"❌ Serial error: {e}")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    run_memory_test()
