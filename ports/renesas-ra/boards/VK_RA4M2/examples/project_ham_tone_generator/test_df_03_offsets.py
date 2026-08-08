# Тест 03: Write на различни offsets (0, 16, 32, 60).
# Цел: проверка дали проблемът е специфичен за offset 0 или универсален.

import dataflash

print("=== TEST 03: Write at different offsets ===")

# Изтрий блок 0 (всички 64 байта).
print("erase_block(0)...")
dataflash.erase_block(0)

# Read веднага след erase.
print("After erase:", list(dataflash.read(0, 64)))

# Write 0x00 на offset 0.
print()
print("Write bytes([0x00]) at offset 0...")
try:
    dataflash.write(0, bytes([0x00]))
    print("  OK")
except Exception as e:
    print("  FAILED:", e)

# Write 0x00 на offset 16.
print("Write bytes([0x00]) at offset 16...")
try:
    dataflash.write(16, bytes([0x00]))
    print("  OK")
except Exception as e:
    print("  FAILED:", e)

# Write 0x00 на offset 32.
print("Write bytes([0x00]) at offset 32...")
try:
    dataflash.write(32, bytes([0x00]))
    print("  OK")
except Exception as e:
    print("  FAILED:", e)

# Write 0x00 на offset 60.
print("Write bytes([0x00]) at offset 60...")
try:
    dataflash.write(60, bytes([0x00]))
    print("  OK")
except Exception as e:
    print("  FAILED:", e)

print()
print("Final read:", list(dataflash.read(0, 64)))
print()
print("ОЧАКВАНО: байтове на offsets 0, 16, 32, 60 = 0x00 (=0).")
print("Останалите = 0xFF (=255). Ако не — write не работи.")
