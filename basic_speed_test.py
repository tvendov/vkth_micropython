#!/usr/bin/env python3
import serial
import time

def basic_speed_test():
    try:
        ser = serial.Serial('COM20', 115200, timeout=3)
        print('✓ Connected to COM20')
        
        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        def send_simple_cmd(cmd):
            """Send simple command and get result"""
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(1.0)
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
        
        print('\n=== BASIC MEMORY SPEED TEST ===')
        
        # Setup
        send_simple_cmd('import gc, time')
        send_simple_cmd('gc.collect()')
        
        print('\n--- Creating Objects ---')
        
        # Create small object
        send_simple_cmd('small_obj = bytearray(4096)')
        addr_small = send_simple_cmd('id(small_obj)')
        print(f'Small object address: {addr_small}')
        
        # Create large object  
        send_simple_cmd('large_obj = bytearray(1048576)')
        addr_large = send_simple_cmd('id(large_obj)')
        print(f'Large object address: {addr_large}')
        
        print('\n--- Memory Regions ---')
        
        # Check regions
        if addr_small:
            addr_val = int(addr_small) if addr_small.isdigit() else 0
            if 0x20000000 <= addr_val < 0x20080000:
                region_small = "INTERNAL_SRAM"
            elif 0x20080000 <= addr_val < 0x30000000:
                region_small = "MAPPED_OSPI"
            elif 0x68000000 <= addr_val < 0x70000000:
                region_small = "DIRECT_OSPI"
            else:
                region_small = "UNKNOWN"
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
                region_large = "UNKNOWN"
            print(f'Large object region: {region_large}')
        
        print('\n--- Speed Tests ---')
        
        # Simple write test for small object
        print('Testing small object write speed...')
        send_simple_cmd('start_time = time.ticks_us()')
        send_simple_cmd('small_obj[0] = 0xAA')
        send_simple_cmd('small_obj[1000] = 0xBB')
        send_simple_cmd('small_obj[2000] = 0xCC')
        send_simple_cmd('small_obj[3000] = 0xDD')
        write_time_small = send_simple_cmd('time.ticks_diff(time.ticks_us(), start_time)')
        print(f'Small object write time: {write_time_small} μs')
        
        # Simple read test for small object
        print('Testing small object read speed...')
        send_simple_cmd('start_time = time.ticks_us()')
        send_simple_cmd('val1 = small_obj[0]')
        send_simple_cmd('val2 = small_obj[1000]')
        send_simple_cmd('val3 = small_obj[2000]')
        send_simple_cmd('val4 = small_obj[3000]')
        read_time_small = send_simple_cmd('time.ticks_diff(time.ticks_us(), start_time)')
        print(f'Small object read time: {read_time_small} μs')
        
        # Simple write test for large object
        print('Testing large object write speed...')
        send_simple_cmd('start_time = time.ticks_us()')
        send_simple_cmd('large_obj[0] = 0x11')
        send_simple_cmd('large_obj[100000] = 0x22')
        send_simple_cmd('large_obj[500000] = 0x33')
        send_simple_cmd('large_obj[1000000] = 0x44')
        write_time_large = send_simple_cmd('time.ticks_diff(time.ticks_us(), start_time)')
        print(f'Large object write time: {write_time_large} μs')
        
        # Simple read test for large object
        print('Testing large object read speed...')
        send_simple_cmd('start_time = time.ticks_us()')
        send_simple_cmd('val1 = large_obj[0]')
        send_simple_cmd('val2 = large_obj[100000]')
        send_simple_cmd('val3 = large_obj[500000]')
        send_simple_cmd('val4 = large_obj[1000000]')
        read_time_large = send_simple_cmd('time.ticks_diff(time.ticks_us(), start_time)')
        print(f'Large object read time: {read_time_large} μs')
        
        print('\n--- Results Summary ---')
        print(f'Small object (4KB):')
        print(f'  Address: {addr_small}')
        print(f'  Region: {region_small if "region_small" in locals() else "Unknown"}')
        print(f'  Write time: {write_time_small} μs')
        print(f'  Read time: {read_time_small} μs')
        
        print(f'Large object (1MB):')
        print(f'  Address: {addr_large}')
        print(f'  Region: {region_large if "region_large" in locals() else "Unknown"}')
        print(f'  Write time: {write_time_large} μs')
        print(f'  Read time: {read_time_large} μs')
        
        # Performance comparison
        try:
            if write_time_small and write_time_large and write_time_small.isdigit() and write_time_large.isdigit():
                wt_small = int(write_time_small)
                wt_large = int(write_time_large)
                if wt_small < wt_large:
                    print(f'\n✓ Small object writes are {wt_large/wt_small:.1f}x faster')
                else:
                    print(f'\n⚠ Large object writes are {wt_small/wt_large:.1f}x faster')
            
            if read_time_small and read_time_large and read_time_small.isdigit() and read_time_large.isdigit():
                rt_small = int(read_time_small)
                rt_large = int(read_time_large)
                if rt_small < rt_large:
                    print(f'✓ Small object reads are {rt_large/rt_small:.1f}x faster')
                else:
                    print(f'⚠ Large object reads are {rt_small/rt_large:.1f}x faster')
        except:
            print('Could not compare performance')
        
        # Cleanup
        send_simple_cmd('del small_obj, large_obj')
        send_simple_cmd('gc.collect()')
        
        ser.close()
        print('\n✓ Basic speed test completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Basic Memory Speed Test")
    print("Simple performance comparison")
    print("=" * 40)
    basic_speed_test()
