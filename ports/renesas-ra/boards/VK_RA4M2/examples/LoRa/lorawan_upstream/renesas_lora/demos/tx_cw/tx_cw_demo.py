"""SX1262 continuous-wave transmitter for indoor RF coverage measurement.

Drives the SX1262 via raw SPI opcodes — no LoRaWAN stack.
Button P014: short=cycle power, long=TX on/off. OLED 128x32 shows state.
Hard-stops 30 min after first TX; only hardware RESET recovers from STOP.
"""
import time, framebuf, gc
from machine import Pin, SPI, SoftI2C

# ---- Hardware objects (proven pattern from rssi_scanner.py) ----
spi   = SPI(3, baudrate=8000000, polarity=0, phase=0,
            sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))
nss   = Pin('P206', Pin.OUT, value=1)
busy  = Pin('P002', Pin.IN)
dio1  = Pin('P015', Pin.IN)
rst   = Pin('P001', Pin.OUT, value=1)
rf_en = Pin('P100', Pin.OUT, value=1)
btn   = Pin('P014', Pin.IN, Pin.PULL_UP)

OLED_W, OLED_H, OLED_ADDR = 128, 32, 0x3C
i2c  = SoftI2C(scl=Pin('P301', Pin.IN), sda=Pin('P302', Pin.IN), freq=400_000)

class SSD1306:
    def __init__(self, i2c, addr=OLED_ADDR):
        self.i2c, self.addr = i2c, addr
        self.pages = OLED_H // 8
        self._tx = bytearray(1 + OLED_W * self.pages)
        self._tx[0] = 0x40
        self.fb = framebuf.FrameBuffer(memoryview(self._tx)[1:],
                                        OLED_W, OLED_H, framebuf.MONO_VLSB)
        cmd = bytearray(2); cmd[0] = 0x00
        for c in (0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
                  0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
                  0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF):
            cmd[1] = c; self.i2c.writeto(self.addr, cmd)
        self._show = bytearray(b"\x00\x21\x00\x7F\x22\x00"
                                + bytes([self.pages - 1]))
        self.fb.fill(0); self.show()
    def show(self):
        self.i2c.writeto(self.addr, self._show)
        self.i2c.writeto(self.addr, self._tx)

oled = SSD1306(i2c)

# ---- Low-level SPI helpers ----
_BUSY_TIMEOUT_MS = 200

def wait_busy(tag=""):
    t0 = time.ticks_ms()
    while busy.value():
        if time.ticks_diff(time.ticks_ms(), t0) >= _BUSY_TIMEOUT_MS:
            print("BUSY timeout", tag)
            return False
    return True

def cmd(op, payload=b''):
    # Poll BUSY before asserting NSS — per SX1262 DS §8.1
    if not wait_busy("pre op=0x%02X" % op):
        return
    nss(0)
    spi.write(bytes([op]) + payload)
    nss(1)
    wait_busy("post op=0x%02X" % op)

def read_status():
    # GetStatus opcode 0xC0 + 1 NOP; status in byte[1]
    wait_busy("read_status pre")
    nss(0)
    buf = bytearray(2)
    spi.write_readinto(bytes([0xC0, 0x00]), buf)
    nss(1)
    wait_busy("read_status post")
    return buf[1]

# ---- SX1262 lifecycle ----
def chip_reset():
    rst(0)
    time.sleep_us(150)          # ≥100 µs per SX1262 DS §8.1
    rst(1)
    wait_busy("after reset")

def radio_init():
    # Steps 1–11: standby, DCDC, TCXO, calibrate, clear errors,
    # DIO2 RF-switch, packet type, frequency, PA config.
    cmd(0x80, b'\x00')                  # 1  SetStandby(STDBY_RC)
    cmd(0x96, b'\x01')                  # 2  SetRegulatorMode(DCDC)
    cmd(0x97, b'\x02\x00\x01\x40')     # 3  SetDio3AsTcxoCtrl(1.8V, 320 ticks)
    time.sleep_ms(10)                   # 4  TCXO stabilisation ≥5 ms
    cmd(0x89, b'\x7F')                  # 5  Calibrate(all blocks)
    wait_busy("post calibrate")         # 6  calibration up to 3.5 ms
    cmd(0x07, b'\x00\x00')             # 7  ClearDeviceErrors
    time.sleep_ms(10)                   # brief settle after error clear
    cmd(0x9D, b'\x01')                  # 8  SetDio2AsRfSwitchCtrl
    cmd(0x8A, b'\x01')                  # 9  SetPacketType(LoRa)
    cmd(0x86, b'\x36\x41\x99\x9A')     # 10 SetRfFrequency(868.1 MHz)
    cmd(0x95, b'\x04\x07\x00\x01')     # 11 SetPaConfig(HP PA)

def tx_start_cw(power_dbm):
    # Steps 12–13 only; called standalone for power change mid-TX as well
    cmd(0x8E, bytes([power_dbm & 0xFF, 0x02]))  # 12 SetTxParams(power, ramp=40µs)
    cmd(0xD1, b'')                               # 13 SetTxContinuousWave

# ---- State ----
POWERS = (-1, 5, 10, 14, 17, 20, 22)
pwr_idx = 3             # default +14 dBm

state            = 'IDLE'   # 'IDLE' | 'TX' | 'PAUSE' | 'STOP'
T_first_tx_start = None     # ticks_ms() of first IDLE→TX transition
tx_start         = None     # ticks_ms() of current TX entry (uptime display)

btn_prev = 1
press_t  = 0
LONG_MS  = 1000

# ---- Render (only on state change or every 500 ms in TX) ----
_render_last_state = None
_render_last_tick  = 0

def render(force=False):
    global _render_last_state, _render_last_tick
    now = time.ticks_ms()
    if not force:
        if state == _render_last_state:
            if state != 'TX':
                return
            if time.ticks_diff(now, _render_last_tick) < 500:
                return
    _render_last_state = state
    _render_last_tick  = now

    pwr     = POWERS[pwr_idx]
    pwr_str = "%+3d dBm" % pwr

    oled.fb.fill(0)
    if state == 'IDLE':
        oled.fb.text("IDLE  " + pwr_str,  0,  0, 1)
        oled.fb.text("868.1 MHz  CW",     0, 11, 1)
        oled.fb.text("hold P014 -> TX",   0, 22, 1)
    elif state == 'TX':
        el_ms = time.ticks_diff(now, tx_start) if tx_start is not None else 0
        el_s  = el_ms // 1000
        oled.fb.text(">TX   " + pwr_str,                         0,  0, 1)
        oled.fb.text("868.1 MHz  CW",                            0, 11, 1)
        oled.fb.text("%02d:%02d  uptime" % (el_s // 60, el_s % 60), 0, 22, 1)
    elif state == 'PAUSE':
        el_ms = time.ticks_diff(now, tx_start) if tx_start is not None else 0
        el_s  = el_ms // 1000
        oled.fb.text("PAUSE " + pwr_str,              0,  0, 1)
        oled.fb.text("hold P014 resume",               0, 11, 1)
        oled.fb.text("%02d:%02d" % (el_s // 60, el_s % 60), 0, 22, 1)
    elif state == 'STOP':
        oled.fb.text("STOP  30 min HW",  0,  0, 1)
        oled.fb.text("safety auto-off",  0, 11, 1)
        oled.fb.text("RESET to restart", 0, 22, 1)
    oled.show()

# ---- Boot ----
print("tx_cw_demo: init...")
chip_reset()
radio_init()
render(force=True)
print("tx_cw_demo: IDLE +%d dBm — long-press P014 to TX" % POWERS[pwr_idx])

# ---- Main loop ----
while True:
    now = time.ticks_ms()

    # Hard-stop: 30 min after first TX start, fires from TX or PAUSE
    if T_first_tx_start is not None and state != 'STOP':
        if time.ticks_diff(now, T_first_tx_start) >= 30 * 60 * 1000:
            cmd(0x80, b'\x00')  # SetStandby STDBY_RC
            rf_en(0)
            state = 'STOP'
            render(force=True)

    if state != 'STOP':
        bc = btn.value()
        if btn_prev == 1 and bc == 0:
            press_t = now
        elif btn_prev == 0 and bc == 1:
            held = time.ticks_diff(now, press_t)
            time.sleep_ms(80)       # debounce
            if held < LONG_MS:      # short press
                if state in ('IDLE', 'TX'):
                    pwr_idx = (pwr_idx + 1) % len(POWERS)
                    if state == 'TX':
                        tx_start_cw(POWERS[pwr_idx])    # steps 12+13 only
                    render(force=True)
            else:                   # long press
                if state == 'IDLE':
                    if T_first_tx_start is None:
                        T_first_tx_start = time.ticks_ms()
                    tx_start = time.ticks_ms()
                    radio_init()                        # steps 1–11
                    tx_start_cw(POWERS[pwr_idx])        # steps 12–13
                    state = 'TX'
                    render(force=True)
                elif state == 'TX':
                    cmd(0x80, b'\x00')                  # SetStandby STDBY_RC
                    state = 'PAUSE'
                    render(force=True)
                elif state == 'PAUSE':
                    tx_start = time.ticks_ms()
                    radio_init()                        # full re-init from PAUSE
                    tx_start_cw(POWERS[pwr_idx])        # steps 12–13
                    state = 'TX'
                    render(force=True)
        btn_prev = bc

    render()        # noop unless TX + 500 ms elapsed
    gc.collect()
    time.sleep_ms(50)
