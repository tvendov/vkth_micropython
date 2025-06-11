#!/usr/bin/env python3
import serial
import time

def simple_memory_test():
    """Simple test to show object allocation and memory regions"""
    port = 'COM20'
    baudrate = 115200
    
    try:
        ser = serial.Serial(port, baudrate, timeout=2)
        print("Connected to COM20")
        
        # Reset
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        # Simple commands to test memory allocation
        commands = [
            "import gc",
            "gc.collect()",
            "print('Initial free:', gc.mem_free())",
            "",
            "# Create 1MB objects",
            "obj1 = bytearray(1024*1024)",
            "print('obj1 address: 0x{:08x}'.format(id(obj1)))",
            "print('obj1 size:', len(obj1))",
            "",
            "obj2 = bytearray(1024*1024)", 
            "print('obj2 address: 0x{:08x}'.format(id(obj2)))",
            "print('obj2 size:', len(obj2))",
            "",
            "obj3 = bytearray(1024*1024)",
            "print('obj3 address: 0x{:08x}'.format(id(obj3)))",
            "print('obj3 size:', len(obj3))",
            "",
            "gc.collect()",
            "print('Free after 3MB allocation:', gc.mem_free())",
            "print('Allocated:', gc.mem_alloc())",
            "",
            "# Memory region analysis",
            "addrs = [id(obj1), id(obj2), id(obj3)]",
            "names = ['obj1', 'obj2', 'obj3']",
            "for addr, name in zip(addrs, names):",
            "    if addr < 0x20000000:",
            "        region = 'Flash/ROM'",
            "    elif addr < 0x30000000:",
            "        region = 'Internal SRAM'", 
            "    elif addr < 0x70000000:",
            "        region = 'External RAM (OSPI)'",
            "    else:",
            "        region = 'Other'",
            "    print('{}: 0x{:08x} -> {}'.format(name, addr, region))"
        ]
        
        print("\nExecuting commands:")
        print("-" * 40)
        
        for cmd in commands:
            if cmd.strip() and not cmd.strip().startswith('#'):
                ser.write(cmd.encode() + b'\r\n')
                time.sleep(0.5)
                
                response = ser.read_all()
                if response:
                    output = response.decode('utf-8', errors='ignore')
                    # Print only the actual output, not the echo
                    lines = output.split('\n')
                    for line in lines:
                        if line.strip() and not line.strip() == cmd.strip() and not line.strip().startswith('>>>'):
                            print(line)
            elif cmd.strip().startswith('#'):
                print(f"\n{cmd}")
            else:
                time.sleep(0.1)
        
        ser.close()
        print("\n" + "-" * 40)
        print("Test completed!")
        
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    print("Simple Memory Allocation Test")
    print("Creating 1MB objects and showing their locations")
    simple_memory_test()
