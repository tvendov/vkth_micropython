"""
VK_RA4M2 — SOSC drive demo + dropout detector.

Циклира SOMCR.SODRV между Standard (0) и Low (1) на всеки 10 s.
Засича dropouts чрез R64CNT (RA4M2 §28.2): всеки бит е независим
divider flag — bit0=64Hz, bit1=32Hz, …, bit6=1Hz toggle. Ако в
50 ms прозорец R64CNT не се променя НИТО ВЕДНЪЖ → SOSC е спрял.

Старт-up последователност (RA4M2 §8.2.21): SOSTP=1 → SODRV → SOSTP=0 → tSUBOSCWT.

Стартирай:
    mpremote connect COM21 run examples/sosc_drive_cycle.py
"""

import time
import machine
from machine import Pin, SoftI2C
import framebuf

# --- Регистри ---
PRCR    = 0x4001E3FE
SOSCCR  = 0x4001E480
SOMCR   = 0x4001E481
R64CNT  = 0x40083000          # bit0..6 = independent freq toggle bits

DWELL_MS    = 10_000          # престой в режим
WIN_MS      = 50              # window за live detection
SAMPLE_MS   = 200             # период между window-ите
STAB_MS     = 2_000           # tSUBOSCWT

OLED_SCL, OLED_SDA, OLED_ADDR = "P301", "P302", 0x3C
OLED_W, OLED_H = 128, 32

class SSD1306:
    def __init__(self, i2c, addr=OLED_ADDR):
        self.i2c, self.addr = i2c, addr
        self._tx = bytearray(1 + OLED_W * (OLED_H // 8)); self._tx[0] = 0x40
        self.fb = framebuf.FrameBuffer(memoryview(self._tx)[1:],
                                       OLED_W, OLED_H, framebuf.MONO_VLSB)
        c1 = bytearray(2); c1[0] = 0
        for c in (0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
                  0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
                  0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF):
            c1[1] = c; self.i2c.writeto(self.addr, c1)
        self._show = bytearray(b"\x00\x21\x00\x7F\x22\x00\x03")
        self.fb.fill(0); self.show()
    def show(self):
        self.i2c.writeto(self.addr, self._show)
        self.i2c.writeto(self.addr, self._tx)

def make_oled():
    try:
        i2c = SoftI2C(scl=Pin(OLED_SCL, Pin.OPEN_DRAIN),
                      sda=Pin(OLED_SDA, Pin.OPEN_DRAIN), freq=400_000)
        if OLED_ADDR not in i2c.scan():
            return None
        return SSD1306(i2c)
    except Exception:
        return None

def set_sodrv(drv):
    machine.mem16[PRCR] = 0xA503
    machine.mem8[SOSCCR] = 0x01
    while (machine.mem8[SOSCCR] & 0x01) != 1: pass
    time.sleep_ms(1)
    machine.mem8[SOMCR] = (drv & 0x01) << 1
    machine.mem8[SOSCCR] = 0x00
    while (machine.mem8[SOSCCR] & 0x01) != 0: pass
    machine.mem16[PRCR] = 0xA500
    time.sleep_ms(STAB_MS)

def is_alive(window_us=50_000):
    """True ако R64CNT се променя в window_us. F64HZ toggle period ≈ 15.6 ms,
    така че в 50 ms прозорец очакваме ≥ 6 промени при здрав SOSC."""
    start = machine.mem8[R64CNT]
    t0 = time.ticks_us()
    transitions = 0
    last = start
    while time.ticks_diff(time.ticks_us(), t0) < window_us:
        v = machine.mem8[R64CNT]
        if v != last:
            transitions += 1
            last = v
    return transitions, last

def render(oled, name, drv, alive, transitions, dropouts, t_left):
    if oled is None: return
    oled.fb.fill(0)
    oled.fb.text("DRV:%s %s" % (name, "OK" if alive else "DEAD"), 0, 0, 1)
    oled.fb.text("trans=%d / %dms" % (transitions, WIN_MS), 0, 10, 1)
    oled.fb.text("drop=%d  T-%2ds" % (dropouts, t_left), 0, 20, 1)
    oled.show()

def run_mode(drv, name, oled):
    print("\n[%s] applying SODRV=%d" % (name, drv))
    set_sodrv(drv)

    samples = DWELL_MS // SAMPLE_MS
    dropouts = 0
    total_trans = 0
    t0 = time.ticks_ms()

    for i in range(samples):
        target = t0 + (i + 1) * SAMPLE_MS
        # изчакай до следващия sample tick
        while time.ticks_diff(target, time.ticks_ms()) > 0:
            pass

        trans, last = is_alive(WIN_MS * 1000)
        alive = trans > 0
        if not alive:
            dropouts += 1
        total_trans += trans

        elapsed_ms = (i + 1) * SAMPLE_MS
        t_left = (DWELL_MS - elapsed_ms) // 1000
        render(oled, name, drv, alive, trans, dropouts, t_left)

        # печатай на всяка цяла секунда + при dropout
        if (not alive) or (elapsed_ms % 1000 == 0):
            tag = "DEAD!" if not alive else "live"
            print("  t=%2ds.%01d  R64=0x%02X  trans=%2d in %dms  %s" %
                  (elapsed_ms // 1000, (elapsed_ms % 1000) // 100,
                   last, trans, WIN_MS, tag))

    avg_trans = total_trans / samples
    print("[%s] summary: %d/%d windows DEAD  (avg %.1f trans / %dms)" %
          (name, dropouts, samples, avg_trans, WIN_MS))
    return dropouts

def main():
    oled = make_oled()
    print("[OLED] %s" % ("ready" if oled else "headless (not detected)"))
    print("Cycling SODRV: 10 s STANDARD ↔ 10 s LOW  (Ctrl-C to stop)")
    print("Probe XCIN(P215) / XCOUT(P214) on the scope.")
    print("Healthy SOSC: ≥6 R64CNT transitions per 50 ms window.")

    try:
        cycle = 0
        while True:
            cycle += 1
            print("\n=== CYCLE %d ===" % cycle)
            d_std = run_mode(0, "STANDARD", oled)
            d_low = run_mode(1, "LOW",      oled)
            print("cycle %d → STANDARD drops=%d  LOW drops=%d" %
                  (cycle, d_std, d_low))
    except KeyboardInterrupt:
        print("\nstopped — last SODRV =", (machine.mem8[SOMCR] >> 1) & 0x01)

main()
