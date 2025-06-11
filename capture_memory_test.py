#!/usr/bin/env python3
import serial
import time

def capture_memory_test():
    """Capture memory test output that we can see"""
    port = 'COM20'
    baudrate = 115200
    
    print("⚠️  Please close TeraTerm first so we can use COM20!")
    input("Press Enter when TeraTerm is closed...")
    
    try:
        ser = serial.Serial(port, baudrate, timeout=2)
        print(f"✓ Connected to {port}")
        
        # Reset and get clean prompt
        ser.write(b'\x03')  # Ctrl+C
        time.sleep(0.2)
        ser.write(b'\x04')  # Ctrl+D
        time.sleep(2)
        ser.read_all()  # Clear buffer
        
        commands = [
            "import gc",
            "gc.collect()",
            "print('=== INITIAL STATE ===')",
            "print('Initial free:', gc.mem_free())",
            "print('Initial alloc:', gc.mem_alloc())",
            "",
            "print('\\n=== CREATING 1MB OBJECTS ===')",
            "obj1 = bytearray(1024*1024)",
            "print('obj1 address: 0x{:08x}'.format(id(obj1)))",
            "print('obj1 size: {:,} bytes'.format(len(obj1)))",
            "",
            "obj2 = bytearray(1024*1024)",
            "print('obj2 address: 0x{:08x}'.format(id(obj2)))",
            "",
            "obj3 = bytearray(1024*1024)",
            "print('obj3 address: 0x{:08x}'.format(id(obj3)))",
            "",
            "print('\\n=== MEMORY ANALYSIS ===')",
            "gc.collect()",
            "print('Free after 3MB:', gc.mem_free())",
            "print('Allocated:', gc.mem_alloc())",
            "",
            "print('\\n=== MEMORY REGIONS ===')",
            "addrs = [id(obj1), id(obj2), id(obj3)]",
            "names = ['obj1(1MB)', 'obj2(1MB)', 'obj3(1MB)']",
            "for addr, name in zip(addrs, names):",
            "    if addr < 0x20000000:",
            "        region = 'Flash/ROM'",
            "    elif addr < 0x30000000:",
            "        region = 'Internal SRAM'",
            "    elif addr < 0x70000000:",
            "        region = 'External RAM (OSPI)'",
            "    else:",
            "        region = 'Other'",
            "    print('{:12} @ 0x{:08x} -> {}'.format(name, addr, region))"
        ]
        
        print("\n" + "="*50)
        print("MEMORY ALLOCATION TEST RESULTS:")
        print("="*50)
        
        for cmd in commands:
            if cmd.strip():
                ser.write(cmd.encode() + b'\r\n')
                time.sleep(0.5)
                
                response = ser.read_all()
                if response:
                    output = response.decode('utf-8', errors='ignore')
                    lines = output.split('\n')
                    for line in lines:
                        clean_line = line.strip()
                        if clean_line and not clean_line.startswith('>>>') and clean_line != cmd.strip():
                            print(clean_line)
            else:
                time.sleep(0.1)
        
        print("="*50)
        ser.close()
        
    except serial.SerialException as e:
        if "Access is denied" in str(e):
            print("❌ COM20 is still in use by TeraTerm!")
            print("Please close TeraTerm and try again.")
        else:
            print(f"❌ Serial error: {e}")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    capture_memory_test()
