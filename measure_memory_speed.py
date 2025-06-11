#!/usr/bin/env python3
import serial
import time

def measure_memory_speed():
    try:
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        def send_and_get_result(cmd, wait_time=2.0):
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip() and not clean.startswith('...'):
                        print(f"  → {clean}")
                        return clean
            return None
        
        print('\n=== MEMORY SPEED MEASUREMENT ===')
        
        # Setup
        send_and_get_result('import gc, time')
        send_and_get_result('gc.collect()')
        
        print('\n--- Creating Test Objects ---')
        
        # Small object (100KB) - entirely in Internal SRAM
        send_and_get_result('small_obj = bytearray(102400)')  # 100KB
        small_start = send_and_get_result('id(small_obj)')
        print(f'Small object (100KB): 0x{int(small_start):08x} - INTERNAL SRAM')
        
        # Large object (1MB) - spans both regions  
        send_and_get_result('large_obj = bytearray(1048576)')  # 1MB
        large_start = send_and_get_result('id(large_obj)')
        print(f'Large object (1MB): 0x{int(large_start):08x} - SPANS REGIONS')
        
        print('\n=== SPEED TEST 1: INTERNAL SRAM (Small Object) ===')
        
        # Test 1: Write speed to Internal SRAM
        test_size = 10240  # 10KB test
        print(f'Writing {test_size} bytes to Internal SRAM...')
        
        send_and_get_result('gc.disable()')
        send_and_get_result('start_time = time.ticks_us()')
        send_and_get_result(f'for i in range({test_size}): small_obj[i] = i & 0xFF', 3.0)
        write_time_sram = send_and_get_result('time.ticks_diff(time.ticks_us(), start_time)')
        send_and_get_result('gc.enable()')
        
        if write_time_sram:
            write_time_us = int(write_time_sram)
            write_speed_sram = (test_size * 1000000) // write_time_us if write_time_us > 0 else 0
            print(f'SRAM Write: {write_time_us} μs = {write_speed_sram:,} bytes/sec ({write_speed_sram/1024:.1f} KB/s)')
        
        # Test 1: Read speed from Internal SRAM
        print(f'Reading {test_size} bytes from Internal SRAM...')
        
        send_and_get_result('gc.disable()')
        send_and_get_result('start_time = time.ticks_us()')
        send_and_get_result(f'checksum = sum(small_obj[i] for i in range({test_size}))', 3.0)
        read_time_sram = send_and_get_result('time.ticks_diff(time.ticks_us(), start_time)')
        send_and_get_result('gc.enable()')
        
        if read_time_sram:
            read_time_us = int(read_time_sram)
            read_speed_sram = (test_size * 1000000) // read_time_us if read_time_us > 0 else 0
            print(f'SRAM Read:  {read_time_us} μs = {read_speed_sram:,} bytes/sec ({read_speed_sram/1024:.1f} KB/s)')
        
        print('\n=== SPEED TEST 2: MIXED REGIONS (Large Object) ===')
        
        # Test 2: Write speed to Mixed regions (mostly OSPI)
        print(f'Writing {test_size} bytes to Mixed regions...')
        
        send_and_get_result('gc.disable()')
        send_and_get_result('start_time = time.ticks_us()')
        send_and_get_result(f'for i in range({test_size}): large_obj[i] = i & 0xFF', 3.0)
        write_time_mixed = send_and_get_result('time.ticks_diff(time.ticks_us(), start_time)')
        send_and_get_result('gc.enable()')
        
        if write_time_mixed:
            write_time_us = int(write_time_mixed)
            write_speed_mixed = (test_size * 1000000) // write_time_us if write_time_us > 0 else 0
            print(f'Mixed Write: {write_time_us} μs = {write_speed_mixed:,} bytes/sec ({write_speed_mixed/1024:.1f} KB/s)')
        
        # Test 2: Read speed from Mixed regions
        print(f'Reading {test_size} bytes from Mixed regions...')
        
        send_and_get_result('gc.disable()')
        send_and_get_result('start_time = time.ticks_us()')
        send_and_get_result(f'checksum = sum(large_obj[i] for i in range({test_size}))', 3.0)
        read_time_mixed = send_and_get_result('time.ticks_diff(time.ticks_us(), start_time)')
        send_and_get_result('gc.enable()')
        
        if read_time_mixed:
            read_time_us = int(read_time_mixed)
            read_speed_mixed = (test_size * 1000000) // read_time_us if read_time_us > 0 else 0
            print(f'Mixed Read:  {read_time_us} μs = {read_speed_mixed:,} bytes/sec ({read_speed_mixed/1024:.1f} KB/s)')
        
        print('\n=== SPEED TEST 3: OSPI REGION (End of Large Object) ===')
        
        # Test 3: Test the end of large object (definitely in OSPI region)
        ospi_offset = 600000  # 600KB into the 1MB object = definitely in OSPI region
        print(f'Writing {test_size} bytes to OSPI region (offset {ospi_offset})...')
        
        send_and_get_result('gc.disable()')
        send_and_get_result('start_time = time.ticks_us()')
        send_and_get_result(f'for i in range({test_size}): large_obj[{ospi_offset} + i] = i & 0xFF', 3.0)
        write_time_ospi = send_and_get_result('time.ticks_diff(time.ticks_us(), start_time)')
        send_and_get_result('gc.enable()')
        
        if write_time_ospi:
            write_time_us = int(write_time_ospi)
            write_speed_ospi = (test_size * 1000000) // write_time_us if write_time_us > 0 else 0
            print(f'OSPI Write: {write_time_us} μs = {write_speed_ospi:,} bytes/sec ({write_speed_ospi/1024:.1f} KB/s)')
        
        # Test 3: Read from OSPI region
        print(f'Reading {test_size} bytes from OSPI region (offset {ospi_offset})...')
        
        send_and_get_result('gc.disable()')
        send_and_get_result('start_time = time.ticks_us()')
        send_and_get_result(f'checksum = sum(large_obj[{ospi_offset} + i] for i in range({test_size}))', 3.0)
        read_time_ospi = send_and_get_result('time.ticks_diff(time.ticks_us(), start_time)')
        send_and_get_result('gc.enable()')
        
        if read_time_ospi:
            read_time_us = int(read_time_ospi)
            read_speed_ospi = (test_size * 1000000) // read_time_us if read_time_us > 0 else 0
            print(f'OSPI Read:  {read_time_us} μs = {read_speed_ospi:,} bytes/sec ({read_speed_ospi/1024:.1f} KB/s)')
        
        print('\n=== PERFORMANCE COMPARISON ===')
        
        # Compare write speeds
        if 'write_speed_sram' in locals() and 'write_speed_ospi' in locals():
            if write_speed_sram > write_speed_ospi:
                ratio = write_speed_sram / write_speed_ospi if write_speed_ospi > 0 else float('inf')
                print(f'✓ Internal SRAM is {ratio:.1f}x FASTER for writes')
            elif write_speed_ospi > write_speed_sram:
                ratio = write_speed_ospi / write_speed_sram if write_speed_sram > 0 else float('inf')
                print(f'✓ OSPI RAM is {ratio:.1f}x FASTER for writes')
            else:
                print('= Write speeds are similar')
        
        # Compare read speeds
        if 'read_speed_sram' in locals() and 'read_speed_ospi' in locals():
            if read_speed_sram > read_speed_ospi:
                ratio = read_speed_sram / read_speed_ospi if read_speed_ospi > 0 else float('inf')
                print(f'✓ Internal SRAM is {ratio:.1f}x FASTER for reads')
            elif read_speed_ospi > read_speed_sram:
                ratio = read_speed_ospi / read_speed_sram if read_speed_sram > 0 else float('inf')
                print(f'✓ OSPI RAM is {ratio:.1f}x FASTER for reads')
            else:
                print('= Read speeds are similar')
        
        print('\n--- Summary ---')
        print(f'Test size: {test_size:,} bytes ({test_size/1024:.1f} KB)')
        if 'write_speed_sram' in locals():
            print(f'Internal SRAM: Write {write_speed_sram:,} B/s, Read {read_speed_sram:,} B/s')
        if 'write_speed_ospi' in locals():
            print(f'OSPI RAM:      Write {write_speed_ospi:,} B/s, Read {read_speed_ospi:,} B/s')
        
        # Cleanup
        send_and_get_result('del small_obj, large_obj')
        send_and_get_result('gc.collect()')
        
        ser.close()
        print('\n✓ Memory speed measurement completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Memory Speed Measurement")
    print("Measuring bytes/sec for different memory regions")
    print("=" * 50)
    measure_memory_speed()
