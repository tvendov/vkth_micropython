# SPI3 / SCI9 FIFO+DTC test suite for VK_RA4M2.
# Wiring: P109 (MOSI) <-> P110 (MISO) loopback wire.

from machine import SPI, Pin
import time
import gc

PASS = 0
FAIL = 0

def check(name, cond):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("PASS  %s" % name)
    else:
        FAIL += 1
        print("FAIL  %s" % name)

def hdr(s):
    print()
    print("=== %s ===" % s)


# 1. ALL FOUR SPI MODES (CPOL x CPHA)
hdr("SPI modes 0-3 loopback")
patterns = [
    bytes([0x00, 0xFF, 0xA5, 0x5A, 0x01, 0x80, 0x7F, 0xC3]),
    bytes(range(32)),
]
for mode in range(4):
    cpol = (mode >> 1) & 1
    cpha = mode & 1
    spi = SPI(3, baudrate=2_000_000, polarity=cpol, phase=cpha, bits=8)
    for pat in patterns:
        rx = bytearray(len(pat))
        spi.write_readinto(pat, rx)
        check("mode=%d len=%d" % (mode, len(pat)), rx == pat)
    spi.deinit()


# 2. write-only (src valid, dst=NULL path)
hdr("write-only (no rx capture)")
spi = SPI(3, baudrate=4_000_000)
try:
    spi.write(b"\xDE\xAD\xBE\xEF\x12\x34\x56\x78")
    check("write() returned cleanly", True)
except Exception as e:
    check("write() returned cleanly", False)
    print("  err:", e)
spi.deinit()


# 3. read-only via readinto (src=NULL fallback, MicroPython core fills 0xFF)
hdr("read into buffer")
spi = SPI(3, baudrate=4_000_000)
buf = bytearray(16)
spi.readinto(buf, 0xFF)
# With loopback, readinto sends 0xFF and reads back -> rx = all 0xFF
expected = b"\xFF" * 16
check("readinto loopback all 0xFF", bytes(buf) == expected)
spi.deinit()


# 4. SEQUENTIAL TRANSFERS — back-to-back, no state contamination
hdr("Sequential transfers")
spi = SPI(3, baudrate=4_000_000)
ok_seq = True
for i in range(20):
    tx = bytes([(i + j) & 0xff for j in range(31)])  # odd length
    rx = bytearray(len(tx))
    spi.write_readinto(tx, rx)
    if rx != tx:
        ok_seq = False
        print("  iter %d differ" % i)
        break
check("20 back-to-back transfers", ok_seq)
spi.deinit()


# 5. ODD AND EDGE-CASE LENGTHS
hdr("Length boundary cases")
spi = SPI(3, baudrate=4_000_000)
for n in (1, 2, 3, 7, 15, 16, 17, 31, 32, 33, 100, 255, 256, 257, 1023, 1024):
    tx = bytes(((i * 13 + 7) & 0xFF) for i in range(n))
    rx = bytearray(n)
    spi.write_readinto(tx, rx)
    check("len=%d" % n, rx == tx)
spi.deinit()


# 6. LARGE BUFFER — within UINT16_MAX bound
hdr("Large buffer (16 KB) @ 12 MHz")
spi = SPI(3, baudrate=12_000_000)
N = 16 * 1024
gc.collect()
tx = bytes(((i * 31 + 5) & 0xFF) for i in range(N))
rx = bytearray(N)
t0 = time.ticks_us()
spi.write_readinto(tx, rx)
dt = time.ticks_diff(time.ticks_us(), t0)
mbps = (N * 8) / dt
check("16 KB matches", rx == tx)
print("  16 KB in %d us = %.2f Mbps" % (dt, mbps))
spi.deinit()


# 7. Skipped — heap-bound on RA4M2 prevents allocating 2x 24KB buffers.
#    The 16KB run above already exercised the same DTC pipeline path.
del tx, rx
gc.collect()


# 8. MUTEX WITH UART9 — both share SCI9; only one allowed at a time
hdr("SCI9 mutex: UART(9) blocks SPI(3)")
from machine import UART
u = UART(9, baudrate=115200)
spi_blocked = False
try:
    s = SPI(3, baudrate=1_000_000)
    s.deinit()
except Exception as e:
    spi_blocked = True
    print("  blocked as expected:", repr(e))
check("SPI(3) raised while UART(9) live", spi_blocked)
u.deinit()
# After UART9 deinit, SPI3 should construct cleanly
try:
    spi = SPI(3, baudrate=1_000_000)
    rx = bytearray(4)
    spi.write_readinto(b"\x11\x22\x33\x44", rx)
    spi.deinit()
    check("SPI(3) works after UART(9).deinit()", rx == b"\x11\x22\x33\x44")
except Exception as e:
    check("SPI(3) works after UART(9).deinit()", False)
    print("  err:", e)


# 9. (skipped — SPI1 RSPI loopback is a separate hardware block;
#     this test exercises that, not SCI9 SPI3, so we omit it here.)


# 10. STRESS — 200 transfers, varying length, verify no leaks/corruption
hdr("Stress: 200 transfers, varying lengths")
spi = SPI(3, baudrate=8_000_000)
ok_stress = True
sizes = [1, 7, 16, 64, 256, 1024]
for i in range(200):
    n = sizes[i % len(sizes)]
    tx = bytes(((i + j) & 0xff) for j in range(n))
    rx = bytearray(n)
    spi.write_readinto(tx, rx)
    if rx != tx:
        ok_stress = False
        print("  iter %d (len=%d) diverged" % (i, n))
        break
check("200 transfers OK", ok_stress)
spi.deinit()


# 11. DEINIT + REINIT — confirm DTCs and FIFOs cleanly tear down
hdr("Deinit/reinit cycles")
ok_reinit = True
for cyc in range(5):
    s = SPI(3, baudrate=4_000_000)
    rx = bytearray(8)
    tx = bytes(((cyc * 8 + i) & 0xFF) for i in range(8))
    s.write_readinto(tx, rx)
    if rx != tx:
        ok_reinit = False
        break
    s.deinit()
check("5 deinit/reinit cycles", ok_reinit)


print()
print("=" * 40)
print("SUMMARY: %d passed, %d failed" % (PASS, FAIL))
print("=" * 40)
