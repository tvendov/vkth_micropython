# Memory-Only Crash Test - без файлове
import gc
import time

def print_memory_info(label=""):
    gc.collect()
    free = gc.mem_free()
    alloc = gc.mem_alloc()
    total = free + alloc
    usage = (alloc / total) * 100
    
    print(f"\n[{label}]")
    print(f"  Free:  {free:,} bytes ({free/1024:.1f} KB) ({free/1024/1024:.2f} MB)")
    print(f"  Alloc: {alloc:,} bytes ({alloc/1024:.1f} KB) ({alloc/1024/1024:.2f} MB)")
    print(f"  Total: {total:,} bytes ({total/1024:.1f} KB) ({total/1024/1024:.2f} MB)")
    print(f"  Usage: {usage:.1f}%")
    print("-" * 50)

def create_memory_object(size_kb):
    """Създава обект в паметта без файлове"""
    size_bytes = size_kb * 1024
    try:
        # Създаваме bytearray директно в паметта
        data = bytearray(size_bytes)
        # Запълваме с тестови данни
        for i in range(0, size_bytes, 4):
            data[i:i+4] = (i // 4).to_bytes(4, 'little')
        return data
    except Exception as e:
        print(f"❌ CRASH при {size_kb}KB: {e}")
        return None

def memory_crash_test():
    print("=== MEMORY-ONLY CRASH TEST ===")
    print("Тестваме само memory allocation без файлове!")
    
    print_memory_info("START")
    
    # Тестови размери в KB
    test_sizes = [50, 100, 150, 180, 200, 250, 300, 350, 400, 450, 500, 600, 700, 800, 1000]
    
    objects = []
    
    for size_kb in test_sizes:
        print(f"\n🎯 ТЕСТ: {size_kb}KB ({size_kb/1024:.2f}MB)")
        print_memory_info(f"BEFORE {size_kb}KB")
        
        start_time = time.ticks_ms()
        obj = create_memory_object(size_kb)
        end_time = time.ticks_ms()
        
        if obj is None:
            print(f"💥 CRASH LIMIT: {size_kb}KB")
            break
        
        objects.append(obj)
        duration = time.ticks_diff(end_time, start_time)
        
        print(f"✅ SUCCESS: {size_kb}KB allocated in {duration}ms")
        print(f"   Object size: {len(obj):,} bytes")
        print(f"   Object ID: {id(obj):#x}")
        
        print_memory_info(f"AFTER {size_kb}KB")
        
        # Проверка дали имаме достатъчно памет за следващия тест
        if len(test_sizes) > test_sizes.index(size_kb) + 1:
            next_size = test_sizes[test_sizes.index(size_kb) + 1]
            if gc.mem_free() < (next_size * 1024 * 2):
                print("⚠️ Недостатъчно памет за следващия тест")
                break
    
    print(f"\n=== РЕЗУЛТАТИ ===")
    print(f"Успешни allocations: {len(objects)}")
    total_allocated = sum(len(obj) for obj in objects)
    print(f"Общо allocated: {total_allocated:,} bytes ({total_allocated/1024:.1f}KB) ({total_allocated/1024/1024:.2f}MB)")
    
    print_memory_info("FINAL")
    
    # Cleanup
    print("\nCleanup...")
    objects.clear()
    gc.collect()
    print_memory_info("AFTER CLEANUP")

if __name__ == "__main__":
    memory_crash_test()
