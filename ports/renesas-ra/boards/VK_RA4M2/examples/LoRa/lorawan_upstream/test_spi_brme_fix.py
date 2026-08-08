# Verification test for the SCI-SPI BRME fix.
# After the patch, BRME is forever-off; SCK should be uniform.
# We do single-burst GetStatus (opcode 0xC0 + dummy 0x00) at multiple baud
# rates. Per SX1262 Table 13-77, the status byte must land at rx[1].
# Pre-fix: rx[1]=idle (0xAA-ish), rx[2]=status at >=4 MHz.
# Post-fix: rx[1]=status at all rates up to 12.5 MHz (== 16 MHz request).

from machine import Pin, SPI
from time import sleep_ms

Pin('P100', Pin.OUT, value=1)              # RF_SW1 enable (Wio-SX1262)
CS   = Pin('P206', Pin.OUT, value=1)
RST  = Pin('P001', Pin.OUT, value=1)
BUSY = Pin('P002', Pin.IN)

# Hard reset the chip, then wake out of cold start.
RST.value(0); sleep_ms(20); RST.value(1); sleep_ms(50)
while BUSY.value():
    pass

def get_status(spi, label):
    tx = bytearray([0xC0, 0x00])
    rx = bytearray(2)
    CS.value(0)
    spi.write_readinto(tx, rx)
    CS.value(1)
    s = rx[1]
    mode = (s >> 4) & 0x07
    cmd  = (s >> 1) & 0x07
    print('{:>10s}  rx={:02x} {:02x}  mode={}  cmd={}'.format(label, rx[0], rx[1], mode, cmd))

for baud in (1_000_000, 2_000_000, 4_000_000, 8_000_000, 16_000_000):
    spi = SPI(3, baudrate=baud, polarity=0, phase=0, bits=8, firstbit=SPI.MSB)
    sleep_ms(2)
    get_status(spi, '{:>3d} MHz'.format(baud // 1_000_000))
    spi.deinit()

# Multi-byte burst sanity: read 5-byte register block via ReadRegister
# (opcode 0x1D + 16-bit addr + 1 NOP-status + N data). Confirms no
# burst-length-dependent corruption.
print()
spi = SPI(3, baudrate=16_000_000, polarity=0, phase=0, bits=8, firstbit=SPI.MSB)
sleep_ms(2)

# Read LR_Header_Mode register (LoRa modem register 0x0740 — value depends on
# packet config but must be stable across burst sizes).
def read_reg_burst(addr, n):
    # opcode 0x1D, addr_hi, addr_lo, NOP, then n data bytes
    tx = bytearray([0x1D, (addr >> 8) & 0xFF, addr & 0xFF, 0x00] + [0x00] * n)
    rx = bytearray(len(tx))
    CS.value(0)
    spi.write_readinto(tx, rx)
    CS.value(1)
    return rx

for n in (1, 2, 3, 4, 5, 6, 7, 8, 12):
    rx = read_reg_burst(0x0740, n)
    print('n={:2d}  rx={}'.format(n, ' '.join('{:02x}'.format(b) for b in rx)))

spi.deinit()
print('done.')
