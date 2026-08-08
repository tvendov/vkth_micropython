# Тест 04: Write + verify + persist check.
# Първи run: пише 0xAA на offset 0.
# Втори run (след soft reset): чете offset 0, очаква 0xAA.

import dataflash

MARKER = 0xAA      # битово 10101010 - alternating bits
OFFSET = 0

print("=== TEST 04: Write + persist after reset ===")

current = dataflash.read(OFFSET, 1)[0]
print("BOOT: dataflash[{}] = 0x{:02X}".format(OFFSET, current))

if current == MARKER:
    print(">>> PERSIST WORKS: stored value preserved across reset!")
    print("    (run again to test 2nd write/read cycle)")
    print("erase_block(0) for next test cycle...")
    dataflash.erase_block(0)
    after_erase = dataflash.read(OFFSET, 1)[0]
    print("After erase:", "0x{:02X}".format(after_erase))
else:
    print("Writing 0x{:02X} at offset {}...".format(MARKER, OFFSET))
    print("erase_block(0) first...")
    dataflash.erase_block(0)
    after_erase = dataflash.read(OFFSET, 1)[0]
    print("After erase: 0x{:02X} (expect 0xFF)".format(after_erase))

    try:
        dataflash.write(OFFSET, bytes([MARKER]))
        print("  write OK")
    except Exception as e:
        print("  write FAILED:", e)

    after_write = dataflash.read(OFFSET, 1)[0]
    print("After write: 0x{:02X} (expect 0x{:02X})".format(after_write, MARKER))

    if after_write == MARKER:
        print(">>> WRITE+VERIFY OK in session.")
        print(">>> SOFT RESET и пусни пак — BOOT стойността трябва да е 0xAA.")
    else:
        print(">>> WRITE FAILED — readback различен.")
