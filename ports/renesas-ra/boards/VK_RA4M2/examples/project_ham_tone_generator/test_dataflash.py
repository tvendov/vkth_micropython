# Изолиран тест за dataflash операциите.
# Save → Stop/Restart → F5 → копирай output

import dataflash

print("=== DATAFLASH ISOLATION TEST ===")
print("size:", dataflash.size())
print("block:", dataflash.block_size())
print("write_size:", dataflash.write_size())

print("\n--- Test 1: read raw flash ---")
print("before:", list(dataflash.read(0, 8)))

print("\n--- Test 2: erase block 0 ---")
try:
    dataflash.erase_block(0)
    print("erase: OK")
except Exception as e:
    print("erase FAILED:", e)

print("after erase:", list(dataflash.read(0, 8)))

print("\n--- Test 3: write 4 bytes (book pattern) ---")
try:
    n = dataflash.write(0, bytes([0x42, 0x43, 0x44, 0x45]))
    print("write 4 bytes: OK, n =", n)
except Exception as e:
    print("write 4 FAILED:", e)

print("after write 4:", list(dataflash.read(0, 8)))

print("\n--- Test 4: erase + write 2 bytes ---")
try:
    dataflash.erase_block(0)
    n = dataflash.write(0, bytes([0x14, 0x00]))
    print("write 2 bytes: OK, n =", n)
except Exception as e:
    print("write 2 FAILED:", e)

print("after write 2:", list(dataflash.read(0, 8)))

print("\n--- Test 5: erase + write 1 byte (book example pattern) ---")
try:
    dataflash.erase_block(0)
    n = dataflash.write(0, bytes([0x55]))
    print("write 1 byte: OK, n =", n)
except Exception as e:
    print("write 1 FAILED:", e)

print("after write 1:", list(dataflash.read(0, 8)))

print("\n=== DONE ===")
