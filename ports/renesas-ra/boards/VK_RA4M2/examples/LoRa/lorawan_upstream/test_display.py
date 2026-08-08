"""Test script: SoftI2C на P301/P302 + SSD1306 128x16 + credentials info.

Цел: потвърждава че:
  1. SoftI2C на P301=SCL / P302=SDA работи (както в project_ham_tone_generator)
  2. SSD1306 128x16 OLED на 0x3C се init-ва успешно
  3. Credentials от Data Flash се изобразяват с rotation

Изпълнение (без upload):
    mpremote connect COMxx run test_display.py

Натисни Ctrl-C за изход.

Бележка: P301/P302 не са IIC peripheral пинове на RA4M2 (те са SCI2 channel).
Само SoftI2C bit-banged на GPIO работи без firmware update. Hardware I2C(2)
(SCI2 Simple I2C режим) изисква wiring в machine_i2c.c + recompile.
"""

import time
import framebuf
from machine import Pin, SoftI2C
import dataflash


# === Config ===

PIN_SCL  = "P301"
PIN_SDA  = "P302"
I2C_FREQ = 400_000

OLED_W   = 128
OLED_H   = 32                # 4 реда × 8 px (горни 2 = radio, долни 2 = credentials)
OLED_ADDR_TRY = (0x3C, 0x3D)

ROTATE_PERIOD_MS = 3000


# === SSD1306 128x16 driver (от ham_ctcss.py, проверен) ===

class SSD1306:
    """SSD1306 OLED — generic init за 128x16 / 128x32 / 128x64.

    mux ratio (cmd 0xA8) и com pin config (cmd 0xDA) се избират според OLED_H:
      16: mux=0x0F, com_pin=0x02
      32: mux=0x1F, com_pin=0x02
      64: mux=0x3F, com_pin=0x12
    """

    def __init__(self, i2c, addr=0x3C):
        self.i2c = i2c
        self.addr = addr
        self.pages = OLED_H // 8
        self._tx_buf = bytearray(1 + OLED_W * self.pages)
        self._tx_buf[0] = 0x40
        self._fb_view = memoryview(self._tx_buf)[1:]
        self.fb = framebuf.FrameBuffer(self._fb_view, OLED_W, OLED_H,
                                       framebuf.MONO_VLSB)
        self._cmd1 = bytearray(2)
        self._cmd1[0] = 0x00

        if OLED_H == 64:
            mux, com_pin = 0x3F, 0x12
        elif OLED_H == 32:
            mux, com_pin = 0x1F, 0x02
        else:                                # default 16
            mux, com_pin = 0x0F, 0x02

        init_cmds = (
            0xAE, 0xD5, 0x80, 0xA8, mux, 0xD3, 0x00, 0x40,
            0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, com_pin,
            0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
        )
        # show_cmd: column 0..127 (0x21 0x00 0x7F), page 0..pages-1 (0x22 0x00 N)
        self._show_cmd = bytearray(
            b"\x00\x21\x00\x7F\x22\x00" + bytes([self.pages - 1]))
        for c in init_cmds:
            self._cmd1[1] = c
            self.i2c.writeto(self.addr, self._cmd1)
        self.fb.fill(0)
        self.show()

    def fill(self, c):
        self.fb.fill(c)

    def text(self, s, x, y, color=1):
        self.fb.text(s, x, y, color)

    def show(self):
        self.i2c.writeto(self.addr, self._show_cmd)
        self.i2c.writeto(self.addr, self._tx_buf)


# === Credentials loader (от Data Flash) ===

def crc16_ccitt(data, crc=0xFFFF):
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def load_credentials():
    blob = bytes(dataflash.read(0, 40))
    if blob[0:4] != b"LWCR" or blob[4] != 0x01:
        return None, "no LWCR magic"
    stored_crc = (blob[38] << 8) | blob[39]
    if crc16_ccitt(blob[:38]) != stored_crc:
        return None, "CRC fail"
    return (blob[6:14], blob[14:22], blob[22:38]), "OK"


# === Main ===

print("=" * 50)
print("Test: SoftI2C SCL=%s SDA=%s @ %d Hz" % (PIN_SCL, PIN_SDA, I2C_FREQ))
print("=" * 50)

i2c = SoftI2C(scl=Pin(PIN_SCL, Pin.OPEN_DRAIN),
              sda=Pin(PIN_SDA, Pin.OPEN_DRAIN),
              freq=I2C_FREQ)

devices = i2c.scan()
print("I2C scan: %s" % [hex(d) for d in devices])

oled_addr = None
for a in OLED_ADDR_TRY:
    if a in devices:
        oled_addr = a
        break

if oled_addr is None:
    print("[FAIL] SSD1306 не е намерен (очаквам 0x3C или 0x3D).")
    print("       Провери: захранване, pull-up на SDA/SCL, кабели.")
    raise SystemExit(1)

print("[OK] SSD1306 на 0x%02X" % oled_addr)
oled = SSD1306(i2c, oled_addr)

# Boot greeting
oled.fill(0)
oled.text("I2C P301/P302", 0, 0)
oled.text("addr 0x%02X OK" % oled_addr, 0, 8)
oled.text("OLED %dx%d" % (OLED_W, OLED_H), 0, 16)
oled.text("test_display.py", 0, 24)
oled.show()
time.sleep(2)

# Credentials
creds, status = load_credentials()
print("Credentials: %s" % status)
if creds is None:
    oled.fill(0)
    oled.text("DataFlash:", 0, 0)
    oled.text(status, 0, 8)
    oled.show()
    raise SystemExit(2)

dev_eui, join_eui, app_key = creds
print("  DevEUI : %s" % dev_eui.hex())
print("  JoinEUI: %s" % join_eui.hex())
print("  AppKey : %s" % app_key.hex())


# Layout (4 реда × 8 px):
#   y=0   ред 0: TX параметри (винаги)
#   y=8   ред 1: RX параметри RSSI/SNR/window (винаги)
#   y=16  ред 2: credentials label (rotating)
#   y=24  ред 3: credentials value — пълен 16-hex-char код (rotating)
#
# В test mode: TX/RX редовете са placeholder; реалните стойности идват от
# lorawan_app по време на работа.

RADIO_TOP    = "TX +14dBm SF7"            # ред 0
RADIO_BOTTOM = "R-47 S+12.3 RX1"          # ред 1

PAGES = (
    ("DevEUI",      dev_eui.hex().upper()),
    ("JoinEUI",     join_eui.hex().upper()),
    ("AppKey 1/2",  app_key.hex()[0:16].upper()),
    ("AppKey 2/2",  app_key.hex()[16:32].upper()),
)

print()
print("Rotating display (%d страници, %dms всяка). Ctrl-C за изход." %
      (len(PAGES), ROTATE_PERIOD_MS))
print("  Top 2 rows:    radio params (винаги)")
print("  Bottom 2 rows: credentials (rotation)")

try:
    while True:
        for label, value in PAGES:
            oled.fill(0)
            oled.text(RADIO_TOP[:16],    0,  0)
            oled.text(RADIO_BOTTOM[:16], 0,  8)
            oled.text(label[:16],        0, 16)
            oled.text(value[:16],        0, 24)
            oled.show()
            print("  [%-10s] %s" % (label, value))
            time.sleep_ms(ROTATE_PERIOD_MS)
except KeyboardInterrupt:
    print("\nTest приключи.")
    oled.fill(0)
    oled.text("Test ended.", 0, 0)
    oled.show()
