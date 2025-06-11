#!/usr/bin/env python3
import serial
import time

def better_memory_test():
    try:
        # Connect to COM20
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Send interrupt only (no reset)
        ser.write(b'\x03')
        time.sleep(0.5)
        ser.read_all()
        
        print('\n=== MEMORY ALLOCATION TEST RESULTS ===')
        
        # Send all commands as one block to avoid reset
        full_script = '''
import gc
gc.collect()
print("=== INITIAL STATE ===")
print("Initial free: {:,} bytes ({:.2f} MB)".format(gc.mem_free(), gc.mem_free()/1024/1024))
print("Initial alloc: {:,} bytes".format(gc.mem_alloc()))

print("\\n=== CREATING 1MB OBJECTS ===")
obj1 = bytearray(1024*1024)
print("obj1 created - addr: 0x{:08x}".format(id(obj1)))

obj2 = bytearray(1024*1024)
print("obj2 created - addr: 0x{:08x}".format(id(obj2)))

obj3 = bytearray(1024*1024)
print("obj3 created - addr: 0x{:08x}".format(id(obj3)))

print("\\n=== MEMORY USAGE ===")
gc.collect()
print("Free after 3MB: {:,} bytes ({:.2f} MB)".format(gc.mem_free(), gc.mem_free()/1024/1024))
print("Allocated: {:,} bytes ({:.2f} MB)".format(gc.mem_alloc(), gc.mem_alloc()/1024/1024))

print("\\n=== MEMORY REGION ANALYSIS ===")
objects = [(id(obj1), "obj1(1MB)"), (id(obj2), "obj2(1MB)"), (id(obj3), "obj3(1MB)")]
for addr, name in objects:
    if addr < 0x20000000:
        region = "Flash/ROM"
    elif addr < 0x30000000:
        region = "Internal SRAM"
    elif addr < 0x70000000:
        region = "External RAM (OSPI)"
    else:
        region = "Other/Unknown"
    print("{:12} @ 0x{:08x} -> {}".format(name, addr, region))

print("\\n=== OSPI RAM STATUS ===")
total_heap = gc.mem_free() + gc.mem_alloc()
if any(0x30000000 <= addr < 0x70000000 for addr, _ in objects):
    print("✓ OSPI RAM is being used for large allocations!")
else:
    print("? Objects are in internal SRAM")
print("Total heap: {:,} bytes ({:.2f} MB)".format(total_heap, total_heap/1024/1024))
'''
        
        # Send the entire script
        ser.write(full_script.encode())
        ser.write(b'\r\n')
        
        # Wait for execution and collect all output
        time.sleep(3)
        
        # Read all output
        all_output = b''
        for _ in range(10):  # Try multiple times to get all output
            time.sleep(0.5)
            data = ser.read_all()
            if data:
                all_output += data
            else:
                break
        
        if all_output:
            output = all_output.decode('utf-8', errors='ignore')
            lines = output.split('\n')
            
            # Filter and print meaningful lines
            for line in lines:
                clean = line.strip()
                if clean and not clean.startswith('>>>') and not clean.startswith('...'):
                    # Skip the script echo
                    if not any(keyword in clean for keyword in ['import gc', 'bytearray(', 'print(', 'objects =', 'for addr']):
                        print(clean)
        
        ser.close()
        print('\n✓ Memory test completed!')
        
    except serial.SerialException as e:
        if "Access is denied" in str(e):
            print("❌ COM20 is in use. Please close TeraTerm first.")
        else:
            print(f"❌ Serial error: {e}")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    better_memory_test()
