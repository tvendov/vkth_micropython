# ham_ctcss.py - HAM CTCSS Тон Генератор за VK_RA4M2.
# Pins: P014=ЦАП, P109=PTT, P301/P302=I2C, P112=RGB DATA, P500=RGB PWR,
#       P206=MODE btn, P100=TONE+ btn.

import gc
import math
import time
from array import array
from machine import DAC, Pin, SoftI2C, WS2812
import framebuf
import os

# Data Flash на VK_RA4M2 firmware се оказа нефункционален —
# reads са случайни, erase не работи. Ползваме file storage в /flash.
PERSIST_FILE = "/flash/ham_state.bin"

PIN_DAC      = "P014"                          # ЦАП аудио изход.
PIN_PTT_IN   = "P301"                          # PTT вход.
PIN_SCL      = "P109"                          # I2C SCL.
PIN_SDA      = "P110"                          # I2C SDA.
PIN_RGB      = "P112"                          # WS2812 DATA.
PIN_RGB_PWR  = "P500"                          # WS2812 PWR.
PIN_BTN_MODE = "P206"                          # MODE бутон.
PIN_BTN_TONE = "P100"                          # TONE+ бутон.

DAC_MID      = 2048                            # 12-bit center.
DAC_AMP      = 870                             # ±870 LSB.
DAC_TBL      = 256                             # семпли/период.

DEBOUNCE_MS       = 40                         # бутон debounce.
SHORT_MAX_MS      = 500                        # short press.
LONG_MIN_MS       = 1000                       # long press.
COMBO_MIN_MS      = 3000                       # combo reset.
LOOP_TICK_MS      = 5                          # loop tick.
PTT_DEBOUNCE_MS   = 5                          # PTT debounce.
AUTOSAVE_DELAY_MS = 2000                        # auto-save.

OLED_W    = 128                                # OLED ширина.
OLED_H    = 16                                 # OLED височина.
OLED_ADDR = 0x3C                               # I2C адрес.
OLED_FREQ = 400000                             # I2C clock.

DF_OFFSET = 0                                  # persist offset.
DF_LEN    = 2                                  # persist дължина.

DEF_TONE_IDX = 19                              # default 100 Hz.
DEF_POL      = 0                               # default LOW.

# 57 CTCSS тона.
CTCSS_HZ = (
     50.0,  52.5,  55.2,  58.0,  60.0,  62.5,  65.0,
     67.0,  69.3,  71.9,  74.4,  77.0,  79.7,  82.5,  85.4,  88.5,  91.5,
     94.8,  97.4, 100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3,
    131.8, 136.5, 141.3, 146.2, 151.4, 156.7, 159.8, 162.2, 165.5, 167.9,
    171.3, 173.8, 177.3, 179.9, 183.5, 186.2, 189.9, 192.8, 196.6, 199.5,
    203.5, 206.5, 210.7, 218.1, 225.7, 229.1, 233.6, 241.8, 250.3, 254.1,
)
N_TONES = len(CTCSS_HZ)

# Pre-computed tables.
_SR_TABLE = array("I", [0] * N_TONES)
_i = 0
while _i < N_TONES:
    _SR_TABLE[_i] = int(CTCSS_HZ[_i] * DAC_TBL + 0.5)
    _i += 1
del _i

_FREQ_STR = tuple("{:5.1f}".format(_h) for _h in CTCSS_HZ)
_SMALL_BUF = bytearray(64)
_SMALL_FB = framebuf.FrameBuffer(_SMALL_BUF, 64, 8, framebuf.MONO_VLSB)

_RGB_OFF       = (0, 0, 0)
_RGB_BOOT      = (40, 20, 0)
_RGB_IDLE_HI   = (0, 0, 60)                   # POL=1 → син.
_RGB_IDLE_LO   = (0, 40, 0)                   # POL=0 → зелен.
_RGB_TX        = (60, 0, 0)                   # PTT активен → червен.
_RGB_SAVED     = (60, 60, 60)                 # бял ярък — записано.
_RGB_ERROR     = (60, 0, 40)
_RGB_RESET     = (40, 0, 60)                  # лилав — combo reset.

def persist_load():
    try:
        f = open(PERSIST_FILE, "rb")
        raw = f.read(2)
        f.close()
    except:
        return DEF_TONE_IDX, DEF_POL
    if len(raw) < 2:
        return DEF_TONE_IDX, DEF_POL
    idx = raw[0]
    pol = raw[1]
    if idx >= N_TONES or pol > 1:
        return DEF_TONE_IDX, DEF_POL
    return idx, pol

def persist_save(tone_idx, pol):
    try:
        f = open(PERSIST_FILE, "wb")
        f.write(bytes([tone_idx & 0xFF, 1 if pol else 0]))
        f.close()
        return True
    except:
        return False

def persist_erase():
    try:
        os.remove(PERSIST_FILE)
        return True
    except:
        return False


# DAC синусоида с DTC circular.
class CtcssDac:
    def __init__(self, pin_name):
        self.dac = DAC(Pin(pin_name))
        self.buf = array("H", [0] * DAC_TBL)
        i = 0
        while i < DAC_TBL:
            v = DAC_MID + int(DAC_AMP * math.sin(2.0 * math.pi * i / DAC_TBL))
            if v < 0: v = 0
            elif v > 4095: v = 4095
            self.buf[i] = v
            i += 1
        self.dac.write(DAC_MID)
        self.running = False
    def start_idx(self, tone_idx):
        if self.running:
            self.dac.stop()
        self.dac.write_timed(self.buf, _SR_TABLE[tone_idx],
                             mode=DAC.CIRCULAR, transfer=DAC.TRANSFER_DTC)
        self.running = True
    def stop(self):
        if self.running:
            self.dac.stop()
            self.running = False
        self.dac.write(DAC_MID)
    def retune_idx(self, tone_idx):
        if self.running:
            self.dac.stop()
            self.dac.write_timed(self.buf, _SR_TABLE[tone_idx],
                                 mode=DAC.CIRCULAR, transfer=DAC.TRANSFER_DTC)


# SSD1306 OLED 128x16.
class SSD1306:
    _INIT_CMDS = (
        0xAE, 0xD5, 0x80, 0xA8, 0x0F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
        0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    )
    def __init__(self, i2c, addr=OLED_ADDR):
        self.i2c = i2c
        self.addr = addr
        self._tx_buf = bytearray(1 + OLED_W * OLED_H // 8)
        self._tx_buf[0] = 0x40
        self._fb_view = memoryview(self._tx_buf)[1:]
        self.fb = framebuf.FrameBuffer(self._fb_view, OLED_W, OLED_H,
                                       framebuf.MONO_VLSB)
        self._cmd1 = bytearray(2)
        self._cmd1[0] = 0x00
        self._show_cmd = bytearray(b"\x00\x21\x00\x7F\x22\x00\x01")
        self._init_panel()
        self.fb.fill(0)
        self.show()
    def _cmd(self, c):
        self._cmd1[1] = c
        self.i2c.writeto(self.addr, self._cmd1)
    def _init_panel(self):
        i = 0
        n = len(self._INIT_CMDS)
        while i < n:
            self._cmd(self._INIT_CMDS[i])
            i += 1
    def fill(self, color):
        self.fb.fill(color)
    def text(self, s, x, y, color=1):
        self.fb.text(s, x, y, color)
    def show(self):
        self.i2c.writeto(self.addr, self._show_cmd)
        self.i2c.writeto(self.addr, self._tx_buf)


def oled_probe(i2c):
    devs = i2c.scan()
    print("I2C scan:", devs)
    if 0x3C in devs:
        return 0x3C
    if 0x3D in devs:
        return 0x3D
    return None


# WS2812 RGB пиксел статус.
class RgbStatus:
    def __init__(self, pin_name=PIN_RGB, power_pin_name=PIN_RGB_PWR):
        self.power = Pin(power_pin_name, Pin.OUT, value=1)
        time.sleep_ms(100)
        self.strip = WS2812(pixel_count=1, pin=Pin(pin_name), channels=3)
        self._cur = None
        self.set(_RGB_BOOT)
    def set(self, color):
        if color == self._cur:
            return
        self._cur = color
        self.strip[0] = color
        self.strip.write()
    def force(self, color):
        self._cur = color
        self.strip[0] = color
        self.strip.write()
    def flash(self, color, ms=100, times=2):
        prev = self._cur
        i = 0
        while i < times:
            self.force(color)
            time.sleep_ms(ms)
            self.force(_RGB_OFF)
            time.sleep_ms(ms)
            i += 1
        if prev is not None:
            self.force(prev)


# PTT вход с polarity и debounce.
class PttInput:
    def __init__(self, pin_name, pol):
        self.pin = Pin(pin_name, Pin.IN, Pin.PULL_UP)
        self.pol = 1 if pol else 0
        self._raw_last = self.pin.value()
        self._stable = self._raw_last
        self._t_change = time.ticks_ms()
    def update(self, now_ms):
        v = self.pin.value()
        if v != self._raw_last:
            self._raw_last = v
            self._t_change = now_ms
        elif v != self._stable and time.ticks_diff(now_ms, self._t_change) >= PTT_DEBOUNCE_MS:
            self._stable = v
        return self._stable == self.pol
    def set_pol(self, pol):
        self.pol = 1 if pol else 0


# Бутон с short/long press.
class Button:
    EV_NONE  = 0
    EV_SHORT = 1
    EV_LONG  = 2
    def __init__(self, pin_name):
        self.pin = Pin(pin_name, Pin.IN, Pin.PULL_UP)
        self.stable = self.pin.value()
        self.last_change = time.ticks_ms()
        self.press_start = 0
        self.is_down = False
        self._event = self.EV_NONE
        self._long_fired = False
    def update(self, now_ms):
        v = self.pin.value()
        if v != self.stable:
            if time.ticks_diff(now_ms, self.last_change) >= DEBOUNCE_MS:
                self.stable = v
                self.last_change = now_ms
                if v == 0:
                    self.is_down = True
                    self.press_start = now_ms
                    self._long_fired = False
                else:
                    if self.is_down:
                        held = time.ticks_diff(now_ms, self.press_start)
                        self.is_down = False
                        if not self._long_fired:
                            if held >= LONG_MIN_MS:
                                self._event = self.EV_LONG
                            elif held <= SHORT_MAX_MS:
                                self._event = self.EV_SHORT
        else:
            self.last_change = now_ms
        ev = self._event
        self._event = self.EV_NONE
        return ev
    def is_pressed(self):
        return self.stable == 0
    def suppress_pending(self):
        self._event = self.EV_NONE
        self._long_fired = True


_MSG_NORMAL = 0
_MSG_SAVED  = 1
_MSG_FAILED = 2
_MSG_RESET  = 3


# 2x scaling text render (16x16 на знак).
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


# Centered overlay messages (text, x_offset).
_MSG_TEXT = {
    _MSG_SAVED:  ("SAVED", 24),
    _MSG_FAILED: ("FAIL",  32),
    _MSG_RESET:  ("RESET", 24),
}

def render_ui(oled, idx, pol, tx_on, msg_id):
    if oled is None:
        return
    oled.fill(0)
    if msg_id in _MSG_TEXT:
        text, x = _MSG_TEXT[msg_id]
        text2x(oled.fb, text, x, 0, 1)
    else:
        text2x(oled.fb, _FREQ_STR[idx], 0, 0, 1)
        text2x(oled.fb, "Hz", 80, 0, 1)
        if tx_on:
            oled.fb.fill_rect(114, 2, 12, 12, 1)
    oled.show()


def rgb_for_state(tx_on, pol):
    if tx_on:
        return _RGB_TX
    if pol == 1:
        return _RGB_IDLE_HI                   # син
    return _RGB_IDLE_LO                       # зелен


# === Boot ===
print("=== HAM CTCSS Tone Generator (VK_RA4M2) ===")

#rgb = RgbStatus(PIN_RGB)
dac = CtcssDac(PIN_DAC)
btn_mode = Button(PIN_BTN_MODE)
btn_tone = Button(PIN_BTN_TONE)

i2c = SoftI2C(scl=Pin(PIN_SCL, Pin.OPEN_DRAIN),
              sda=Pin(PIN_SDA, Pin.OPEN_DRAIN), freq=OLED_FREQ)
addr = oled_probe(i2c)
oled = None
if addr is not None:
    try:
        oled = SSD1306(i2c, addr)
        print("OLED ready at addr", hex(addr))
    except Exception as e:
        print("OLED init failed:", e)
        #rgb.flash(_RGB_ERROR, 80, 3)
        oled = None
else:
    print("OLED NOT found - headless")
    #rgb.flash(_RGB_ERROR, 80, 3)

tone_idx, pol = persist_load()
ptt = PttInput(PIN_PTT_IN, pol)
tx_on = False
render_ui(oled, tone_idx, pol, tx_on, _MSG_NORMAL)
#rgb.set(_RGB_IDLE_HI if pol else _RGB_IDLE_LO)
print("Boot: idx={} pol={}".format(tone_idx, pol))

# === Loop state ===
msg_id       = _MSG_NORMAL
msg_until_ms = 0
combo_start  = 0
combo_active = False
last_idx     = tone_idx
last_pol     = pol
last_tx      = tx_on
cfg_dirty    = False
cfg_save_at  = 0
saved_idx    = tone_idx
saved_pol    = pol

gc.collect()
gc.disable()

while 1:
    time.sleep(1)
    print("loop")

# === Main loop ===
while True:
    now = time.ticks_ms()

    ev_mode = btn_mode.update(now)
    ev_tone = btn_tone.update(now)
    ptt_active = ptt.update(now)

    # PTT -> DAC.
    if ptt_active and not tx_on:
        dac.start_idx(tone_idx)
        tx_on = True
    elif (not ptt_active) and tx_on:
        dac.stop()
        tx_on = False

    # Combo factory reset.
    both_down = btn_mode.is_pressed() and btn_tone.is_pressed()
    if both_down:
        if not combo_active:
            combo_start = now
            combo_active = True
            rgb.set(_RGB_RESET)
        elif time.ticks_diff(now, combo_start) >= COMBO_MIN_MS:
            persist_erase()
            rgb.flash(_RGB_SAVED, 120, 3)
            tone_idx = DEF_TONE_IDX
            pol = DEF_POL
            tx_on = False
            dac.stop()
            ptt.set_pol(pol)
            combo_active = False
            cfg_dirty = False
            saved_idx = tone_idx
            saved_pol = pol
            btn_mode.suppress_pending()
            btn_tone.suppress_pending()
            msg_id = _MSG_RESET
            msg_until_ms = time.ticks_add(now, 1500)
            render_ui(oled, tone_idx, pol, tx_on, _MSG_RESET)
            continue
    else:
        if combo_active:
            combo_active = False

    # MODE - toggle POL.
    if not combo_active and ev_mode == Button.EV_SHORT:
        pol = 0 if pol == 1 else 1
        ptt.set_pol(pol)
        cfg_dirty = True
        cfg_save_at = time.ticks_add(now, AUTOSAVE_DELAY_MS)

    # TONE+ events.
    if not combo_active:
        if ev_tone == Button.EV_SHORT:
            tone_idx += 1
            if tone_idx >= N_TONES:
                tone_idx = 0
            if tx_on:
                dac.retune_idx(tone_idx)
            cfg_dirty = True
            cfg_save_at = time.ticks_add(now, AUTOSAVE_DELAY_MS)
        elif ev_tone == Button.EV_LONG:
            if persist_save(tone_idx, pol):
                saved_idx = tone_idx
                saved_pol = pol
                cfg_dirty = False
                rgb.flash(_RGB_SAVED, 250, 3)            # 1.5 s ярък бял.
                msg_id = _MSG_SAVED
            else:
                rgb.flash(_RGB_ERROR, 200, 3)
                msg_id = _MSG_FAILED
            msg_until_ms = time.ticks_add(now, 1500)
            render_ui(oled, tone_idx, pol, tx_on, msg_id)

    # Auto-save.
    if cfg_dirty and not combo_active and time.ticks_diff(now, cfg_save_at) >= 0:
        if tone_idx != saved_idx or pol != saved_pol:
            if persist_save(tone_idx, pol):
                saved_idx = tone_idx
                saved_pol = pol
                rgb.flash(_RGB_SAVED, 200, 2)            # 800 ms ярък бял.
                msg_id = _MSG_SAVED
                msg_until_ms = time.ticks_add(now, 1000)
                render_ui(oled, tone_idx, pol, tx_on, msg_id)
            else:
                rgb.flash(_RGB_ERROR, 150, 3)
        cfg_dirty = False

    # Msg overlay expiry.
    if msg_id != _MSG_NORMAL and time.ticks_diff(now, msg_until_ms) >= 0:
        msg_id = _MSG_NORMAL
        render_ui(oled, tone_idx, pol, tx_on, _MSG_NORMAL)
        last_idx = tone_idx
        last_pol = pol
        last_tx = tx_on

    # Lazy render.
    if msg_id == _MSG_NORMAL:
        if tone_idx != last_idx or pol != last_pol or tx_on != last_tx:
            render_ui(oled, tone_idx, pol, tx_on, _MSG_NORMAL)
            last_idx = tone_idx
            last_pol = pol
            last_tx = tx_on

    # RGB state.
    if not combo_active:
        rgb.set(rgb_for_state(tx_on, pol))

    time.sleep_ms(LOOP_TICK_MS)
