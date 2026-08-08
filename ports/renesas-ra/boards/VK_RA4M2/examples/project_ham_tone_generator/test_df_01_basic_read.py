# Тест 01: Базови dataflash параметри + 2 последователни read.
# Базиран на книгата: 27_storage/02_dataflash_basic.py.
# Цел: проверка дали reads са stable (детерминирани).

import dataflash

print("=== TEST 01: Basic Read Stability ===")
print("size():", dataflash.size())
print("block_size():", dataflash.block_size())
print("write_size():", dataflash.write_size())

print()
print("--- 5 consecutive reads of offset 0..15 (no operation between) ---")
for i in range(5):
    print("Read {}:".format(i + 1), list(dataflash.read(0, 16)))

print()
print("ОЧАКВАНО: всичките 5 reads трябва да са ИДЕНТИЧНИ.")
print("Ако се различават → reads са нестабилни (silicon или address bug).")
