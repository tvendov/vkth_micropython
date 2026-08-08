# dac_adc_sandy.py
# DAC-ADC voltmeter с OLED дисплей (SSD1306 128x16).
# Pins: P015=DAC, P001=ADC, P301=SCL, P302=SDA

from machine import DAC, ADC, Pin, SoftI2C
import framebuf
import time
import sys
import select

VREF = 3.286
BITS = 12
FULL = 1 << BITS

dac = DAC(Pin("P015"))
adc = ADC(Pin("P001"), bits=12)

dac_value = 2048
dac.write(dac_value)


# --- OLED (SSD1306 128x32) ---
OLED_W    = 128
OLED_H    = 32
OLED_ADDR = 0x3C
OLED_FREQ = 400000

class SSD1306:
    _INIT_CMDS = (
        0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
        0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    )
    def __init__(self, i2c, addr=OLED_ADDR):
        self.i2c = i2c
        self.addr = addr
        self._tx_buf = bytearray(1 + OLED_W * OLED_H // 8)
        self._tx_buf[0] = 0x40
        self.fb = framebuf.FrameBuffer(memoryview(self._tx_buf)[1:],
                                       OLED_W, OLED_H, framebuf.MONO_VLSB)
        self._cmd1 = bytearray(2)
        self._show_cmd = bytearray(b"\x00\x21\x00\x7F\x22\x00\x03")
        for c in self._INIT_CMDS:
            self._cmd1[1] = c
            self.i2c.writeto(self.addr, self._cmd1)
        self.fb.fill(0)
        self.show()
    def show(self):
        self.i2c.writeto(self.addr, self._show_cmd)
        self.i2c.writeto(self.addr, self._tx_buf)


i2c = SoftI2C(scl=Pin("P301", Pin.OPEN_DRAIN),
              sda=Pin("P302", Pin.OPEN_DRAIN), freq=OLED_FREQ)
oled = None
devs = i2c.scan()
print("I2C scan:", devs)
if 0x3C in devs:
    oled = SSD1306(i2c, 0x3C)
elif 0x3D in devs:
    oled = SSD1306(i2c, 0x3D)


# --- 2x scaled font (16x16 на знак) — както в ham_ctcss.py ---
_SMALL_BUF = bytearray(64)
_SMALL_FB = framebuf.FrameBuffer(_SMALL_BUF, 64, 8, framebuf.MONO_VLSB)

def text2x(big_fb, text, x, y, color):
    n = len(text)
    if n > 8:
        n = 8
    _SMALL_FB.fill(0)
    _SMALL_FB.text(text, 0, 0, 1)
    src_w = n * 8
    py = 0
    while py < 8:
        px = 0
        while px < src_w:
            if _SMALL_FB.pixel(px, py):
                xx = x + (px << 1)
                yy = y + (py << 1)
                big_fb.pixel(xx,     yy,     color)
                big_fb.pixel(xx + 1, yy,     color)
                big_fb.pixel(xx,     yy + 1, color)
                big_fb.pixel(xx + 1, yy + 1, color)
            px += 1
        py += 1


# --- Non-blocking REPL input ---
_poll = select.poll()
_poll.register(sys.stdin, select.POLLIN)
_in_buf = ""

def poll_repl():
    """Чете volt input от REPL. Връща LSB стойност (0..4095) или None."""
    global _in_buf
    new_val = None
    while _poll.poll(0):
        ch = sys.stdin.read(1)
        if ch == "\n" or ch == "\r":
            line = _in_buf.strip().rstrip("Vv")
            _in_buf = ""
            if line:
                try:
                    v = float(line)
                    if 0.0 <= v <= VREF:
                        lsb = int(v * FULL / VREF + 0.5)
                        if lsb > 4095:
                            lsb = 4095
                        new_val = lsb
                    else:
                        print("\n[ERR] 0..{:.3f}V".format(VREF))
                except ValueError:
                    print("\n[ERR] not number")
        elif ch == "\x03":
            raise KeyboardInterrupt
        elif ch == "\x08" or ch == "\x7f":
            _in_buf = _in_buf[:-1]
        else:
            _in_buf += ch
    return new_val


print("\nDAC-ADC Voltmeter (live + OLED)")
print("Type volts 0..{:.3f} (e.g. 1.5 or 1.5V) + Enter\n".format(VREF))

next_tick = time.ticks_add(time.ticks_ms(), 100)

while True:
    nv = poll_repl()
    if nv is not None:
        dac_value = nv
        dac.write(dac_value)
        print("\n[DAC <- {}]".format(dac_value))

    # ---- ADC averaging (100 samples) ----
    total = 0
    for _ in range(100):
        total += adc.read()
    raw = total // 100

    volts_adc = raw * VREF / FULL
    volts_dac = dac_value * VREF / FULL
    err = raw - dac_value

    # ---- REPL single-line overwrite ----
    print(
        "\rDAC:{:4d} ({:.3f}V) | ADC:{:4d} ({:.3f}V) | ERR:{:+4d} LSB   "
        .format(dac_value, volts_dac, raw, volts_adc, err),
        end=""
    )

    # ---- OLED (2x font като в ham_ctcss.py); двата реда едновременно ----
    if oled is not None:
        oled.fb.fill(0)
        text2x(oled.fb, "D{:5.3f}V".format(volts_dac), 8,  0, 1)
        text2x(oled.fb, "A{:5.3f}V".format(volts_adc), 8, 16, 1)
        oled.show()

    # ---- 100 ms cadence ----
    remaining = time.ticks_diff(next_tick, time.ticks_ms())
    if remaining > 0:
        time.sleep_ms(remaining)
    next_tick = time.ticks_add(next_tick, 100)
