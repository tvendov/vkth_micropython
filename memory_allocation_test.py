#!/usr/bin/env python3
import serial
import time
import sys

def memory_allocation_test():
    """Test memory allocation and show where objects are located"""
    port = 'COM20'
    baudrate = 115200
    
    try:
        print(f"Connecting to {port}...")
        ser = serial.Serial(port, baudrate, timeout=2)
        print(f"✓ Connected to {port}")
        
        # Reset and establish connection
        ser.write(b'\x03')  # Ctrl+C
        time.sleep(0.2)
        ser.write(b'\x04')  # Ctrl+D (soft reset)
        time.sleep(2)
        ser.read_all()  # Clear buffer
        
        print("\nSending memory allocation test commands...")
        print("=" * 60)
        
        # Commands to test memory allocation and location
        commands = [
            # Initial memory state
            "import gc",
            "gc.collect()",
            "print('=== INITIAL MEMORY STATE ===')",
            "initial_free = gc.mem_free()",
            "initial_alloc = gc.mem_alloc()",
            "print(f'Initial free: {initial_free:,} bytes ({initial_free/1024/1024:.2f} MB)')",
            "print(f'Initial allocated: {initial_alloc:,} bytes ({initial_alloc/1024:.1f} KB)')",
            "",
            
            # Import modules for memory inspection
            "import micropython",
            "import sys",
            "",
            
            # Function to get object address (if available)
            "def get_obj_info(obj, name):",
            "    try:",
            "        addr = id(obj)",
            "        size = len(obj) if hasattr(obj, '__len__') else 'unknown'",
            "        print(f'{name}:')",
            "        print(f'  Address: 0x{addr:08x}')",
            "        print(f'  Size: {size:,} bytes' if isinstance(size, int) else f'  Size: {size}')",
            "        # Determine memory region based on address",
            "        if addr < 0x20000000:",
            "            region = 'Flash/ROM region'",
            "        elif addr < 0x30000000:",
            "            region = 'Internal SRAM'",
            "        elif addr < 0x70000000:",
            "            region = 'External RAM (likely OSPI)'",
            "        else:",
            "            region = 'Other/Unknown region'",
            "        print(f'  Memory region: {region}')",
            "        return addr, size",
            "    except Exception as e:",
            "        print(f'Error getting info for {name}: {e}')",
            "        return None, None",
            "",
            
            # Create small objects first
            "print('\\n=== CREATING SMALL OBJECTS ===')",
            "small_obj1 = bytearray(1024)  # 1KB",
            "addr1, size1 = get_obj_info(small_obj1, 'small_obj1 (1KB)')",
            "",
            "small_obj2 = bytearray(10240)  # 10KB", 
            "addr2, size2 = get_obj_info(small_obj2, 'small_obj2 (10KB)')",
            "",
            
            # Check memory after small allocations
            "gc.collect()",
            "free_after_small = gc.mem_free()",
            "alloc_after_small = gc.mem_alloc()",
            "print(f'\\nAfter small objects:')",
            "print(f'  Free: {free_after_small:,} bytes ({free_after_small/1024/1024:.2f} MB)')",
            "print(f'  Allocated: {alloc_after_small:,} bytes ({alloc_after_small/1024:.1f} KB)')",
            "print(f'  Used by small objects: {alloc_after_small - initial_alloc:,} bytes')",
            "",
            
            # Create medium objects
            "print('\\n=== CREATING MEDIUM OBJECTS ===')",
            "medium_obj1 = bytearray(100 * 1024)  # 100KB",
            "addr3, size3 = get_obj_info(medium_obj1, 'medium_obj1 (100KB)')",
            "",
            "medium_obj2 = bytearray(500 * 1024)  # 500KB",
            "addr4, size4 = get_obj_info(medium_obj2, 'medium_obj2 (500KB)')",
            "",
            
            # Check memory after medium allocations
            "gc.collect()",
            "free_after_medium = gc.mem_free()",
            "alloc_after_medium = gc.mem_alloc()",
            "print(f'\\nAfter medium objects:')",
            "print(f'  Free: {free_after_medium:,} bytes ({free_after_medium/1024/1024:.2f} MB)')",
            "print(f'  Allocated: {alloc_after_medium:,} bytes ({alloc_after_medium/1024:.1f} KB)')",
            "",
            
            # Create large objects (1MB each)
            "print('\\n=== CREATING LARGE OBJECTS (1MB each) ===')",
            "large_obj1 = bytearray(1024 * 1024)  # 1MB",
            "addr5, size5 = get_obj_info(large_obj1, 'large_obj1 (1MB)')",
            "",
            "large_obj2 = bytearray(1024 * 1024)  # 1MB",
            "addr6, size6 = get_obj_info(large_obj2, 'large_obj2 (1MB)')",
            "",
            "large_obj3 = bytearray(1024 * 1024)  # 1MB",
            "addr7, size7 = get_obj_info(large_obj3, 'large_obj3 (1MB)')",
            "",
            
            # Final memory check
            "gc.collect()",
            "final_free = gc.mem_free()",
            "final_alloc = gc.mem_alloc()",
            "print(f'\\n=== FINAL MEMORY STATE ===')",
            "print(f'Free: {final_free:,} bytes ({final_free/1024/1024:.2f} MB)')",
            "print(f'Allocated: {final_alloc:,} bytes ({final_alloc/1024:.1f} KB)')",
            "print(f'Total used: {final_alloc - initial_alloc:,} bytes ({(final_alloc - initial_alloc)/1024/1024:.2f} MB)')",
            "",
            
            # Summary of address ranges
            "print('\\n=== ADDRESS ANALYSIS ===')",
            "addresses = [addr1, addr2, addr3, addr4, addr5, addr6, addr7]",
            "names = ['1KB obj', '10KB obj', '100KB obj', '500KB obj', '1MB obj1', '1MB obj2', '1MB obj3']",
            "for addr, name in zip(addresses, names):",
            "    if addr:",
            "        if addr < 0x20000000:",
            "            region = 'Flash/ROM'",
            "        elif addr < 0x30000000:",
            "            region = 'Internal SRAM'",
            "        elif addr < 0x70000000:",
            "            region = 'External RAM (OSPI)'",
            "        else:",
            "            region = 'Other'",
            "        print(f'{name:12} @ 0x{addr:08x} -> {region}')",
            "",
            
            # Test if we can still allocate more
            "print('\\n=== STRESS TEST ===')",
            "try:",
            "    stress_obj = bytearray(2 * 1024 * 1024)  # 2MB",
            "    addr_stress, size_stress = get_obj_info(stress_obj, 'stress_obj (2MB)')",
            "    print('✓ Successfully allocated additional 2MB object')",
            "    del stress_obj",
            "except MemoryError:",
            "    print('✗ Cannot allocate additional 2MB (memory full)')",
            "except Exception as e:",
            "    print(f'Error in stress test: {e}')",
            "",
            
            "print('\\n=== TEST COMPLETE ===')",
            "gc.collect()",
            "print(f'Final free memory: {gc.mem_free():,} bytes ({gc.mem_free()/1024/1024:.2f} MB)')"
        ]
        
        # Send commands and capture responses
        for i, cmd in enumerate(commands):
            if cmd.strip():  # Skip empty lines
                print(f"[{i+1:3d}] {cmd}")
                ser.write(cmd.encode() + b'\r\n')
                time.sleep(0.3)  # Give more time for large allocations
                
                # Read response
                response = ser.read_all()
                if response:
                    decoded = response.decode('utf-8', errors='ignore')
                    # Filter out the echo of the command
                    lines = decoded.split('\n')
                    for line in lines:
                        if line.strip() and not line.strip().startswith(cmd.strip()):
                            print(f"     {line}")
            else:
                time.sleep(0.1)
                
        ser.close()
        print("\n" + "=" * 60)
        print("Memory allocation test completed!")
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False
    
    return True

if __name__ == "__main__":
    print("MicroPython Memory Allocation Test")
    print("Testing object allocation and memory regions")
    print("=" * 60)
    memory_allocation_test()
