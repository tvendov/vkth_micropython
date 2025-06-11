#!/usr/bin/env python3
import serial
import time

def final_speed_test():
    try:
        ser = serial.Serial('COM20', 115200, timeout=2)
        print('✓ Connected to COM20')

        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()

        def send_and_wait(cmd):
            """Send command and wait for response"""
            print(f"Sending: {cmd}")
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(2.0)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                print(f"Response: {output.strip()}")
                # Extract numeric result if present
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean.isdigit():
                        return int(clean)
            return None

        print('\n=== FINAL MEMORY SPEED TEST ===')

        # Setup
        send_and_wait('import gc, time')
        send_and_wait('gc.collect()')

        # Create test objects
        print('\n--- Creating Test Objects ---')
        send_and_wait('small_obj = bytearray(50000)')  # 50KB - should be in Internal SRAM
        send_and_wait('large_obj = bytearray(1000000)')  # 1MB - spans regions

        # Show object locations
        small_addr = send_and_wait('id(small_obj)')
        large_addr = send_and_wait('id(large_obj)')

        if small_addr:
            print(f'Small object: 0x{small_addr:08x}')
        if large_addr:
            print(f'Large object: 0x{large_addr:08x}')

        print('\n=== SPEED TEST: INTERNAL SRAM ===')

        # Test 1: Write to Internal SRAM (small object)
        print('Testing write speed to Internal SRAM...')
        write_time_sram = send_and_wait('start=time.ticks_us(); exec("for i in range(1000): small_obj[i] = 0xAA"); time.ticks_diff(time.ticks_us(), start)')

        if write_time_sram and write_time_sram > 0:
            write_speed_sram = (1000 * 1000000) // write_time_sram
            print(f'SRAM Write: {write_time_sram} μs = {write_speed_sram:,} bytes/sec ({write_speed_sram/1024:.1f} KB/s)')

        # Test 1: Read from Internal SRAM
        print('Testing read speed from Internal SRAM...')
        read_time_sram = send_and_wait('start=time.ticks_us(); exec("total = sum(small_obj[i] for i in range(1000))"); time.ticks_diff(time.ticks_us(), start)')

        if read_time_sram and read_time_sram > 0:
            read_speed_sram = (1000 * 1000000) // read_time_sram
            print(f'SRAM Read:  {read_time_sram} μs = {read_speed_sram:,} bytes/sec ({read_speed_sram/1024:.1f} KB/s)')

        print('\n=== SPEED TEST: OSPI RAM ===')

        # Test 2: Write to OSPI RAM (end of large object)
        print('Testing write speed to OSPI RAM...')
        write_time_ospi = send_and_wait('start=time.ticks_us(); exec("for i in range(1000): large_obj[500000+i] = 0x55"); time.ticks_diff(time.ticks_us(), start)')

        if write_time_ospi and write_time_ospi > 0:
            write_speed_ospi = (1000 * 1000000) // write_time_ospi
            print(f'OSPI Write: {write_time_ospi} μs = {write_speed_ospi:,} bytes/sec ({write_speed_ospi/1024:.1f} KB/s)')

        # Test 2: Read from OSPI RAM
        print('Testing read speed from OSPI RAM...')
        read_time_ospi = send_and_wait('start=time.ticks_us(); exec("total = sum(large_obj[500000+i] for i in range(1000))"); time.ticks_diff(time.ticks_us(), start)')

        if read_time_ospi and read_time_ospi > 0:
            read_speed_ospi = (1000 * 1000000) // read_time_ospi
            print(f'OSPI Read:  {read_time_ospi} μs = {read_speed_ospi:,} bytes/sec ({read_speed_ospi/1024:.1f} KB/s)')

        print('\n=== PERFORMANCE ANALYSIS ===')

        # Compare write performance
        if write_time_sram and write_time_ospi:
            if write_time_sram < write_time_ospi:
                ratio = write_time_ospi / write_time_sram
                faster_region = "Internal SRAM"
                print(f'✓ {faster_region} writes are {ratio:.1f}x FASTER')
            elif write_time_ospi < write_time_sram:
                ratio = write_time_sram / write_time_ospi
                faster_region = "OSPI RAM"
                print(f'✓ {faster_region} writes are {ratio:.1f}x FASTER')
            else:
                print('= Write speeds are similar')

        # Compare read performance
        if read_time_sram and read_time_ospi:
            if read_time_sram < read_time_ospi:
                ratio = read_time_ospi / read_time_sram
                faster_region = "Internal SRAM"
                print(f'✓ {faster_region} reads are {ratio:.1f}x FASTER')
            elif read_time_ospi < read_time_sram:
                ratio = read_time_sram / read_time_ospi
                faster_region = "OSPI RAM"
                print(f'✓ {faster_region} reads are {ratio:.1f}x FASTER')
            else:
                print('= Read speeds are similar')

        print('\n=== FINAL RESULTS SUMMARY ===')
        print('Memory Type   | Write Speed    | Read Speed')
        print('-------------|----------------|----------------')

        if 'write_speed_sram' in locals() and 'read_speed_sram' in locals():
            print(f'Internal SRAM | {write_speed_sram:8,} B/s | {read_speed_sram:8,} B/s')

        if 'write_speed_ospi' in locals() and 'read_speed_ospi' in locals():
            print(f'OSPI RAM      | {write_speed_ospi:8,} B/s | {read_speed_ospi:8,} B/s')

        # Cleanup
        send_and_wait('del small_obj, large_obj')
        send_and_wait('gc.collect()')

        ser.close()
        print('\n✓ Final speed test completed!')

    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Final Memory Speed Test")
    print("Measuring actual bytes/sec performance")
    print("=" * 45)
    final_speed_test()
