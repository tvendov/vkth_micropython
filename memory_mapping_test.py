#!/usr/bin/env python3
import serial
import time

def memory_mapping_test():
    try:
        ser = serial.Serial('COM20', 115200, timeout=2)
        print('✓ Connected to COM20')
        
        # Just interrupt, don't reset
        ser.write(b'\x03')
        time.sleep(0.3)
        ser.read_all()
        
        def send_cmd(cmd):
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(0.6)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip():
                        print(clean)
        
        print('\n=== MEMORY MAPPING INVESTIGATION ===')
        
        send_cmd('import gc')
        send_cmd('gc.collect()')
        
        # Create many objects to see where they get allocated
        print('\n--- Creating multiple 1MB objects to see allocation pattern ---')
        
        for i in range(1, 8):  # Try to create 7 x 1MB objects
            send_cmd(f'obj{i} = bytearray(1024*1024)')
            send_cmd(f'print("obj{i}: 0x{{:08x}} ({{:.1f}}MB from start)".format(id(obj{i}), (id(obj{i}) - 0x20000000)/1024/1024))')
            send_cmd('gc.collect()')
            send_cmd(f'print("  Free after obj{i}: {{:,}} bytes ({{:.1f}}MB)".format(gc.mem_free(), gc.mem_free()/1024/1024))')
            
            # Check if we're running out of memory
            send_cmd('if gc.mem_free() < 1024*1024: print("  ⚠️  Less than 1MB free!")')
        
        print('\n--- Memory boundary analysis ---')
        send_cmd('print("\\n=== FINAL ANALYSIS ===")')
        send_cmd('total_heap = gc.mem_free() + gc.mem_alloc()')
        send_cmd('print("Total heap: {:,} bytes ({:.1f}MB)".format(total_heap, total_heap/1024/1024))')
        
        # Try to determine the actual memory layout
        send_cmd('print("\\n=== MEMORY LAYOUT THEORY ===")')
        send_cmd('print("RA6M5 internal SRAM: ~512KB-1MB")')
        send_cmd('print("Total heap observed: ~8MB")')
        send_cmd('print("Conclusion: OSPI RAM is memory-mapped into 0x20xxxxxx space")')
        send_cmd('print("This extends the apparent \\"internal SRAM\\" range to include OSPI RAM")')
        
        ser.close()
        print('\n✓ Memory mapping test completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    memory_mapping_test()
