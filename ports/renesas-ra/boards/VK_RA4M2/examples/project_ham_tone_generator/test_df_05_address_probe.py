# Тест 05: Address probe - дали data flash е memory-mapped и на кой адрес.
# RA4M2 datasheet: data flash 8KB на 0x08000000 (read alias).
# Тестваме дали dataflash.read и mem32 връщат идентични стойности.

import dataflash
from machine import mem32

print("=== TEST 05: Address probe (mem32 vs dataflash.read) ===")

DF_BASE_CANDIDATES = [
    ("0x08000000", 0x08000000),
    ("0x40100000", 0x40100000),
    ("0x40080000", 0x40080000),
    ("0x40070000", 0x40070000),
]

print()
print("--- mem32 на различни адреси (4 байта) ---")
for name, addr in DF_BASE_CANDIDATES:
    try:
        val = mem32[addr]
        print("mem32[{}]: 0x{:08X}".format(name, val))
    except Exception as e:
        print("mem32[{}]: ERROR".format(name), e)

print()
print("--- dataflash.read(0, 4) ---")
print("dataflash:", list(dataflash.read(0, 4)))

print()
print("ОЧАКВАНО: mem32 на правилния data flash base трябва да съвпада")
print("(little-endian) с dataflash.read(0, 4).")
print("dataflash.read = [a, b, c, d]  ↔  mem32 = 0xddccbbaa")
