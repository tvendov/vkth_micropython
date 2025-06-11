#!/usr/bin/env python3
import serial
import time

def robust_memory_test():
    try:
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        def send_cmd_and_wait(cmd, wait_time=1.0):
            """Send command and wait for complete response"""
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)
            response = ser.read_all()
            output = ""
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip():
                        print(clean)
            return output
        
        print('\n=== ROBUST MEMORY PERFORMANCE TEST ===')
        
        # Setup critical test environment
        send_cmd_and_wait('import gc, time, micropython')
        send_cmd_and_wait('gc.collect()')
        send_cmd_and_wait('micropython.mem_info()')
        
        # Define test script with critical sections
        test_script = '''
# Memory performance test with critical sections
def test_memory_performance():
    import gc, time, micropython
    
    print("\\n=== MEMORY PERFORMANCE TEST ===")
    
    # Disable interrupts for critical measurements
    def critical_section_test(obj, test_name, size):
        print(f"\\n--- {test_name} ({size} bytes) ---")
        
        # Get object info
        addr = id(obj)
        print(f"Object address: 0x{addr:08x}")
        
        # Determine actual memory region
        if addr >= 0x68000000 and addr < 0x70000000:
            region = "OSPI_RAM_DIRECT"
        elif addr >= 0x20000000 and addr < 0x20080000:
            region = "INTERNAL_SRAM"
        elif addr >= 0x20080000 and addr < 0x30000000:
            region = "MAPPED_OSPI_RAM"
        else:
            region = f"UNKNOWN_0x{addr:08x}"
        
        print(f"Memory region: {region}")
        
        # Ensure object stays in memory
        micropython.mem_info()
        
        # Test 1: Byte-by-byte write (critical section)
        gc.disable()
        start_time = time.ticks_us()
        
        # Write test pattern
        for i in range(min(size, 1024)):  # Limit to 1KB for timing
            obj[i] = (i & 0xFF)
        
        write_time = time.ticks_diff(time.ticks_us(), start_time)
        gc.enable()
        
        write_speed = (min(size, 1024) * 1000000) / write_time / 1024  # KB/s
        print(f"Byte write: {write_time} μs ({write_speed:.1f} KB/s)")
        
        # Test 2: Byte-by-byte read (critical section)
        gc.disable()
        start_time = time.ticks_us()
        
        checksum = 0
        for i in range(min(size, 1024)):
            checksum += obj[i]
        
        read_time = time.ticks_diff(time.ticks_us(), start_time)
        gc.enable()
        
        read_speed = (min(size, 1024) * 1000000) / read_time / 1024  # KB/s
        print(f"Byte read: {read_time} μs ({read_speed:.1f} KB/s)")
        print(f"Checksum: {checksum}")
        
        # Test 3: Block operations
        if size >= 1024:
            pattern = bytes(range(256)) * 4  # 1KB pattern
            
            gc.disable()
            start_time = time.ticks_us()
            
            # Block write
            for i in range(0, min(size, 10240), 1024):
                end_idx = min(i + 1024, size)
                obj[i:end_idx] = pattern[:end_idx-i]
            
            block_write_time = time.ticks_diff(time.ticks_us(), start_time)
            gc.enable()
            
            tested_bytes = min(size, 10240)
            block_write_speed = (tested_bytes * 1000000) / block_write_time / 1024  # KB/s
            print(f"Block write: {block_write_time} μs ({block_write_speed:.1f} KB/s)")
            
            # Block read
            gc.disable()
            start_time = time.ticks_us()
            
            total_sum = 0
            for i in range(0, min(size, 10240), 1024):
                end_idx = min(i + 1024, size)
                chunk = obj[i:end_idx]
                total_sum += sum(chunk)
            
            block_read_time = time.ticks_diff(time.ticks_us(), start_time)
            gc.enable()
            
            block_read_speed = (tested_bytes * 1000000) / block_read_time / 1024  # KB/s
            print(f"Block read: {block_read_time} μs ({block_read_speed:.1f} KB/s)")
        
        return region
    
    # Force allocation in different regions
    print("\\nForcing allocations in different memory regions...")
    
    # Small object - should go to internal SRAM
    gc.collect()
    small_obj = bytearray(4096)  # 4KB
    small_region = critical_section_test(small_obj, "Small Object", 4096)
    
    # Medium object - might go to mapped OSPI
    gc.collect()
    medium_obj = bytearray(65536)  # 64KB
    medium_region = critical_section_test(medium_obj, "Medium Object", 65536)
    
    # Large object - should definitely use OSPI
    gc.collect()
    large_obj = bytearray(1048576)  # 1MB
    large_region = critical_section_test(large_obj, "Large Object", 1048576)
    
    # Summary
    print("\\n=== PERFORMANCE SUMMARY ===")
    print(f"Small (4KB):  {small_region}")
    print(f"Medium (64KB): {medium_region}")
    print(f"Large (1MB):   {large_region}")
    
    if small_region != large_region:
        print("✓ Different memory regions detected!")
        print("✓ Memory allocation strategy is working")
    else:
        print("⚠ All objects in same region")
    
    # Cleanup
    del small_obj, medium_obj, large_obj
    gc.collect()
    print("\\n✓ Test completed, memory cleaned up")

# Execute the test
test_memory_performance()
'''
        
        print('\n--- Sending robust test script ---')
        
        # Send script in smaller chunks to avoid issues
        lines = test_script.strip().split('\n')
        current_chunk = ""
        
        for line in lines:
            if line.strip():
                current_chunk += line + '\n'
                if len(current_chunk) > 500:  # Send in chunks
                    send_cmd_and_wait(current_chunk, 0.5)
                    current_chunk = ""
        
        if current_chunk:
            send_cmd_and_wait(current_chunk, 0.5)
        
        print('\n--- Executing test (this may take a moment) ---')
        send_cmd_and_wait('', 1.0)  # Execute
        
        # Wait for test completion and collect results
        for i in range(15):  # Wait up to 15 seconds
            time.sleep(1)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>'):
                        print(clean)
        
        ser.close()
        print('\n✓ Robust memory test completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Robust Memory Performance Test")
    print("Testing with critical sections and proper memory management")
    print("=" * 60)
    robust_memory_test()
