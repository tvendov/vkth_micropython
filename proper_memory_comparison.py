#!/usr/bin/env python3
import serial
import time

def proper_memory_comparison():
    try:
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        def send_and_get_result(cmd, wait_time=1.0):
            """Send command and extract the actual result"""
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
        
        print('\n=== PROPER MEMORY REGION COMPARISON ===')
        
        # Setup
        send_and_get_result('import gc, time')
        send_and_get_result('gc.collect()')
        
        print('\n--- Step 1: Force allocation in different regions ---')
        
        # Strategy: Allocate many small objects first to fill internal SRAM,
        # then allocate large objects that should go to OSPI
        
        # Fill internal SRAM with small objects
        print('Filling internal SRAM with small objects...')
        send_and_get_result('small_objects = []')
        send_and_get_result('for i in range(50): small_objects.append(bytearray(8192))', 3.0)  # 50 x 8KB = 400KB
        
        # Check memory state
        free_after_small = send_and_get_result('gc.mem_free()')
        print(f'Free memory after small objects: {free_after_small}')
        
        # Now allocate a large object - should go to OSPI region
        print('Allocating large object (should go to OSPI)...')
        send_and_get_result('large_obj = bytearray(2097152)', 2.0)  # 2MB
        addr_large = send_and_get_result('id(large_obj)')
        
        # Get one small object for comparison
        send_and_get_result('small_obj = small_objects[0]')
        addr_small = send_and_get_result('id(small_obj)')
        
        print('\n--- Step 2: Verify different memory regions ---')
        print(f'Small object address: {addr_small}')
        print(f'Large object address: {addr_large}')
        
        # Determine regions
        if addr_small:
            addr_val = int(addr_small) if addr_small.isdigit() else 0
            if 0x20000000 <= addr_val < 0x20080000:
                region_small = "INTERNAL_SRAM"
            elif 0x20080000 <= addr_val < 0x30000000:
                region_small = "MAPPED_OSPI"
            elif 0x68000000 <= addr_val < 0x70000000:
                region_small = "DIRECT_OSPI"
            else:
                region_small = f"UNKNOWN_0x{addr_val:08x}"
            print(f'Small object region: {region_small}')
        
        if addr_large:
            addr_val = int(addr_large) if addr_large.isdigit() else 0
            if 0x20000000 <= addr_val < 0x20080000:
                region_large = "INTERNAL_SRAM"
            elif 0x20080000 <= addr_val < 0x30000000:
                region_large = "MAPPED_OSPI"
            elif 0x68000000 <= addr_val < 0x70000000:
                region_large = "DIRECT_OSPI"
            else:
                region_large = f"UNKNOWN_0x{addr_val:08x}"
            print(f'Large object region: {region_large}')
        
        # Verify they are in different regions
        if 'region_small' in locals() and 'region_large' in locals():
            if region_small != region_large:
                print('✓ SUCCESS: Objects are in different memory regions!')
                proceed_with_test = True
            else:
                print('❌ FAILED: Both objects are in the same region!')
                print('Cannot perform meaningful speed comparison.')
                proceed_with_test = False
        else:
            print('❌ Could not determine memory regions')
            proceed_with_test = False
        
        if proceed_with_test:
            print('\n--- Step 3: Speed comparison test ---')
            
            # Test parameters
            test_size = 1024  # Test 1KB of data
            
            # Speed test for small object (internal SRAM)
            print(f'Testing {region_small} speed...')
            send_and_get_result('gc.disable()')
            send_and_get_result('start = time.ticks_us()')
            send_and_get_result(f'for i in range({test_size}): small_obj[i] = i & 0xFF', 2.0)
            write_time_small = send_and_get_result('time.ticks_diff(time.ticks_us(), start)')
            send_and_get_result('gc.enable()')
            
            send_and_get_result('gc.disable()')
            send_and_get_result('start = time.ticks_us()')
            send_and_get_result(f'checksum = sum(small_obj[i] for i in range({test_size}))', 2.0)
            read_time_small = send_and_get_result('time.ticks_diff(time.ticks_us(), start)')
            send_and_get_result('gc.enable()')
            
            # Speed test for large object (OSPI RAM)
            print(f'Testing {region_large} speed...')
            send_and_get_result('gc.disable()')
            send_and_get_result('start = time.ticks_us()')
            send_and_get_result(f'for i in range({test_size}): large_obj[i] = i & 0xFF', 2.0)
            write_time_large = send_and_get_result('time.ticks_diff(time.ticks_us(), start)')
            send_and_get_result('gc.enable()')
            
            send_and_get_result('gc.disable()')
            send_and_get_result('start = time.ticks_us()')
            send_and_get_result(f'checksum = sum(large_obj[i] for i in range({test_size}))', 2.0)
            read_time_large = send_and_get_result('time.ticks_diff(time.ticks_us(), start)')
            send_and_get_result('gc.enable()')
            
            print('\n--- Step 4: Results Analysis ---')
            print(f'{region_small} (small object):')
            print(f'  Write {test_size} bytes: {write_time_small} μs')
            print(f'  Read {test_size} bytes: {read_time_small} μs')
            
            print(f'{region_large} (large object):')
            print(f'  Write {test_size} bytes: {write_time_large} μs')
            print(f'  Read {test_size} bytes: {read_time_large} μs')
            
            # Calculate performance difference
            try:
                if (write_time_small and write_time_large and 
                    write_time_small.isdigit() and write_time_large.isdigit()):
                    wt_small = int(write_time_small)
                    wt_large = int(write_time_large)
                    
                    if wt_small < wt_large:
                        ratio = wt_large / wt_small
                        print(f'\n✓ {region_small} is {ratio:.1f}x FASTER for writes')
                    else:
                        ratio = wt_small / wt_large
                        print(f'\n✓ {region_large} is {ratio:.1f}x FASTER for writes')
                
                if (read_time_small and read_time_large and 
                    read_time_small.isdigit() and read_time_large.isdigit()):
                    rt_small = int(read_time_small)
                    rt_large = int(read_time_large)
                    
                    if rt_small < rt_large:
                        ratio = rt_large / rt_small
                        print(f'✓ {region_small} is {ratio:.1f}x FASTER for reads')
                    else:
                        ratio = rt_small / rt_large
                        print(f'✓ {region_large} is {ratio:.1f}x FASTER for reads')
                        
            except Exception as e:
                print(f'Could not calculate performance ratios: {e}')
        
        # Cleanup
        send_and_get_result('del small_objects, large_obj, small_obj')
        send_and_get_result('gc.collect()')
        
        ser.close()
        print('\n✓ Proper memory comparison completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Proper Memory Region Speed Comparison")
    print("Ensuring objects are in different memory regions")
    print("=" * 50)
    proper_memory_comparison()
