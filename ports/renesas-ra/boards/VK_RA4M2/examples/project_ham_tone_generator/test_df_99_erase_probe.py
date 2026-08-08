# Deep probe: какво точно се случва след erase
import dataflash
import time

print("=== ERASE BEHAVIOR PROBE ===")

# Стъпка 1: запиши известен pattern на offset 0 и 4
print("\n--- 1. Setup: write known patterns ---")
dataflash.erase_block(0)
print("Erased block 0; immediate read[0..8]:", list(dataflash.read(0, 8)))

dataflash.write(0, bytes([0xAA, 0xBB, 0xCC, 0xDD]))
dataflash.write(4, bytes([0x11, 0x22, 0x33, 0x44]))
print("After writes, read[0..8]:", list(dataflash.read(0, 8)))

# Стъпка 2: повтори читането многократно (без erase) — стабилни ли са?
print("\n--- 2. Read stability of programmed cells (10 reads) ---")
for i in range(10):
    print("  read{}: {}".format(i, list(dataflash.read(0, 8))))

# Стъпка 3: изтрий блок 0 и веднага прочети 10 пъти
print("\n--- 3. erase_block(0) + 10 immediate reads ---")
dataflash.erase_block(0)
for i in range(10):
    print("  read{}: {}".format(i, list(dataflash.read(0, 8))))

# Стъпка 4: между четенията — блокирай известно време
print("\n--- 4. erase_block(0) + reads with delays ---")
dataflash.erase_block(0)
print("  immediate:", list(dataflash.read(0, 8)))
time.sleep_ms(10)
print("  +10ms:    ", list(dataflash.read(0, 8)))
time.sleep_ms(100)
print("  +100ms:   ", list(dataflash.read(0, 8)))
time.sleep_ms(500)
print("  +500ms:   ", list(dataflash.read(0, 8)))

# Стъпка 5: пълен erase
print("\n--- 5. dataflash.erase() (full) + reads ---")
dataflash.erase()
for i in range(5):
    print("  read{} of [0..8]:    {}".format(i, list(dataflash.read(0, 8))))
    print("  read{} of [64..72]:  {}".format(i, list(dataflash.read(64, 8))))
    print("  read{} of [4096..04]:{}".format(i, list(dataflash.read(4096, 8))))

# Стъпка 6: тест с power-of-two pattern — пише 0x55 на офсет 0
print("\n--- 6. Write 0x55 explicitly via blank-branch ---")
print("  before write read[0..4]:", list(dataflash.read(0, 4)))
try:
    dataflash.write(0, bytes([0x55]))
    print("  write 0x55 OK; read[0..4]:", list(dataflash.read(0, 4)))
except Exception as e:
    print("  write 0x55 FAILED:", e)

# Стъпка 7: directly check FACI blank state
print("\n--- 7. After full erase, what does FACI think? ---")
dataflash.erase()
# erase_block ползва blankCheck вътрешно — ако мине без exception → FACI казва blank
try:
    dataflash.erase_block(0)
    print("  erase_block(0) success → FACI blankCheck PASSED → block IS blank per FACI")
except Exception as e:
    print("  erase_block(0) failed:", e)

print("  AHB read[0..16]:", list(dataflash.read(0, 16)))
print()
print("Ако FACI казва blank, но AHB чете non-FF →")
print("  AHB не вижда истинското cell състояние (cache/buffer проблем)")
print("Ако AHB върне различни стойности при reads → cells реално са undefined")
print("Ако AHB върне СЪЩАТА стойност като преди erase → erase не работи / FCACHE stale")
