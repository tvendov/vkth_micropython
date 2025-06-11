#!/usr/bin/env python3
import serial
import time

def memory_speed_test():
    try:
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Just interrupt, don't reset
        ser.write(b'\x03')
        time.sleep(0.3)
        ser.read_all()
        
        def send_cmd(cmd, wait_time=0.8):
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
        
        print('\n=== MEMORY PERFORMANCE TEST ===')
        
        # Setup test environment
        send_cmd('import gc, time')
        send_cmd('gc.collect()')
        
        # Create test script for memory performance
        test_script = '''
def memory_speed_test():
    import gc, time
    
    print("=== MEMORY SPEED TEST ===")
    
    # Test parameters
    test_sizes = [1024, 10240, 102400, 1048576]  # 1KB, 10KB, 100KB, 1MB
    test_names = ["1KB", "10KB", "100KB", "1MB"]
    
    for size, name in zip(test_sizes, test_names):
        print(f"\\n--- Testing {name} ({size:,} bytes) ---")
        
        # Allocate object
        start_time = time.ticks_us()
        obj = bytearray(size)
        alloc_time = time.ticks_diff(time.ticks_us(), start_time)
        
        addr = id(obj)
        print(f"Address: 0x{addr:08x}")
        
        # Determine memory region
        if addr < 0x20000000:
            region = "Flash/ROM"
        elif addr < 0x30000000:
            region = "Internal SRAM"
        elif addr < 0x70000000:
            region = "External RAM (OSPI)"
        else:
            region = "Other"
        print(f"Region: {region}")
        print(f"Allocation time: {alloc_time} μs")
        
        # Write speed test
        test_data = 0xAA
        start_time = time.ticks_us()
        for i in range(size):
            obj[i] = test_data
        write_time = time.ticks_diff(time.ticks_us(), start_time)
        
        write_speed = (size / write_time) * 1000000 / 1024 / 1024  # MB/s
        print(f"Write time: {write_time} μs ({write_speed:.2f} MB/s)")
        
        # Read speed test
        start_time = time.ticks_us()
        checksum = 0
        for i in range(size):
            checksum += obj[i]
        read_time = time.ticks_diff(time.ticks_us(), start_time)
        
        read_speed = (size / read_time) * 1000000 / 1024 / 1024  # MB/s
        print(f"Read time: {read_time} μs ({read_speed:.2f} MB/s)")
        print(f"Checksum: {checksum} (expected: {test_data * size})")
        
        # Block write test (faster)
        test_pattern = bytes([0x55] * min(1024, size))
        start_time = time.ticks_us()
        for i in range(0, size, len(test_pattern)):
            end_idx = min(i + len(test_pattern), size)
            obj[i:end_idx] = test_pattern[:end_idx-i]
        block_write_time = time.ticks_diff(time.ticks_us(), start_time)
        
        block_write_speed = (size / block_write_time) * 1000000 / 1024 / 1024
        print(f"Block write time: {block_write_time} μs ({block_write_speed:.2f} MB/s)")
        
        # Block read test
        start_time = time.ticks_us()
        total_bytes = 0
        for i in range(0, size, 1024):
            end_idx = min(i + 1024, size)
            chunk = obj[i:end_idx]
            total_bytes += len(chunk)
        block_read_time = time.ticks_diff(time.ticks_us(), start_time)
        
        block_read_speed = (size / block_read_time) * 1000000 / 1024 / 1024
        print(f"Block read time: {block_read_time} μs ({block_read_speed:.2f} MB/s)")
        
        # Clean up
        del obj
        gc.collect()
        
        # Small delay between tests
        time.sleep_ms(100)
    
    print("\\n=== COMPARATIVE ANALYSIS ===")
    print("Expected results:")
    print("- Internal SRAM: Very fast (>100 MB/s)")
    print("- OSPI RAM: Slower but still good (10-50 MB/s)")
    print("- Block operations should be faster than byte-by-byte")

# Run the test
memory_speed_test()
'''
        
        print('\n--- Sending performance test script ---')
        
        # Send the script line by line to avoid issues
        lines = test_script.strip().split('\n')
        for line in lines:
            if line.strip():
                send_cmd(line, 0.3)
        
        print('\n--- Executing test ---')
        send_cmd('', 0.5)  # Execute the function
        
        # Wait for test completion and read results
        print('\n--- Test Results ---')
        time.sleep(2)
        
        # Read any remaining output
        for _ in range(10):
            time.sleep(1)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>'):
                        print(clean)
            else:
                break
        
        ser.close()
        print('\n✓ Memory speed test completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("MicroPython Memory Speed Test")
    print("Testing read/write performance in different memory regions")
    print("=" * 60)
    memory_speed_test()
