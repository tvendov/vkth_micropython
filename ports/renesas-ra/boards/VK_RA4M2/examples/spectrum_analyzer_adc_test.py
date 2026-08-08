# spectrum_analyzer_adc_test.py — с реален ADC вход.
# AudioADC поддържа САМО P000 (AN000) — DTC каналът е хардкодиран.
# Свържи сигнал генератор към P000 (AC coupled или директно 0..3.3V).
# WS2812 лента на P112.   Ctrl+C за спиране.
#
# Очаквано поведение:
#   - Подай синус 1kHz → виждаш пик в лявата част на лентата
#   - Подай синус 5kHz → пикът се мести надясно
#   - Sweep → пикът пълзи по лентата
#   - Без сигнал → лентата е тъмна (само шум)

from machine import Pin, AudioADC, WS2812, Timer
from array import array
import dsp
import time

# --- Конфигурация ------------------------------------------------------------
ADC_PIN     = "P001"       # Сигнал генератор тук (P000, P001, P002... всички ADC пинове)
PWR_PIN     = "P500"
DATA_PIN    = "P112"
N_PIXELS    = 56
N_BANDS     = 32
FFT_LEN     = 128
HALF        = FFT_LEN // 2 + 1
FS_HZ       = 22050
BRIGHTNESS  = 60
DECAY       = 12
DB_FLOOR    = -60

# --- Хардуер -----------------------------------------------------------------
print("[1] Pin VCC ...", end="")
vcc = Pin(PWR_PIN, Pin.OUT, value=1)
time.sleep_ms(50)
print(" OK")

print("[2] WS2812 ...", end="")
strip = WS2812(pixel_count=N_PIXELS, pin=Pin(DATA_PIN), channels=3)
print(" OK")

print("[3] AudioADC(%s, fs=%d, frame=%d) ..." % (ADC_PIN, FS_HZ, FFT_LEN), end="")
adc = AudioADC(ADC_PIN, fs=FS_HZ, frame=FFT_LEN)
print(" OK")

# --- DSP ---------------------------------------------------------------------
print("[4] FFT(%d) ..." % FFT_LEN, end="")
fft = dsp.FFT(FFT_LEN)
fft.set_bands(N_BANDS, DB_FLOOR)
print(" OK")

# --- Буфери (всичко преди main loop, нула GC след тук) -----------------------
frame_buf = array('f', (0.0 for _ in range(FFT_LEN)))
fft_buf   = array('f', (0.0 for _ in range(FFT_LEN)))
mag_buf   = array('f', (0.0 for _ in range(HALF)))
band_buf  = bytearray(N_BANDS)
peak_buf  = bytearray(N_BANDS)
pixel_buf = bytearray(N_PIXELS * 3)

# Pixel→band LUT (56 pixels → 32 bands)
pix2band = bytearray(N_PIXELS)
for _i in range(N_PIXELS):
    pix2band[_i] = _i * N_BANDS // N_PIXELS

# Hue LUT per band
hue_lut = bytearray(N_BANDS)
for _i in range(N_BANDS):
    hue_lut[_i] = (_i * 170) // N_BANDS

# WS2812 latch
_ws_done = bytearray(1)
def _ws_cb(_t):
    _ws_done[0] = 1
_latch = Timer(-1)

# --- Zero-alloc функции ------------------------------------------------------
def hsv_to_buf(h, v, ofs):
    if v == 0:
        pixel_buf[ofs] = 0; pixel_buf[ofs+1] = 0; pixel_buf[ofs+2] = 0
        return
    region = h // 43
    rem = (h - region * 43) * 6
    q = (v * (255 - rem)) >> 8
    t = (v * rem) >> 8
    if   region == 0: pixel_buf[ofs]=v;  pixel_buf[ofs+1]=t; pixel_buf[ofs+2]=0
    elif region == 1: pixel_buf[ofs]=q;  pixel_buf[ofs+1]=v; pixel_buf[ofs+2]=0
    elif region == 2: pixel_buf[ofs]=0;  pixel_buf[ofs+1]=v; pixel_buf[ofs+2]=t
    elif region == 3: pixel_buf[ofs]=0;  pixel_buf[ofs+1]=q; pixel_buf[ofs+2]=v
    elif region == 4: pixel_buf[ofs]=t;  pixel_buf[ofs+1]=0; pixel_buf[ofs+2]=v
    else:             pixel_buf[ofs]=v;  pixel_buf[ofs+1]=0; pixel_buf[ofs+2]=q

def render_rainbow():
    for i in range(N_PIXELS):
        bi = pix2band[i]
        bv = (peak_buf[bi] * BRIGHTNESS) >> 8
        hsv_to_buf(hue_lut[bi], bv, i * 3)

def peak_hold():
    for i in range(N_BANDS):
        v = band_buf[i]
        p = peak_buf[i]
        if v >= p:
            peak_buf[i] = v
        elif p > DECAY:
            peak_buf[i] = p - DECAY
        else:
            peak_buf[i] = 0

# --- Main loop (ZERO ALLOC) --------------------------------------------------
print("[5] Buffers OK")
print("=== Spectrum Analyzer ADC TEST ===")
print("ADC:", ADC_PIN, " Fs:", FS_HZ, "Hz  FFT:", FFT_LEN)
print("WS2812:", DATA_PIN, " Pixels:", N_PIXELS, " Bands:", N_BANDS)
print("Ctrl+C to stop.\n")

print("[6] adc object:", adc)

print("[7] adc.start() ...", end="")
adc.start()
print(" OK")

print("[8] Polling adc.ready() + status() ...")
wait_ms = 0
while not adc.ready():
    time.sleep_ms(1)
    wait_ms += 1
    if wait_ms % 100 == 0:
        s = adc.status()
        print("  %4d ms: wi=%d ah=%d rm=%d fs=%d" % (
            wait_ms, s['write_index'], s['active_half'],
            s['ready_mask'], s['frame_sequence']))
    if wait_ms > 2000:
        print("  TIMEOUT after 2s!")
        s = adc.status()
        for k in sorted(s):
            print("    %s = %s" % (k, s[k]))
        raise SystemExit
print("  First frame ready after %d ms" % wait_ms)

print("[9] Reading first frame ...", end="")
adc.read_f32(frame_buf)
print(" OK")

print("[10] First 8 samples:")
for i in range(8):
    print("  [%d] = %.4f" % (i, frame_buf[i]))

_mn = frame_buf[0]
_mx = frame_buf[0]
for i in range(FFT_LEN):
    if frame_buf[i] < _mn: _mn = frame_buf[i]
    if frame_buf[i] > _mx: _mx = frame_buf[i]
print("  min=%.4f  max=%.4f  range=%.4f" % (_mn, _mx, _mx - _mn))
if _mx - _mn < 0.001:
    print("  WARNING: signal appears flat (no AC component)")

print("[10] Entering main loop ...")
frames = 0

def print_bars():
    """ASCII bar graph на 32 ленти + pixel RGB dump."""
    # Band values as bar
    s = "bands: "
    for i in range(N_BANDS):
        v = band_buf[i]
        if   v > 200: s += "#"
        elif v > 150: s += "="
        elif v > 100: s += "+"
        elif v >  50: s += "-"
        elif v >  10: s += "."
        else:         s += " "
    s += "|"
    print(s)
    # Peak values
    s = "peaks: "
    for i in range(N_BANDS):
        v = peak_buf[i]
        if   v > 200: s += "#"
        elif v > 150: s += "="
        elif v > 100: s += "+"
        elif v >  50: s += "-"
        elif v >  10: s += "."
        else:         s += " "
    s += "|"
    print(s)
    # First 16 pixels RGB
    s = "px0-15: "
    for i in range(16):
        r = pixel_buf[i*3]
        g = pixel_buf[i*3+1]
        b = pixel_buf[i*3+2]
        if r + g + b > 30:
            s += "*"
        elif r + g + b > 0:
            s += "."
        else:
            s += " "
    print(s)

try:
    while True:
        while not adc.ready():
            pass
        strip.sync()

        adc.read_f32(frame_buf)
        fft.window_apply(frame_buf, fft_buf)
        fft.run(fft_buf, fft_buf)
        fft.magnitude(fft_buf, mag_buf)
        fft.bands(mag_buf, band_buf)
        peak_hold()
        render_rainbow()

        strip.write_buf(pixel_buf)
        strip.write_async()
        _ws_done[0] = 0
        _latch.init(mode=Timer.ONE_SHOT, period=2, callback=_ws_cb)

        frames += 1
        if frames % 50 == 0:
            mx_b = 0; mx_v = 0
            for i in range(N_BANDS):
                if band_buf[i] > mx_v:
                    mx_v = band_buf[i]; mx_b = i
            _mn = frame_buf[0]; _mx = frame_buf[0]
            for i in range(FFT_LEN):
                if frame_buf[i] < _mn: _mn = frame_buf[i]
                if frame_buf[i] > _mx: _mx = frame_buf[i]
            print("--- f=%d  peak_band=%d  val=%d  sig=[%.3f..%.3f] ---" % (frames, mx_b, mx_v, _mn, _mx))
            print_bars()

except KeyboardInterrupt:
    adc.stop()
    strip.fill((0, 0, 0))
    strip.write()
    strip.deinit()
    print("Stopped. Frames:", frames)
