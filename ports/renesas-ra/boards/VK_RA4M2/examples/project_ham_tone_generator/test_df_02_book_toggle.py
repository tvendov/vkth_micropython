# Тест 02: Точна репликация на 27_storage/03_dataflash_state_led.py.
# Цел: проверка дали книжният pattern (1 байт write на 0x00, erase на 0xFF) работи.
# Пуска се ДВА пъти подред: първи път пише 0x00, втори път erase-ва.

import dataflash

STATE_OFFSET = 0

print("=== TEST 02: Book toggle pattern (single byte) ===")

state = dataflash.read(STATE_OFFSET, 1)[0]
print("BOOT: dataflash[0] =", hex(state))

if state == 0x00:
    print("Action: ERASE block 0 -> expect 0xFF")
    dataflash.erase_block(0)
    expected = 0xFF
else:
    print("Action: WRITE 0x00 -> expect 0x00")
    dataflash.write(STATE_OFFSET, bytes([0x00]))
    expected = 0x00

new_state = dataflash.read(STATE_OFFSET, 1)[0]
print("AFTER:  dataflash[0] =", hex(new_state))
print("EXPECT: dataflash[0] =", hex(expected))

if new_state == expected:
    print(">>> PASS: write + readback OK (in session)")
else:
    print(">>> FAIL: read returns", hex(new_state), "but expected", hex(expected))

print()
print("Сега направи soft reset и пусни същия скрипт пак.")
print("BOOT стойността от 2-я run трябва = AFTER от 1-я run (персист).")
