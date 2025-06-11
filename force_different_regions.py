#!/usr/bin/env python3
import serial
import time

def force_different_regions():
    try:
        print('Attempting to connect to COM20...')
        ser = serial.Serial('COM20', 115200, timeout=2)
        print('✓ Connected to COM20')
        
        # Send interrupt and wait
        ser.write(b'\x03')
        time.sleep(0.5)
        ser.read_all()
        
        def send_cmd(cmd):
            print(f'Sending: {cmd}')
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(1.5)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                print(f'Response: {output.strip()}')
                return output
            else:
                print('No response')
                return ''
        
        print('\n=== FORCING OBJECTS INTO DIFFERENT MEMORY REGIONS ===')
        
        # Step 1: Setup
        send_cmd('import gc, time')
        send_cmd('gc.collect()')
        
        # Step 2: Check initial memory
        send_cmd('print("Initial free:", gc.mem_free())')
        
        # Step 3: Try to force allocation in different regions
        # Method: Allocate many objects to exhaust one region, then allocate in another
        
        print('\n--- Allocating multiple large objects ---')
        send_cmd('objects = []')
        
        # Allocate several 1MB objects
        for i in range(5):
            print(f'Allocating object {i+1}...')
            send_cmd(f'obj{i} = bytearray(1048576)')
            send_cmd(f'objects.append(obj{i})')
            send_cmd(f'print("obj{i} addr: 0x{{:08x}}".format(id(obj{i})))')
            send_cmd('gc.collect()')
            send_cmd('print("Free memory:", gc.mem_free())')
        
        print('\n--- Checking object locations ---')
        send_cmd('for i, obj in enumerate(objects):')
        send_cmd('    addr = id(obj)')
        send_cmd('    if addr >= 0x68000000 and addr < 0x70000000:')
        send_cmd('        region = "DIRECT_OSPI"')
        send_cmd('    elif addr >= 0x20080000 and addr < 0x30000000:')
        send_cmd('        region = "MAPPED_OSPI"')
        send_cmd('    elif addr >= 0x20000000 and addr < 0x20080000:')
        send_cmd('        region = "INTERNAL_SRAM"')
        send_cmd('    else:')
        send_cmd('        region = "UNKNOWN"')
        send_cmd('    print("Object {}: 0x{:08x} -> {}".format(i, addr, region))')
        
        print('\n--- Speed test on different objects ---')
        
        # Test first object
        send_cmd('test_obj1 = objects[0]')
        send_cmd('addr1 = id(test_obj1)')
        send_cmd('print("Test object 1 addr: 0x{:08x}".format(addr1))')
        
        send_cmd('start = time.ticks_us()')
        send_cmd('for i in range(100): test_obj1[i] = 0xAA')
        send_cmd('time1 = time.ticks_diff(time.ticks_us(), start)')
        send_cmd('print("Object 1 write time: {} μs".format(time1))')
        
        # Test last object (might be in different region)
        send_cmd('test_obj2 = objects[-1]')
        send_cmd('addr2 = id(test_obj2)')
        send_cmd('print("Test object 2 addr: 0x{:08x}".format(addr2))')
        
        send_cmd('start = time.ticks_us()')
        send_cmd('for i in range(100): test_obj2[i] = 0xBB')
        send_cmd('time2 = time.ticks_diff(time.ticks_us(), start)')
        send_cmd('print("Object 2 write time: {} μs".format(time2))')
        
        # Compare
        send_cmd('if addr1 != addr2:')
        send_cmd('    print("✓ Objects have different addresses")')
        send_cmd('    if time1 < time2:')
        send_cmd('        print("Object 1 is faster")')
        send_cmd('    elif time2 < time1:')
        send_cmd('        print("Object 2 is faster")')
        send_cmd('    else:')
        send_cmd('        print("Similar performance")')
        send_cmd('else:')
        send_cmd('    print("⚠ Objects have same address")')
        
        # Cleanup
        send_cmd('del objects, test_obj1, test_obj2')
        send_cmd('gc.collect()')
        
        ser.close()
        print('\n✓ Test completed!')
        
    except serial.SerialException as e:
        print(f"❌ Serial error: {e}")
        print("Make sure COM20 is available and not used by another program")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Force Different Memory Regions Test")
    print("=" * 40)
    force_different_regions()
