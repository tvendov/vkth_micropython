# Write-then-read roundtrip on SX1262 LoRa Sync Word registers (0x0740/0x0741).
# Writeable in standby, no RF side-effects until RX/TX is started.
# A passing test means single-burst writes AND reads are aligned at the tested rate.

from machine import Pin, SPI
from time import sleep_ms

Pin('P100', Pin.OUT, value=1)
CS   = Pin('P206', Pin.OUT, value=1)
RST  = Pin('P001', Pin.OUT, value=1)
BUSY = Pin('P002', Pin.IN)

RST.value(0); sleep_ms(20); RST.value(1); sleep_ms(50)
while BUSY.value():
    pass

ADDR_HI = 0x07
ADDR_LO = 0x40

def wait_busy():
    t = 0
    while BUSY.value():
        t += 1
        if t > 100000:
            return False
    return True

def write_reg(spi, addr_hi, addr_lo, val_hi, val_lo):
    # WriteRegister opcode 0x0D, addr_hi, addr_lo, data...
    tx = bytearray([0x0D, addr_hi, addr_lo, val_hi, val_lo])
    rx = bytearray(len(tx))
    CS.value(0)
    spi.write_readinto(tx, rx)
    CS.value(1)
    wait_busy()

def read_reg(spi, addr_hi, addr_lo, n):
    # ReadRegister opcode 0x1D, addr_hi, addr_lo, NOP, then n NOPs to clock data out
    tx = bytearray([0x1D, addr_hi, addr_lo, 0x00] + [0x00] * n)
    rx = bytearray(len(tx))
    CS.value(0)
    spi.write_readinto(tx, rx)
    CS.value(1)
    return bytes(rx[4:4 + n])

ok_all = True
for baud in (1_000_000, 2_000_000, 4_000_000, 8_000_000, 16_000_000):
    spi = SPI(3, baudrate=baud, polarity=0, phase=0, bits=8, firstbit=SPI.MSB)
    sleep_ms(2)
    # Use a value that survives a bit-shift attack — 0x5A 0xA5 has every-other-bit toggle.
    write_reg(spi, ADDR_HI, ADDR_LO, 0x5A, 0xA5)
    rb = read_reg(spi, ADDR_HI, ADDR_LO, 2)
    ok = (rb == b'\x5A\xA5')
    if not ok:
        ok_all = False
    print('{:>3d} MHz  read-back = {:02x} {:02x}  expected 5a a5  {}'.format(
        baud // 1_000_000, rb[0], rb[1], 'OK' if ok else 'FAIL'))
    spi.deinit()

# Multi-byte burst at 16 MHz, varying n
print()
spi = SPI(3, baudrate=16_000_000, polarity=0, phase=0, bits=8, firstbit=SPI.MSB)
sleep_ms(2)
# Write 8 bytes to a writeable region (RX_GAIN at 0x08AC, multiple bytes around it).
# Use SyncWord 0x0740..0x0747 — first 2 bytes are LoRa sync, rest are RFU but writeable.
pattern = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88])
tx = bytearray([0x0D, ADDR_HI, ADDR_LO]) + pattern
rx = bytearray(len(tx))
CS.value(0); spi.write_readinto(tx, rx); CS.value(1); wait_busy()

for n in (1, 2, 3, 4, 5, 6, 7, 8):
    rb = read_reg(spi, ADDR_HI, ADDR_LO, n)
    expected = pattern[:n]
    ok = (rb == expected)
    if not ok:
        ok_all = False
    print('n={}  read = {}  expected {}  {}'.format(
        n,
        ' '.join('{:02x}'.format(b) for b in rb),
        ' '.join('{:02x}'.format(b) for b in expected),
        'OK' if ok else 'FAIL'))
spi.deinit()

print()
print('OVERALL:', 'PASS' if ok_all else 'FAIL')
