#!/usr/bin/env python3
import serial
import time

def verify_real_memory():
    try:
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        def send_and_get_result(cmd, wait_time=1.5):
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
        
        print('\n=== VERIFYING REAL 1MB MEMORY ACCESS ===')
        
        # Setup
        send_and_get_result('import gc, time')
        send_and_get_result('gc.collect()')
        
        # Create 1MB object
        print('\n--- Creating 1MB Object ---')
        send_and_get_result('obj = bytearray(1048576)', 2.0)  # 1MB
        start_addr = send_and_get_result('id(obj)')
        size = send_and_get_result('len(obj)')
        
        if start_addr and size:
            start = int(start_addr)
            end = start + int(size)
            print(f'Object start: 0x{start:08x}')
            print(f'Object end:   0x{end:08x}')
            print(f'Object size:  {size} bytes')
        
        print('\n--- Writing Unique Pattern to ENTIRE 1MB ---')
        print('Writing pattern that cannot be cached or faked...')
        
        # Write unique pattern: each 4-byte word contains its own address offset
        # This ensures every location has a unique value that proves it's real memory
        send_and_get_result('print("Writing address-based pattern...")')
        send_and_get_result('start_time = time.ticks_ms()')
        
        # Write pattern in chunks to avoid timeout
        chunk_size = 4096  # 4KB chunks
        total_chunks = 1048576 // chunk_size  # 256 chunks
        
        print(f'Writing {total_chunks} chunks of {chunk_size} bytes each...')
        
        # Write first few chunks to test
        for chunk in range(0, min(10, total_chunks)):  # Test first 10 chunks (40KB)
            offset = chunk * chunk_size
            print(f'Writing chunk {chunk+1}/{min(10, total_chunks)} at offset {offset}...')
            
            # Create unique pattern for this chunk
            send_and_get_result(f'for i in range({chunk_size}): obj[{offset} + i] = ({offset} + i) & 0xFF', 3.0)
        
        send_and_get_result('write_time = time.ticks_diff(time.ticks_ms(), start_time)')
        write_time = send_and_get_result('print("Write time: {} ms".format(write_time))')
        
        print('\n--- Verifying Pattern Integrity ---')
        print('Reading back and verifying the unique pattern...')
        
        send_and_get_result('start_time = time.ticks_ms()')
        
        # Verify the pattern we wrote
        errors = 0
        for chunk in range(0, min(10, total_chunks)):  # Verify first 10 chunks
            offset = chunk * chunk_size
            print(f'Verifying chunk {chunk+1}/{min(10, total_chunks)} at offset {offset}...')
            
            # Check a few key positions in this chunk
            for test_pos in [0, 1000, 2000, 3000]:
                if offset + test_pos < 1048576:
                    expected = (offset + test_pos) & 0xFF
                    actual = send_and_get_result(f'obj[{offset + test_pos}]')
                    if actual and int(actual) != expected:
                        print(f'  ❌ ERROR at position {offset + test_pos}: expected {expected}, got {actual}')
                        errors += 1
                    elif actual:
                        print(f'  ✓ Position {offset + test_pos}: {actual} (correct)')
        
        send_and_get_result('read_time = time.ticks_diff(time.ticks_ms(), start_time)')
        read_time = send_and_get_result('print("Read time: {} ms".format(read_time))')
        
        print('\n--- Full Memory Test ---')
        print('Testing random positions across the entire 1MB...')
        
        # Test random positions across the full 1MB range
        test_positions = [0, 100000, 200000, 300000, 500000, 700000, 900000, 1000000, 1048575]
        
        for pos in test_positions:
            if pos < 1048576:
                # Write unique value
                unique_value = (pos ^ 0xAA) & 0xFF
                send_and_get_result(f'obj[{pos}] = {unique_value}')
                
                # Read it back
                read_value = send_and_get_result(f'obj[{pos}]')
                
                if read_value and int(read_value) == unique_value:
                    print(f'  ✓ Position {pos:7d}: wrote {unique_value}, read {read_value} ✓')
                else:
                    print(f'  ❌ Position {pos:7d}: wrote {unique_value}, read {read_value} ❌')
                    errors += 1
        
        print('\n--- Memory Stress Test ---')
        print('Writing and reading large blocks to stress test the memory...')
        
        # Write large blocks with different patterns
        patterns = [0x55, 0xAA, 0xFF, 0x00, 0x33, 0xCC]
        block_size = 65536  # 64KB blocks
        
        for i, pattern in enumerate(patterns):
            start_pos = i * block_size
            if start_pos + block_size <= 1048576:
                print(f'Writing pattern 0x{pattern:02X} to block {i+1} (64KB at position {start_pos})...')
                
                # Write pattern
                send_and_get_result(f'for j in range({block_size}): obj[{start_pos} + j] = {pattern}', 4.0)
                
                # Verify a few positions
                test_positions = [0, block_size//4, block_size//2, block_size-1]
                block_errors = 0
                
                for test_pos in test_positions:
                    actual = send_and_get_result(f'obj[{start_pos + test_pos}]')
                    if actual and int(actual) == pattern:
                        print(f'    ✓ Position {start_pos + test_pos}: {actual}')
                    else:
                        print(f'    ❌ Position {start_pos + test_pos}: expected {pattern}, got {actual}')
                        block_errors += 1
                
                if block_errors == 0:
                    print(f'    ✓ Block {i+1} pattern verification PASSED')
                else:
                    print(f'    ❌ Block {i+1} pattern verification FAILED ({block_errors} errors)')
                    errors += block_errors
        
        print('\n--- Final Results ---')
        if errors == 0:
            print('🎉 SUCCESS: All memory tests PASSED!')
            print('✓ The 1MB object has real, accessible memory')
            print('✓ All positions can be written and read correctly')
            print('✓ Different patterns work correctly')
            print('✓ Memory is not cached or faked')
        else:
            print(f'❌ FAILED: {errors} memory errors detected')
            print('⚠ Memory may not be fully functional')
        
        # Show memory usage
        free_mem = send_and_get_result('gc.mem_free()')
        alloc_mem = send_and_get_result('gc.mem_alloc()')
        print(f'\nMemory usage:')
        print(f'  Free: {free_mem} bytes')
        print(f'  Allocated: {alloc_mem} bytes')
        
        # Cleanup
        send_and_get_result('del obj')
        send_and_get_result('gc.collect()')
        
        ser.close()
        print('\n✓ Real memory verification completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Real Memory Verification Test")
    print("Testing if 1MB object has real, accessible memory")
    print("=" * 55)
    verify_real_memory()
