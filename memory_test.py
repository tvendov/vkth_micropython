"""
Memory and File System Test for RA6M5
=====================================

This script tests memory capabilities and file system limits
to determine the maximum file sizes that can be created and stored.
"""

import gc
import os
import time
import machine

def check_memory_info():
    """Display detailed memory information"""
    print("=" * 50)
    print("RA6M5 Memory Information")
    print("=" * 50)
    
    # Basic memory info
    gc.collect()
    free_mem = gc.mem_free()
    alloc_mem = gc.mem_alloc()
    total_mem = free_mem + alloc_mem
    
    print(f"RAM Memory:")
    print(f"  Total:     {total_mem:,} bytes ({total_mem/(1024*1024):.1f}MB)")
    print(f"  Allocated: {alloc_mem:,} bytes ({alloc_mem/(1024*1024):.1f}MB)")
    print(f"  Free:      {free_mem:,} bytes ({free_mem/(1024*1024):.1f}MB)")
    print(f"  Usage:     {(alloc_mem/total_mem)*100:.1f}%")
    
    # File system info
    try:
        statvfs = os.statvfs('/')
        fs_size = statvfs[0] * statvfs[2]  # block_size * total_blocks
        fs_free = statvfs[0] * statvfs[3]  # block_size * free_blocks
        fs_used = fs_size - fs_free
        
        print(f"\nFile System (/):")
        print(f"  Total:     {fs_size:,} bytes ({fs_size/(1024*1024):.1f}MB)")
        print(f"  Used:      {fs_used:,} bytes ({fs_used/(1024*1024):.1f}MB)")
        print(f"  Free:      {fs_free:,} bytes ({fs_free/(1024*1024):.1f}MB)")
        print(f"  Usage:     {(fs_used/fs_size)*100:.1f}%")
        
    except Exception as e:
        print(f"\nFile System info unavailable: {e}")
    
    return free_mem, total_mem

def test_large_memory_allocation():
    """Test allocating large chunks of memory"""
    print("\n" + "=" * 50)
    print("Large Memory Allocation Test")
    print("=" * 50)
    
    gc.collect()
    initial_free = gc.mem_free()
    print(f"Initial free memory: {initial_free:,} bytes")
    
    # Test progressively larger allocations
    test_sizes = [
        (100, "KB"),
        (500, "KB"), 
        (1, "MB"),
        (2, "MB"),
        (4, "MB"),
        (6, "MB"),
        (8, "MB")
    ]
    
    max_allocation = 0
    allocated_objects = []
    
    for size_val, unit in test_sizes:
        if unit == "KB":
            size_bytes = size_val * 1024
        else:  # MB
            size_bytes = size_val * 1024 * 1024
            
        print(f"\nTesting {size_val}{unit} allocation ({size_bytes:,} bytes)...")
        
        try:
            # Try to allocate memory
            data = bytearray(size_bytes)
            
            # Fill with pattern to ensure it's real allocation
            for i in range(0, len(data), 1024):
                end_idx = min(i + 1024, len(data))
                for j in range(i, end_idx):
                    data[j] = (j % 256)
            
            allocated_objects.append(data)
            max_allocation = size_bytes
            
            gc.collect()
            current_free = gc.mem_free()
            print(f"  ✓ SUCCESS - Free memory now: {current_free:,} bytes")
            
        except MemoryError:
            print(f"  ✗ FAILED - MemoryError")
            break
        except Exception as e:
            print(f"  ✗ FAILED - {e}")
            break
    
    # Clean up
    allocated_objects.clear()
    gc.collect()
    final_free = gc.mem_free()
    
    print(f"\nMaximum allocation: {max_allocation:,} bytes ({max_allocation/(1024*1024):.1f}MB)")
    print(f"Memory recovered: {final_free:,} bytes")
    
    return max_allocation

def test_file_creation_limits():
    """Test creating files of various sizes"""
    print("\n" + "=" * 50)
    print("File Creation Limits Test")
    print("=" * 50)
    
    # Test file sizes
    test_sizes_mb = [0.1, 0.5, 1, 2, 4, 6, 8, 10]
    max_file_size = 0
    created_files = []
    
    for size_mb in test_sizes_mb:
        filename = f"test_{size_mb}mb.bin"
        size_bytes = int(size_mb * 1024 * 1024)
        
        print(f"\nCreating {size_mb}MB file: {filename}")
        print(f"  Target size: {size_bytes:,} bytes")
        
        gc.collect()
        free_before = gc.mem_free()
        print(f"  Free memory before: {free_before:,} bytes")
        
        start_time = time.time()
        
        try:
            with open(filename, 'wb') as f:
                chunk_size = 4096  # 4KB chunks
                written = 0
                
                while written < size_bytes:
                    remaining = min(chunk_size, size_bytes - written)
                    
                    # Create unique data pattern
                    chunk = bytearray()
                    for i in range(remaining):
                        # Non-repeating pattern
                        value = (written + i) & 0xFF
                        chunk.append(value)
                    
                    f.write(chunk)
                    written += remaining
                    
                    # Progress for larger files
                    if size_mb >= 1 and written % (512 * 1024) == 0:
                        progress_mb = written / (1024 * 1024)
                        print(f"    {progress_mb:.1f}MB written...")
            
            # Verify file was created
            actual_size = os.stat(filename)[6]
            elapsed = time.time() - start_time
            write_speed = (actual_size / elapsed) / (1024 * 1024) if elapsed > 0 else 0
            
            if actual_size == size_bytes:
                print(f"  ✓ SUCCESS - {actual_size:,} bytes in {elapsed:.1f}s ({write_speed:.1f}MB/s)")
                max_file_size = size_mb
                created_files.append(filename)
            else:
                print(f"  ✗ SIZE MISMATCH - Expected {size_bytes}, got {actual_size}")
                break
                
        except Exception as e:
            print(f"  ✗ FAILED - {e}")
            break
        
        gc.collect()
        free_after = gc.mem_free()
        print(f"  Free memory after: {free_after:,} bytes")
    
    print(f"\nMaximum file size created: {max_file_size}MB")
    print(f"Files created: {len(created_files)}")
    
    # Cleanup option
    cleanup = input("\nDelete test files? (y/n): ").lower().strip()
    if cleanup == 'y':
        for filename in created_files:
            try:
                os.remove(filename)
                print(f"Deleted {filename}")
            except:
                print(f"Failed to delete {filename}")
    
    return max_file_size

def test_file_read_performance():
    """Test file reading performance"""
    print("\n" + "=" * 50)
    print("File Read Performance Test")
    print("=" * 50)
    
    # Create a test file first
    test_file = "read_test.bin"
    test_size_mb = 2
    size_bytes = test_size_mb * 1024 * 1024
    
    print(f"Creating {test_size_mb}MB test file for reading...")
    
    try:
        # Create file
        with open(test_file, 'wb') as f:
            for i in range(size_bytes):
                f.write(bytes([i & 0xFF]))
        
        print(f"✓ Test file created: {size_bytes:,} bytes")
        
        # Test different read chunk sizes
        chunk_sizes = [1024, 4096, 8192, 16384, 32768]  # 1KB to 32KB
        
        for chunk_size in chunk_sizes:
            print(f"\nTesting read with {chunk_size} byte chunks...")
            
            start_time = time.time()
            bytes_read = 0
            
            with open(test_file, 'rb') as f:
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    bytes_read += len(chunk)
            
            elapsed = time.time() - start_time
            read_speed = (bytes_read / elapsed) / (1024 * 1024) if elapsed > 0 else 0
            
            print(f"  Read {bytes_read:,} bytes in {elapsed:.2f}s ({read_speed:.1f}MB/s)")
        
        # Cleanup
        os.remove(test_file)
        print(f"\nCleaned up {test_file}")
        
    except Exception as e:
        print(f"Read performance test failed: {e}")

def main():
    """Run all memory and file system tests"""
    print("RA6M5 Memory and File System Testing")
    
    # Memory info
    free_mem, total_mem = check_memory_info()
    
    # Memory allocation test
    max_alloc = test_large_memory_allocation()
    
    # File creation test
    max_file = test_file_creation_limits()
    
    # Read performance test
    test_file_read_performance()
    
    # Summary
    print("\n" + "=" * 50)
    print("TEST SUMMARY")
    print("=" * 50)
    print(f"Total RAM:           {total_mem/(1024*1024):.1f}MB")
    print(f"Free RAM:            {free_mem/(1024*1024):.1f}MB")
    print(f"Max allocation:      {max_alloc/(1024*1024):.1f}MB")
    print(f"Max file size:       {max_file}MB")
    
    if max_file >= 4:
        print("✓ Board capable of handling large file transfers (4MB+)")
    elif max_file >= 1:
        print("⚠ Board can handle medium file transfers (1MB+)")
    else:
        print("⚠ Limited file transfer capability (<1MB)")

if __name__ == "__main__":
    main()
