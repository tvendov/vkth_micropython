# SPI3 (simple-SPI on SCI9) - VK_RA4M2
# Pins (RA4M2 datasheet Table 19.18, PSEL=00101b):
#   SCK  = P111  (SCK9)
#   MOSI = P109  (TXD9/MOSI9)
#   MISO = P110  (RXD9/MISO9)
#
# Notes:
# - Master-only, 8-bit, FIFO + dual-DTC.
#   16-stage TX/RX FIFO, two DTC channels (TX feeds FTDR, RX drains FRDR).
#   Exactly ONE IRQ per transfer (RX DTC IRQ_END signals completion). CPU
#   sleeps in WFI for the entire body of the transfer regardless of length.
# - SCI9 hardware is shared with UART9 (mutex via SCI owner).
# - Pins overlap with SPI1 (RSPI1). Only one of SPI1/SPI3 can be active at a time:
#       spi1.deinit(); spi3 = SPI(3, ...)
#
# Loopback test: short MOSI (P109) <-> MISO (P110) with a wire,
# then run this script. With SCK on P111 you should see the bit-clock too.

from machine import SPI, Pin
import time

print("--- SPI3 / SCI9 simple-SPI on VK_RA4M2 ---")

# Default pinset: SCK=P111, MOSI=P109, MISO=P110.
# Mode 0 (CPOL=0, CPHA=0), 1 MHz baud, MSB first, 8-bit.
spi = SPI(3, baudrate=1_000_000, polarity=0, phase=0, bits=8, firstbit=SPI.MSB)
print(spi)

# 1) Loopback test - requires P109 <-> P110 short.
tx = bytes([0xA5, 0x5A, 0x01, 0x02, 0x03, 0x80, 0xFF, 0x00])
rx = bytearray(len(tx))
spi.write_readinto(tx, rx)
print("TX:", " ".join("%02X" % b for b in tx))
print("RX:", " ".join("%02X" % b for b in rx))
if rx == tx:
    print("LOOPBACK: OK")
else:
    print("LOOPBACK: differ (no jumper P109<->P110, or wrong wiring)")

# 2) Transmit-only stream so SCK/MOSI can be observed on a scope.
print("Streaming pattern on MOSI (P109) / SCK (P111) for ~2 s...")
buf = bytes(range(256))
t_end = time.ticks_add(time.ticks_ms(), 2000)
while time.ticks_diff(t_end, time.ticks_ms()) > 0:
    spi.write(buf)

# 3) Different baud / polarity / phase quick sweep.
for baud in (100_000, 500_000, 4_000_000):
    spi.init(baudrate=baud)
    print("baud=%d:" % baud, spi)
    spi.write(b"\xAA\x55")

# 4) Mode 3 (CPOL=1, CPHA=1) example.
spi.init(baudrate=1_000_000, polarity=1, phase=1)
print("Mode 3:", spi)
spi.write(b"\xDE\xAD\xBE\xEF")

spi.deinit()
print("done")
