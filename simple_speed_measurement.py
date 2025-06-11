#!/usr/bin/env python3
import serial
import time

def simple_speed_measurement():
    try:
        ser = serial.Serial('COM20', 115200, timeout=2)
        print('✓ Connected to COM20')
        
        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        def send_cmd_get_number(cmd):
            """Send command and extract numeric result"""
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(1.5)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip() and not clean.startswith('...'):
                        try:
                            return int(clean)
                        except:
                            print(f"  → {clean}")
                            if clean.isdigit():
                                return int(clean)
            return None
        
        print('\n=== SIMPLE SPEED MEASUREMENT ===')
        
        # Setup
        ser.write(b'import gc, time\r\n')
        time.sleep(1)
        ser.read_all()
        
        ser.write(b'gc.collect()\r\n')
        time.sleep(1)
        ser.read_all()
        
        # Create objects
        print('\n--- Creating Objects ---')
        ser.write(b'small_obj = bytearray(50000)\r\n')  # 50KB
        time.sleep(1)
        ser.read_all()
        
        ser.write(b'large_obj = bytearray(1000000)\r\n')  # 1MB
        time.sleep(2)
        ser.read_all()
        
        print('Objects created successfully')
        
        # Test 1: Small object speed (Internal SRAM)
        print('\n--- Test 1: Internal SRAM Speed ---')
        
        # Write test
        print('Writing to Internal SRAM...')
        write_time_sram = send_cmd_get_number('start=time.ticks_us(); [small_obj.__setitem__(i, 0xAA) for i in range(1000)]; time.ticks_diff(time.ticks_us(), start)')
        
        if write_time_sram:
            write_speed_sram = (1000 * 1000000) // write_time_sram
            print(f'SRAM Write: {write_time_sram} μs → {write_speed_sram:,} bytes/sec ({write_speed_sram/1024:.1f} KB/s)')
        
        # Read test
        print('Reading from Internal SRAM...')
        read_time_sram = send_cmd_get_number('start=time.ticks_us(); sum(small_obj[i] for i in range(1000)); time.ticks_diff(time.ticks_us(), start)')
        
        if read_time_sram:
            read_speed_sram = (1000 * 1000000) // read_time_sram
            print(f'SRAM Read:  {read_time_sram} μs → {read_speed_sram:,} bytes/sec ({read_speed_sram/1024:.1f} KB/s)')
        
        # Test 2: Large object speed (OSPI region)
        print('\n--- Test 2: OSPI RAM Speed ---')
        
        # Write test to end of large object (definitely OSPI)
        print('Writing to OSPI RAM...')
        write_time_ospi = send_cmd_get_number('start=time.ticks_us(); [large_obj.__setitem__(500000+i, 0x55) for i in range(1000)]; time.ticks_diff(time.ticks_us(), start)')
        
        if write_time_ospi:
            write_speed_ospi = (1000 * 1000000) // write_time_ospi
            print(f'OSPI Write: {write_time_ospi} μs → {write_speed_ospi:,} bytes/sec ({write_speed_ospi/1024:.1f} KB/s)')
        
        # Read test from OSPI region
        print('Reading from OSPI RAM...')
        read_time_ospi = send_cmd_get_number('start=time.ticks_us(); sum(large_obj[500000+i] for i in range(1000)); time.ticks_diff(time.ticks_us(), start)')
        
        if read_time_ospi:
            read_speed_ospi = (1000 * 1000000) // read_time_ospi
            print(f'OSPI Read:  {read_time_ospi} μs → {read_speed_ospi:,} bytes/sec ({read_speed_ospi/1024:.1f} KB/s)')
        
        # Performance comparison
        print('\n=== PERFORMANCE COMPARISON ===')
        
        if write_time_sram and write_time_ospi:
            if write_time_sram < write_time_ospi:
                ratio = write_time_ospi / write_time_sram
                print(f'✓ Internal SRAM writes are {ratio:.1f}x FASTER than OSPI')
            else:
                ratio = write_time_sram / write_time_ospi
                print(f'✓ OSPI writes are {ratio:.1f}x FASTER than Internal SRAM')
        
        if read_time_sram and read_time_ospi:
            if read_time_sram < read_time_ospi:
                ratio = read_time_ospi / read_time_sram
                print(f'✓ Internal SRAM reads are {ratio:.1f}x FASTER than OSPI')
            else:
                ratio = read_time_sram / read_time_ospi
                print(f'✓ OSPI reads are {ratio:.1f}x FASTER than Internal SRAM')
        
        print('\n--- Final Results ---')
        print('Memory Region | Write Speed | Read Speed')
        print('-------------|-------------|------------')
        if write_time_sram and read_time_sram:
            print(f'Internal SRAM | {write_speed_sram:,} B/s | {read_speed_sram:,} B/s')
        if write_time_ospi and read_time_ospi:
            print(f'OSPI RAM      | {write_speed_ospi:,} B/s | {read_speed_ospi:,} B/s')
        
        # Cleanup
        ser.write(b'del small_obj, large_obj\r\n')
        time.sleep(1)
        ser.read_all()
        
        ser.close()
        print('\n✓ Speed measurement completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Simple Memory Speed Measurement")
    print("Direct measurement of bytes/sec")
    print("=" * 40)
    simple_speed_measurement()
