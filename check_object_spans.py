#!/usr/bin/env python3
import serial
import time

def check_object_spans():
    try:
        ser = serial.Serial('COM20', 115200, timeout=2)
        print('✓ Connected to COM20')
        
        # Reset to clean state
        ser.write(b'\x03\x04')
        time.sleep(2)
        ser.read_all()
        
        def send_and_get_result(cmd):
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
        
        print('\n=== CHECKING OBJECT MEMORY SPANS ===')
        
        # Setup
        send_and_get_result('import gc')
        send_and_get_result('gc.collect()')
        
        # Memory boundaries
        print('\n--- Memory Region Boundaries ---')
        print('Internal SRAM: 0x20000000 - 0x20080000 (512KB)')
        print('Mapped OSPI:   0x20080000 - 0x20800000 (8MB)')
        print('Direct OSPI:   0x68000000 - 0x68800000 (8MB)')
        
        # Create objects of different sizes
        print('\n--- Creating Objects and Checking Spans ---')
        
        # Small object that should fit in internal SRAM
        print('\n1. Small Object (100KB):')
        send_and_get_result('small_obj = bytearray(102400)')  # 100KB
        start_addr = send_and_get_result('id(small_obj)')
        size = send_and_get_result('len(small_obj)')
        
        if start_addr and size:
            start = int(start_addr)
            end = start + int(size)
            print(f'   Start: 0x{start:08x}')
            print(f'   End:   0x{end:08x}')
            print(f'   Size:  {size} bytes')
            
            if start >= 0x20000000 and end <= 0x20080000:
                print('   ✓ ENTIRELY in Internal SRAM')
            elif start >= 0x20080000:
                print('   ✓ ENTIRELY in Mapped OSPI')
            elif start < 0x20080000 and end > 0x20080000:
                print('   ⚠ SPANS Internal SRAM and Mapped OSPI')
                internal_part = 0x20080000 - start
                ospi_part = end - 0x20080000
                print(f'     Internal SRAM: {internal_part} bytes')
                print(f'     Mapped OSPI:   {ospi_part} bytes')
            else:
                print('   ? Unknown region')
        
        # Medium object
        print('\n2. Medium Object (1MB):')
        send_and_get_result('medium_obj = bytearray(1048576)')  # 1MB
        start_addr = send_and_get_result('id(medium_obj)')
        size = send_and_get_result('len(medium_obj)')
        
        if start_addr and size:
            start = int(start_addr)
            end = start + int(size)
            print(f'   Start: 0x{start:08x}')
            print(f'   End:   0x{end:08x}')
            print(f'   Size:  {size} bytes')
            
            if start >= 0x20000000 and end <= 0x20080000:
                print('   ✓ ENTIRELY in Internal SRAM')
            elif start >= 0x20080000:
                print('   ✓ ENTIRELY in Mapped OSPI')
            elif start < 0x20080000 and end > 0x20080000:
                print('   ⚠ SPANS Internal SRAM and Mapped OSPI')
                internal_part = 0x20080000 - start
                ospi_part = end - 0x20080000
                print(f'     Internal SRAM: {internal_part} bytes')
                print(f'     Mapped OSPI:   {ospi_part} bytes')
            else:
                print('   ? Unknown region')
        
        # Large object
        print('\n3. Large Object (4MB):')
        send_and_get_result('large_obj = bytearray(4194304)')  # 4MB
        start_addr = send_and_get_result('id(large_obj)')
        size = send_and_get_result('len(large_obj)')
        
        if start_addr and size:
            start = int(start_addr)
            end = start + int(size)
            print(f'   Start: 0x{start:08x}')
            print(f'   End:   0x{end:08x}')
            print(f'   Size:  {size} bytes')
            
            if start >= 0x20000000 and end <= 0x20080000:
                print('   ✓ ENTIRELY in Internal SRAM')
            elif start >= 0x20080000:
                print('   ✓ ENTIRELY in Mapped OSPI')
            elif start < 0x20080000 and end > 0x20080000:
                print('   ⚠ SPANS Internal SRAM and Mapped OSPI')
                internal_part = 0x20080000 - start
                ospi_part = end - 0x20080000
                print(f'     Internal SRAM: {internal_part} bytes')
                print(f'     Mapped OSPI:   {ospi_part} bytes')
            else:
                print('   ? Unknown region')
        
        # Check current memory usage
        print('\n--- Memory Usage Analysis ---')
        free_mem = send_and_get_result('gc.mem_free()')
        alloc_mem = send_and_get_result('gc.mem_alloc()')
        
        if free_mem and alloc_mem:
            total = int(free_mem) + int(alloc_mem)
            print(f'Free memory:      {free_mem} bytes')
            print(f'Allocated memory: {alloc_mem} bytes')
            print(f'Total heap:       {total:,} bytes ({total/1024/1024:.1f} MB)')
        
        # Speed test if we have objects in different regions
        print('\n--- Speed Test Preparation ---')
        print('Now we can test speed differences between objects')
        print('that are actually in different memory regions!')
        
        # Cleanup
        send_and_get_result('del small_obj, medium_obj, large_obj')
        send_and_get_result('gc.collect()')
        
        ser.close()
        print('\n✓ Object span analysis completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Object Memory Span Analysis")
    print("Checking where objects actually start and end")
    print("=" * 50)
    check_object_spans()
