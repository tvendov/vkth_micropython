# spectrum_msgeq7_test.py — MSGEQ7 7x8 bar graph от AudioADC.
#
# Чете реален сигнал от P001 → FFT → 7 MSGEQ7 ленти → 56 WS2812 LED.
# 7 ленти x 8 LED = 56. Долните = зелени, средни = жълти, горни = червени.
# Свържи сигнал генератор към P001.
# Ctrl+C за спиране.

from machine import Pin, AudioADC, WS2812, Timer
from array import array
import dsp
import sys
import time

# --- Конфигурация -----------------------------------------------------------
ADC_PIN       = "P001"
PWR_PIN       = "P500"
DATA_PIN      = "P112"
N_PIXELS      = 56
N_MSGEQ7      = 7
LEDS_PER_BAND = 8       # 56 / 7
FFT_LEN       = 128
HALF          = FFT_LEN // 2 + 1
FS_HZ         = 22050
BRIGHTNESS    = 60
DECAY         = 12
DB_FLOOR      = -60

# --- Хардуер -----------------------------------------------------------------
vcc = Pin(PWR_PIN, Pin.OUT, value=1)
time.sleep_ms(50)
strip = WS2812(pixel_count=N_PIXELS, pin=Pin(DATA_PIN), channels=3)
adc   = AudioADC(ADC_PIN, fs=FS_HZ, frame=FFT_LEN)

# --- DSP — 7 MSGEQ7 ленти ---------------------------------------------------
fft = dsp.FFT(FFT_LEN)
fft.set_bands_msgeq7(FS_HZ, DB_FLOOR)

# --- Pre-allocated буфери ----------------------------------------------------
frame_buf = array('f', (0.0 for _ in range(FFT_LEN)))
fft_buf   = array('f', (0.0 for _ in range(FFT_LEN)))
mag_buf   = array('f', (0.0 for _ in range(HALF)))
band_buf  = bytearray(N_MSGEQ7)
peak_buf  = bytearray(N_MSGEQ7)
pixel_buf = bytearray(N_PIXELS * 3)

_NAMES = ("63", "160", "400", "1k", "2.5k", "6.25k", "16k")

# --- Console render constants ------------------------------------------------
BAND_W        = 8          # символи на лента (= LEDS_PER_BAND)
DISP_W        = N_MSGEQ7 * BAND_W + N_MSGEQ7   # 56 + 7 разделителя
DISP_H        = LEDS_PER_BAND                   # 8 реда

_ASCII_PIPE   = 124
_ASCII_HASH   = 35    # '#' зелено ниво
_ASCII_EQ     = 61    # '=' жълто ниво
_ASCII_STAR   = 42    # '*' червено ниво
_ASCII_SPACE  = 32
_ASCII_PLUS   = 43
_ASCII_MINUS  = 45
_ASCII_GT     = 62
_ASCII_ZERO   = 48

# render_rows[0] = header, [1..DISP_H] = data, [DISP_H+1] = axis, [DISP_H+2] = labels
_HDR_IDX    = 0
_DATA_START = 1
_AXIS_IDX   = 1 + DISP_H
_LBL_IDX    = 2 + DISP_H
_ROW_COUNT  = 3 + DISP_H

_HDR_FRM_POS = 7
_HDR_FRM_W   = 6

render_rows = [None] * _ROW_COUNT
_cursor_up  = "\x1b[%dA" % _ROW_COUNT
_first_render = True

def _fill(buf, pos, n, ch):
    for j in range(n):
        buf[pos + j] = ch

def _write_uint(buf, pos, width, value):
    _fill(buf, pos, width, _ASCII_SPACE)
    p = pos + width - 1
    if value <= 0:
        buf[p] = _ASCII_ZERO
        return
    while value > 0 and p >= pos:
        buf[p] = _ASCII_ZERO + (value % 10)
        value //= 10
        p -= 1

def _make_ba(text):
    return bytearray((text + "\n").encode())

def init_console():
    render_rows[_HDR_IDX] = _make_ba("Frame:000000")
    for r in range(DISP_H):
        row = bytearray(DISP_W + 2)
        row[0] = _ASCII_PIPE
        for b in range(N_MSGEQ7):
            base = 1 + b * (BAND_W + 1)
            _fill(row, base, BAND_W, _ASCII_SPACE)
            row[base + BAND_W] = _ASCII_PIPE
        row[DISP_W + 1] = ord('\n')
        render_rows[_DATA_START + r] = row
    axis = bytearray(DISP_W + 3)
    axis[0] = _ASCII_PLUS
    _fill(axis, 1, DISP_W, _ASCII_MINUS)
    axis[DISP_W + 1] = _ASCII_GT
    axis[DISP_W + 2] = ord('\n')
    render_rows[_AXIS_IDX] = axis
    lbl = bytearray(DISP_W + 2)
    _fill(lbl, 0, DISP_W + 2, _ASCII_SPACE)
    lbl[DISP_W + 1] = ord('\n')
    for b in range(N_MSGEQ7):
        nm = _NAMES[b]
        base = 1 + b * (BAND_W + 1)
        for i in range(len(nm)):
            lbl[base + i] = ord(nm[i])
    render_rows[_LBL_IDX] = lbl

init_console()

def render_console(frame_no):
    global _first_render
    _write_uint(render_rows[_HDR_IDX], _HDR_FRM_POS, _HDR_FRM_W, frame_no)
    for r in range(DISP_H):
        row_idx = DISP_H - 1 - r   # r=0 = горен ред
        threshold = (row_idx * 256) // DISP_H
        row = render_rows[_DATA_START + r]
        for b in range(N_MSGEQ7):
            base = 1 + b * (BAND_W + 1)
            level = peak_buf[b]
            if level > threshold:
                led_pos = row_idx
                if led_pos >= 6:
                    ch = _ASCII_STAR   # '*' червено (LED 7-8)
                elif led_pos >= 3:
                    ch = _ASCII_EQ     # '=' жълто (LED 4-6)
                else:
                    ch = _ASCII_HASH   # '#' зелено (LED 1-3)
                _fill(row, base, BAND_W, ch)
            else:
                _fill(row, base, BAND_W, _ASCII_SPACE)
    if _first_render:
        _first_render = False
    else:
        sys.stdout.write(_cursor_up)
    for i in range(_ROW_COUNT):
        sys.stdout.write(render_rows[i])

_ws_done = bytearray(1)
def _ws_cb(_t):
    _ws_done[0] = 1
# Hard timer: fires from ISR even when Python is blocked by stdout writes.
# Timer(-1) soft timer cannot fire during sys.stdout.write() blocking calls.
_latch = Timer(5)

# --- MSGEQ7 bar graph render ------------------------------------------------
def render_msgeq7():
    for b in range(N_MSGEQ7):
        level = peak_buf[b]
        lit = (level * LEDS_PER_BAND + 127) >> 8
        for j in range(LEDS_PER_BAND):
            ofs = (b * LEDS_PER_BAND + j) * 3
            if j < lit:
                bv = (level * BRIGHTNESS) >> 8
                if j < 3:
                    pixel_buf[ofs] = 0; pixel_buf[ofs+1] = bv; pixel_buf[ofs+2] = 0
                elif j < 6:
                    pixel_buf[ofs] = bv; pixel_buf[ofs+1] = bv; pixel_buf[ofs+2] = 0
                else:
                    pixel_buf[ofs] = bv; pixel_buf[ofs+1] = 0; pixel_buf[ofs+2] = 0
            else:
                pixel_buf[ofs] = 0; pixel_buf[ofs+1] = 0; pixel_buf[ofs+2] = 0

def peak_hold():
    for i in range(N_MSGEQ7):
        v = band_buf[i]
        p = peak_buf[i]
        if v >= p:
            peak_buf[i] = v
        elif p > DECAY:
            peak_buf[i] = p - DECAY
        else:
            peak_buf[i] = 0

def do_frame():
    adc.read_f32(frame_buf)
    # DC removal
    s = 0.0
    for i in range(FFT_LEN):
        s += frame_buf[i]
    s /= FFT_LEN
    for i in range(FFT_LEN):
        frame_buf[i] -= s
    fft.window_apply(frame_buf, fft_buf)
    fft.run(fft_buf, fft_buf)
    fft.magnitude(fft_buf, mag_buf)
    fft.bands(mag_buf, band_buf)
    peak_hold()
    render_msgeq7()
    strip.write_buf(pixel_buf)
    strip.write_async()
    _ws_done[0] = 0
    _latch.init(mode=Timer.ONE_SHOT, period=2, callback=_ws_cb, hard=True)

# --- Main loop ---------------------------------------------------------------
print("=== MSGEQ7 Spectrum (ADC %s + LED) ===" % ADC_PIN)
print("7 bands x 8 LED, Fs=%d Hz" % FS_HZ)
print("Ctrl+C to stop.\n")

adc.start()
frames = 0
try:
    while True:
        while not adc.ready():
            pass
        strip.sync()
        do_frame()
        frames += 1
        if frames % 8 == 0:
            render_console(frames)
except KeyboardInterrupt:
    adc.stop()
    strip.fill((0, 0, 0))
    strip.write()
    strip.deinit()
    adc.deinit()
    print("Stopped. Frames:", frames)
