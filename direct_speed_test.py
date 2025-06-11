#!/usr/bin/env python3
import serial
import time

def direct_speed_test():
    try:
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        def send_and_capture(cmd, wait_time=1.0):
            """Send command and capture clean output"""
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)
            response = ser.read_all()
            results = []
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip():
                        results.append(clean)
                        print(clean)
            return results
        
        print('\n=== DIRECT MEMORY SPEED TEST ===')
        
        # Setup
        send_and_capture('import gc, time, micropython')
        send_and_capture('gc.collect()')
        
        # Test 1: Small object (4KB) - likely internal SRAM
        print('\n--- TEST 1: 4KB Object ---')
        send_and_capture('gc.collect()')
        send_and_capture('obj1 = bytearray(4096)')
        send_and_capture('addr1 = id(obj1)')
        addr_result = send_and_capture('print("0x{:08x}".format(addr1))')
        
        # Determine region
        send_and_capture('if addr1 >= 0x68000000 and addr1 < 0x70000000: region1 = "OSPI_DIRECT"')
        send_and_capture('elif addr1 >= 0x20000000 and addr1 < 0x20080000: region1 = "INTERNAL_SRAM"')
        send_and_capture('elif addr1 >= 0x20080000 and addr1 < 0x30000000: region1 = "MAPPED_OSPI"')
        send_and_capture('else: region1 = "UNKNOWN"')
        send_and_capture('print("Region:", region1)')
        
        # Speed test 1: Write
        print('Writing 1KB pattern...')
        send_and_capture('gc.disable()')
        send_and_capture('start = time.ticks_us()')
        send_and_capture('for i in range(1024): obj1[i] = i & 0xFF', 2.0)
        send_and_capture('write_time1 = time.ticks_diff(time.ticks_us(), start)')
        send_and_capture('gc.enable()')
        send_and_capture('write_speed1 = (1024 * 1000000) // write_time1')
        send_and_capture('print("Write: {} μs ({} bytes/s)".format(write_time1, write_speed1))')
        
        # Speed test 1: Read
        print('Reading 1KB...')
        send_and_capture('gc.disable()')
        send_and_capture('start = time.ticks_us()')
        send_and_capture('checksum1 = sum(obj1[i] for i in range(1024))', 2.0)
        send_and_capture('read_time1 = time.ticks_diff(time.ticks_us(), start)')
        send_and_capture('gc.enable()')
        send_and_capture('read_speed1 = (1024 * 1000000) // read_time1')
        send_and_capture('print("Read: {} μs ({} bytes/s)".format(read_time1, read_speed1))')
        
        # Test 2: Large object (1MB) - should use OSPI
        print('\n--- TEST 2: 1MB Object ---')
        send_and_capture('gc.collect()')
        send_and_capture('obj2 = bytearray(1048576)', 2.0)
        send_and_capture('addr2 = id(obj2)')
        send_and_capture('print("0x{:08x}".format(addr2))')
        
        # Determine region
        send_and_capture('if addr2 >= 0x68000000 and addr2 < 0x70000000: region2 = "OSPI_DIRECT"')
        send_and_capture('elif addr2 >= 0x20000000 and addr2 < 0x20080000: region2 = "INTERNAL_SRAM"')
        send_and_capture('elif addr2 >= 0x20080000 and addr2 < 0x30000000: region2 = "MAPPED_OSPI"')
        send_and_capture('else: region2 = "UNKNOWN"')
        send_and_capture('print("Region:", region2)')
        
        # Speed test 2: Block write (1KB blocks)
        print('Block writing 10KB...')
        send_and_capture('pattern = bytes(range(256)) * 4')  # 1KB pattern
        send_and_capture('gc.disable()')
        send_and_capture('start = time.ticks_us()')
        send_and_capture('for i in range(0, 10240, 1024): obj2[i:i+1024] = pattern', 3.0)
        send_and_capture('block_write_time2 = time.ticks_diff(time.ticks_us(), start)')
        send_and_capture('gc.enable()')
        send_and_capture('block_write_speed2 = (10240 * 1000000) // block_write_time2')
        send_and_capture('print("Block write: {} μs ({} bytes/s)".format(block_write_time2, block_write_speed2))')
        
        # Speed test 2: Block read
        print('Block reading 10KB...')
        send_and_capture('gc.disable()')
        send_and_capture('start = time.ticks_us()')
        send_and_capture('total = sum(sum(obj2[i:i+1024]) for i in range(0, 10240, 1024))', 3.0)
        send_and_capture('block_read_time2 = time.ticks_diff(time.ticks_us(), start)')
        send_and_capture('gc.enable()')
        send_and_capture('block_read_speed2 = (10240 * 1000000) // block_read_time2')
        send_and_capture('print("Block read: {} μs ({} bytes/s)".format(block_read_time2, block_read_speed2))')
        
        # Summary comparison
        print('\n--- PERFORMANCE COMPARISON ---')
        send_and_capture('print("4KB object region:", region1)')
        send_and_capture('print("1MB object region:", region2)')
        send_and_capture('print("4KB write speed: {} bytes/s".format(write_speed1))')
        send_and_capture('print("1MB block write speed: {} bytes/s".format(block_write_speed2))')
        send_and_capture('print("4KB read speed: {} bytes/s".format(read_speed1))')
        send_and_capture('print("1MB block read speed: {} bytes/s".format(block_read_speed2))')
        
        # Performance ratio
        send_and_capture('if write_speed1 > block_write_speed2: print("Internal SRAM is faster for writes")')
        send_and_capture('else: print("OSPI RAM is competitive for writes")')
        send_and_capture('if read_speed1 > block_read_speed2: print("Internal SRAM is faster for reads")')
        send_and_capture('else: print("OSPI RAM is competitive for reads")')
        
        # Cleanup
        send_and_capture('del obj1, obj2, pattern')
        send_and_capture('gc.collect()')
        
        ser.close()
        print('\n✓ Direct speed test completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Direct Memory Speed Test")
    print("Testing performance with critical sections")
    print("=" * 50)
    direct_speed_test()
