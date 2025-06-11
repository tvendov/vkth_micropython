#!/usr/bin/env python3
import serial
import time

def simple_speed_test():
    try:
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Just interrupt, don't reset
        ser.write(b'\x03')
        time.sleep(0.3)
        ser.read_all()
        
        def send_and_read(cmd, wait_time=1.0):
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip():
                        print(clean)
        
        print('\n=== SIMPLE MEMORY SPEED TEST ===')
        
        # Setup
        send_and_read('import gc, time')
        send_and_read('gc.collect()')
        
        # Test 1: Small object (likely internal SRAM)
        print('\n--- Test 1: 10KB Object ---')
        send_and_read('obj1 = bytearray(10240)')
        send_and_read('addr1 = id(obj1)')
        send_and_read('print("Address: 0x{:08x}".format(addr1))')
        send_and_read('region1 = "OSPI" if 0x30000000 <= addr1 < 0x70000000 else "SRAM"')
        send_and_read('print("Region:", region1)')
        
        # Write speed test
        send_and_read('start = time.ticks_us()')
        send_and_read('for i in range(10240): obj1[i] = 0xAA', 2.0)
        send_and_read('write_time1 = time.ticks_diff(time.ticks_us(), start)')
        send_and_read('write_speed1 = (10240 / write_time1) * 1000000 / 1024')
        send_and_read('print("Write: {} μs ({:.1f} KB/s)".format(write_time1, write_speed1))')
        
        # Read speed test
        send_and_read('start = time.ticks_us()')
        send_and_read('sum1 = sum(obj1)', 2.0)
        send_and_read('read_time1 = time.ticks_diff(time.ticks_us(), start)')
        send_and_read('read_speed1 = (10240 / read_time1) * 1000000 / 1024')
        send_and_read('print("Read: {} μs ({:.1f} KB/s)".format(read_time1, read_speed1))')
        
        # Test 2: Large object (likely OSPI RAM)
        print('\n--- Test 2: 1MB Object ---')
        send_and_read('obj2 = bytearray(1048576)', 2.0)
        send_and_read('addr2 = id(obj2)')
        send_and_read('print("Address: 0x{:08x}".format(addr2))')
        send_and_read('region2 = "OSPI" if 0x30000000 <= addr2 < 0x70000000 else "SRAM"')
        send_and_read('print("Region:", region2)')
        
        # Write speed test (sample only part to avoid timeout)
        send_and_read('start = time.ticks_us()')
        send_and_read('for i in range(0, 1048576, 1024): obj2[i] = 0x55', 3.0)
        send_and_read('write_time2 = time.ticks_diff(time.ticks_us(), start)')
        send_and_read('write_speed2 = (1024 / write_time2) * 1000000 / 1024')
        send_and_read('print("Write (sampled): {} μs ({:.1f} KB/s)".format(write_time2, write_speed2))')
        
        # Block write test
        send_and_read('pattern = bytes([0x77] * 1024)')
        send_and_read('start = time.ticks_us()')
        send_and_read('for i in range(0, 1048576, 1024): obj2[i:i+1024] = pattern', 3.0)
        send_and_read('block_write_time2 = time.ticks_diff(time.ticks_us(), start)')
        send_and_read('block_write_speed2 = (1048576 / block_write_time2) * 1000000 / 1024 / 1024')
        send_and_read('print("Block write: {} μs ({:.1f} MB/s)".format(block_write_time2, block_write_speed2))')
        
        # Memory comparison
        print('\n--- Memory Comparison ---')
        send_and_read('print("10KB object region:", region1)')
        send_and_read('print("1MB object region:", region2)')
        send_and_read('if region1 != region2: print("✓ Different memory regions detected!")')
        send_and_read('else: print("⚠ Both objects in same region")')
        
        # Cleanup
        send_and_read('del obj1, obj2')
        send_and_read('gc.collect()')
        
        ser.close()
        print('\n✓ Speed test completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Simple Memory Speed Test")
    print("Comparing performance between memory regions")
    print("=" * 50)
    simple_speed_test()
