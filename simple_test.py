#!/usr/bin/env python3
import serial
import time

def simple_test():
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
        
        print('\n=== MEMORY TEST ===')
        
        send_cmd('import gc')
        send_cmd('gc.collect()')
        send_cmd('print("Initial free:", gc.mem_free())')
        
        print('\n--- Creating 1MB objects ---')
        send_cmd('obj1 = bytearray(1024*1024)')
        send_cmd('print("obj1:", hex(id(obj1)))')
        
        send_cmd('obj2 = bytearray(1024*1024)')
        send_cmd('print("obj2:", hex(id(obj2)))')
        
        send_cmd('obj3 = bytearray(1024*1024)')
        send_cmd('print("obj3:", hex(id(obj3)))')
        
        print('\n--- Memory analysis ---')
        send_cmd('gc.collect()')
        send_cmd('print("Free now:", gc.mem_free())')
        send_cmd('print("Allocated:", gc.mem_alloc())')
        
        # Check memory regions
        send_cmd('addr1 = id(obj1)')
        send_cmd('print("obj1 region:", "OSPI" if 0x30000000 <= addr1 < 0x70000000 else "SRAM")')
        
        send_cmd('addr2 = id(obj2)')
        send_cmd('print("obj2 region:", "OSPI" if 0x30000000 <= addr2 < 0x70000000 else "SRAM")')
        
        send_cmd('addr3 = id(obj3)')
        send_cmd('print("obj3 region:", "OSPI" if 0x30000000 <= addr3 < 0x70000000 else "SRAM")')
        
        ser.close()
        print('\n✓ Test completed!')
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    simple_test()
