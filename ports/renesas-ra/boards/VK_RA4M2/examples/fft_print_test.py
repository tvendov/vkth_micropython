# fft_print_test.py  --  FFT от AudioADC, САМО конзола, БЕЗ лента.
#
# Печата ВСИЧКИ 64 FFT бина с:
#   bin | центр.честота | магнитуда | ASCII bar
# Усреднява 8 кадъра за стабилен резултат, печата на 2 секунди.
# Ctrl+C за спиране.

from machine import AudioADC, Pin, Timer
from array import array
import dsp
import sys
import time

# --- HW Timer(6) toggle P302: квадратна вълна 500 Hz ---
# Timer callback toggle-ва на 1000 Hz -> период 2 ms = 500 Hz.
# Свържи P302 → P001 с кабелче.
TOGGLE_PIN = "P302"
TOGGLE_HZ  = 500

tpin = Pin(TOGGLE_PIN, Pin.OUT, value=0)

_cnt = 0
_tval = 0
def _toggle(t):
    global _cnt, _tval
    _tval = 1 - _tval
    tpin.value(_tval)
    _cnt += 1
ttmr = Timer(6)
ttmr.init(freq=TOGGLE_HZ * 2, callback=_toggle, hard=False)

time.sleep_ms(100)
print("Timer(6) test: %d callbacks in 100ms (expect ~%d)" % (_cnt, TOGGLE_HZ * 2 // 10))

# --- Конфигурация -----------------------------------------------------------
ADC_PIN   = "P001"
FFT_LEN   = 128
HALF      = FFT_LEN // 2 + 1    # 65 бина (0=DC .. 64=Nyquist)
N_BINS    = FFT_LEN // 2         # 64 полезни бина (1..64)
FS_HZ     = 22050
BIN_HZ    = FS_HZ / FFT_LEN     # 172.27 Hz на bin
AVG_N     = 8                    # брой кадри за усредняване
HEIGHT    = 20                   # рендер редове по Y (магнитуд)
DISP_W    = N_BINS               # рендер колони по X (bin 1..64)

# индекси в render_lines
HDR_IDX        = 0
DATA_START     = 1
AXIS_IDX       = 1 + HEIGHT
FREQ_IDX       = 2 + HEIGHT
RENDER_LINE_COUNT = 3 + HEIGHT

# позиции в header bytearray:
# "Snap:000000  max:XXXXXXXXX  dc:+XXXXXXX  avg:N frm"
SNAP_POS      = 5
SNAP_W        = 6
MAX_POS       = 17
MAX_W         = 9
DC_SIGN_POS   = 31
DC_POS        = 32
DC_W          = 7

ASCII_PIPE  = 124
ASCII_GT    = 62
ASCII_PLUS  = 43
ASCII_HASH  = 35
ASCII_SPACE = 32
ASCII_ZERO  = 48
ASCII_DOT   = 46
ASCII_MINUS = 45

dc_sum = 0.0   # натрупва средата (DC offset) за всеки кадър

render_lines = [None] * RENDER_LINE_COUNT
cursor_up = "\x1b[%dA" % RENDER_LINE_COUNT
first_render = True

# --- DSP + ADC ---------------------------------------------------------------
fft = dsp.FFT(FFT_LEN)
adc = AudioADC(ADC_PIN, fs=FS_HZ, frame=FFT_LEN)

# --- Буфери ------------------------------------------------------------------
frame_buf = array('f', (0.0 for _ in range(FFT_LEN)))
fft_buf   = array('f', (0.0 for _ in range(FFT_LEN)))
mag_buf   = array('f', (0.0 for _ in range(HALF)))
avg_buf   = array('f', (0.0 for _ in range(HALF)))   # натрупване за средно

# --- Функции -----------------------------------------------------------------
def do_fft():
    """ADC -> DC remove -> FFT -> mag_buf."""
    global dc_sum
    adc.read_f32(frame_buf)
    s = 0.0
    for i in range(FFT_LEN):
        s += frame_buf[i]
    s /= FFT_LEN
    dc_sum += s
    for i in range(FFT_LEN):
        frame_buf[i] -= s
    fft.window_apply(frame_buf, fft_buf)
    fft.run(fft_buf, fft_buf)
    fft.magnitude(fft_buf, mag_buf)

def accumulate():
    """Добавя mag_buf към avg_buf."""
    for i in range(HALF):
        avg_buf[i] += mag_buf[i]

def clear_avg():
    """Нулира avg_buf и dc_sum."""
    global dc_sum
    dc_sum = 0.0
    for i in range(HALF):
        avg_buf[i] = 0.0

def _make_ba(text):
    return bytearray((text + "\n").encode())

def _fill(buf, pos, width, ch):
    for j in range(width):
        buf[pos + j] = ch

def write_uint_right(buf, pos, width, value):
    _fill(buf, pos, width, ASCII_SPACE)
    p = pos + width - 1
    if value <= 0:
        buf[p] = ASCII_ZERO
        return
    while value > 0 and p >= pos:
        buf[p] = ASCII_ZERO + (value % 10)
        value //= 10
        p -= 1

def write_fixed1_right(buf, pos, width, value):
    _fill(buf, pos, width, ASCII_SPACE)
    if value < 0.0:
        value = 0.0
    scaled = int(value * 10.0 + 0.5)
    frac  = scaled % 10
    whole = scaled // 10
    p = pos + width - 1
    buf[p] = ASCII_ZERO + frac;  p -= 1
    buf[p] = ASCII_DOT;          p -= 1
    if whole == 0:
        buf[p] = ASCII_ZERO
    else:
        while whole > 0 and p >= pos:
            buf[p] = ASCII_ZERO + (whole % 10)
            whole //= 10
            p -= 1

def init_render():
    render_lines[HDR_IDX] = _make_ba(
        "Snap:000000  max:XXXXXXXXX  dc:+XXXXXXX  avg:%d frm" % AVG_N)
    for r in range(HEIGHT):
        row = bytearray(DISP_W + 2)
        row[0] = ASCII_PIPE
        _fill(row, 1, DISP_W, ASCII_SPACE)
        row[DISP_W + 1] = ord('\n')
        render_lines[DATA_START + r] = row
    axis = bytearray(DISP_W + 3)
    axis[0] = ASCII_PLUS
    _fill(axis, 1, DISP_W, ord('-'))
    axis[DISP_W + 1] = ASCII_GT
    axis[DISP_W + 2] = ord('\n')
    render_lines[AXIS_IDX] = axis
    # частотни etiketi: ляво, средa, дясно
    lbl = bytearray(DISP_W + 2)
    _fill(lbl, 0, DISP_W + 2, ASCII_SPACE)
    lbl[DISP_W + 1] = ord('\n')
    s0 = "%dHz" % int(BIN_HZ)
    s1 = "%dHz" % int(32 * BIN_HZ)
    s2 = "%dHz" % int(64 * BIN_HZ)
    for i, c in enumerate(s0): lbl[1 + i] = ord(c)
    m = 1 + 32 - len(s1) // 2
    for i, c in enumerate(s1): lbl[m + i] = ord(c)
    e = 1 + DISP_W - len(s2)
    for i, c in enumerate(s2): lbl[e + i] = ord(c)
    render_lines[FREQ_IDX] = lbl

init_render()

def print_all_bins(snapshot):
    """Хоризонтален спектър: X=bin, Y=магнитуд. Без нови обекти."""
    global first_render
    mx = 0.0
    for i in range(1, HALF):
        v = avg_buf[i]
        if v > mx:
            mx = v
    if mx < 1e-6:
        mx = 1.0
    dc_avg = dc_sum / AVG_N
    write_uint_right(render_lines[HDR_IDX], SNAP_POS, SNAP_W, snapshot)
    write_fixed1_right(render_lines[HDR_IDX], MAX_POS, MAX_W, mx)
    render_lines[HDR_IDX][DC_SIGN_POS] = ASCII_PLUS if dc_avg >= 0.0 else ASCII_MINUS
    write_fixed1_right(render_lines[HDR_IDX], DC_POS, DC_W, dc_avg if dc_avg >= 0.0 else -dc_avg)
    for r in range(HEIGHT):
        threshold = mx * (HEIGHT - r) / HEIGHT
        row = render_lines[DATA_START + r]
        for b in range(DISP_W):
            row[1 + b] = ASCII_HASH if avg_buf[b + 1] >= threshold else ASCII_SPACE
    if first_render:
        first_render = False
    else:
        sys.stdout.write(cursor_up)
    for idx in range(RENDER_LINE_COUNT):
        sys.stdout.write(render_lines[idx])
    if hasattr(sys.stdout, "flush"):
        sys.stdout.flush()

# --- Main --------------------------------------------------------------------
print("=== FFT ALL BINS (ADC %s) ===" % ADC_PIN)
print("Toggle: %s -> square wave %d Hz (Timer(6) callback)" % (TOGGLE_PIN, TOGGLE_HZ))
print("FFT: %d pt  |  Fs: %d Hz  |  bin: %.2f Hz  |  bins: %d" % (
    FFT_LEN, FS_HZ, BIN_HZ, N_BINS))
print("Averaging %d frames per snapshot.  Ctrl+C to stop.\n" % AVG_N)

adc.start()

# --- Измерване на реалната Fs ---
MEASURE_FRAMES = 200
print("Measuring actual Fs over %d frames..." % MEASURE_FRAMES)
t0 = time.ticks_ms()
for _ in range(MEASURE_FRAMES):
    while not adc.ready():
        pass
    adc.read_f32(frame_buf)   # изчитаме кадъра (за да не блокира ready)
t1 = time.ticks_ms()
elapsed_ms = time.ticks_diff(t1, t0)
fs_actual = MEASURE_FRAMES * FFT_LEN * 1000.0 / elapsed_ms
bin_hz_actual = fs_actual / FFT_LEN
print("Elapsed: %d ms for %d frames x %d samples" % (elapsed_ms, MEASURE_FRAMES, FFT_LEN))
print("Fs_actual  = %.1f Hz  (configured: %d Hz)" % (fs_actual, FS_HZ))
print("bin_actual = %.2f Hz  (configured: %.2f Hz)" % (bin_hz_actual, BIN_HZ))
print()

snap = 0
try:
    while True:
        clear_avg()
        for _ in range(AVG_N):
            while not adc.ready():
                pass
            do_fft()
            accumulate()
        # Деление за средно
        inv = 1.0 / AVG_N
        for i in range(HALF):
            avg_buf[i] *= inv
        snap += 1
        print_all_bins(snap)
        time.sleep_ms(1500)
except KeyboardInterrupt:
    ttmr.deinit()
    tpin.value(0)
    adc.stop()
    adc.deinit()
    print("Stopped. Snapshots:", snap)
