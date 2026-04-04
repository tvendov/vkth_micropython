# Аудио спектрален анализатор — 32 ленти с WS2812 (VK_RA4M2).
# Ресурси на VK_RA4M2: ADC входове -> P000 (audio in, AC coupled, DC bias Vcc/2).
# Ресурси на VK_RA4M2: WS2812 = SCI2 backend -> P112 data, P500 power enable.
# Хардуер: DTC-driven ADC (ra_storm_adc) + CMSIS-DSP FFT (arm_rfft_fast_f32).
# Алгоритъм: PCM → Hamming window → RFFT → magnitude → 32 log bands → HSV render → WS2812.
# Нула GC allocation в главния цикъл — всички буфери са pre-allocated.
#
# Схема на входа (AC coupled audio input):
#   AUD_IN ---[100nF]---+--- P000
#                        |
#                      [100kΩ] -- 3.3V    <- DC bias = 1.65V ≈ ADC code 2048
#                        |
#                      [100kΩ] -- GND

from machine import Pin, AudioADC, WS2812
from array import array
import dsp
import time

# ---------------------------------------------------------------------------
# Конфигурация
# ---------------------------------------------------------------------------
ADC_PIN    = "P000"    # Аудио вход (AC coupled, DC bias Vcc/2 = 1.65V).
PWR_PIN    = "P500"    # Power enable за WS2812 модула.
DATA_PIN   = "P112"    # SCI2 data към DIN на WS2812 лентата.

N_PIXELS   = 32        # Брой WS2812 пиксели (= N_BANDS).
N_BANDS    = 32        # Брой честотни ленти (log-spaced, bin 1..64).
FFT_LEN    = 128       # Размер на FFT = max frame за RA_STORM_ADC (128).
FS_HZ      = 22050     # Честота на дискретизация (Nyquist = 11025 Hz).
BRIGHTNESS = 60        # Максимална яркост 0-255 (намалена за консумацията).
DECAY      = 12        # Peak hold decay (стъпки/кадър; ~1s при 20fps).
DB_FLOOR   = -60       # Шумов праг в dBFS: -40=по-малко чувствителен, -80=повече.

# ---------------------------------------------------------------------------
# Инициализация на хардуера
# ---------------------------------------------------------------------------
vcc = Pin(PWR_PIN, Pin.OUT, value=1)    # Включваме WS2812 захранването.
time.sleep_ms(50)                        # Изчакваме стабилизиране на модула.

strip = WS2812(pixel_count=N_PIXELS, pin=Pin(DATA_PIN), channels=3)
adc   = AudioADC(ADC_PIN, fs=FS_HZ, frame=FFT_LEN)

# ---------------------------------------------------------------------------
# DSP обекти
# ---------------------------------------------------------------------------
fft = dsp.FFT(FFT_LEN)
fft.set_bands(N_BANDS, DB_FLOOR)  # Log граници + dBFS скала ВЕДНЪЖ, преди цикъла.

# ---------------------------------------------------------------------------
# Pre-allocated буфери — нула GC allocation в главния цикъл.
# ---------------------------------------------------------------------------
frame_buf = array('f', [0.0] * FFT_LEN)              # PCM от AudioADC.read_f32().
fft_buf   = array('f', [0.0] * FFT_LEN)              # Windowed + FFT резултат.
mag_buf   = array('f', [0.0] * (FFT_LEN // 2 + 1))  # Магнитуди (N/2+1 bins).
band_buf  = bytearray(N_BANDS)                        # Текущи стойности 0-255.
peak_buf  = bytearray(N_BANDS)                        # Peak hold с decay.

# ---------------------------------------------------------------------------
# Цветова схема: дъга (червено = ниски честоти, синьо = високи).
# hsv_to_buf(h, v, buf, offset) — пише R,G,B директно в bytearray.
# Нула tuple allocation: не връща обект, само записва в буфера.
# h=0..255 (hue), v=0..255 (value/brightness).
# ---------------------------------------------------------------------------
def hsv_to_buf(h, v, buf, ofs):
    if v == 0:
        buf[ofs] = buf[ofs + 1] = buf[ofs + 2] = 0
        return
    region = h // 43
    rem    = (h - region * 43) * 6
    q = (v * (255 - rem)) >> 8
    t = (v * rem) >> 8
    if   region == 0: buf[ofs] = v; buf[ofs+1] = t; buf[ofs+2] = 0
    elif region == 1: buf[ofs] = q; buf[ofs+1] = v; buf[ofs+2] = 0
    elif region == 2: buf[ofs] = 0; buf[ofs+1] = v; buf[ofs+2] = t
    elif region == 3: buf[ofs] = 0; buf[ofs+1] = q; buf[ofs+2] = v
    elif region == 4: buf[ofs] = t; buf[ofs+1] = 0; buf[ofs+2] = v
    else:             buf[ofs] = v; buf[ofs+1] = 0; buf[ofs+2] = q

# Pre-calculate hue per band: 0 (red) → 170 (blue).
hues = bytearray(N_BANDS)
for i in range(N_BANDS):
    hues[i] = (i * 170) // N_BANDS

# Pre-allocated flat RGB pixel buffer — [R0,G0,B0, R1,G1,B1, ...].
# write_buf() копира в self->buf с ws2812_order без Python object allocation.
pixel_buf = bytearray(N_PIXELS * 3)

# ---------------------------------------------------------------------------
# Главен цикъл — неблокиращ polling, CPU свободен докато DTC записва.
# ---------------------------------------------------------------------------
print("=== Spectrum Analyzer 32-band ===")
print("ADC:", ADC_PIN, " Fs:", FS_HZ, "Hz  FFT:", FFT_LEN, " Bands:", N_BANDS)
print("WS2812: power =", PWR_PIN, " data =", DATA_PIN, " pixels =", N_PIXELS)
print("Ctrl+C to stop.")

adc.start()

try:
    while True:
        if not adc.ready():
            continue                              # CPU свободен; DTC записва.

        # --- DSP pipeline ---
        adc.read_f32(frame_buf)                   # Копиране от DTC ping-pong буфера.
        fft.window_apply(frame_buf, fft_buf)      # Hamming прозорец (намалява spectral leakage).
        fft.run(fft_buf, fft_buf)                # RFFT in-place (arm_rfft_fast_f32).
        fft.magnitude(fft_buf, mag_buf)           # Комплексни → реални магнитуди.
        fft.bands(mag_buf, band_buf)              # Log-спред към N_BANDS (нула powf()).

        # --- Peak hold с decay ---
        for i in range(N_BANDS):
            v = band_buf[i]
            p = peak_buf[i]
            if v >= p:
                peak_buf[i] = v
            else:
                peak_buf[i] = p - DECAY if p > DECAY else 0

        # --- Render: HSV → pixel_buf → write_buf() → WS2812 ---
        # Нула tuple allocation: hsv_to_buf пише директно в bytearray.
        # write_buf() прехвърля pixel_buf в self->buf с ws2812_order (C ниво).
        for i in range(N_PIXELS):
            bv = (peak_buf[i] * BRIGHTNESS) >> 8   # Мащабиране до BRIGHTNESS.
            hsv_to_buf(hues[i], bv, pixel_buf, i * 3)

        strip.write_buf(pixel_buf)   # Копира RGB → GRB в вътрешния буфер (C).
        strip.write()                # SCI2 DMA изпращане към WS2812 лентата.

except KeyboardInterrupt:
    adc.stop()
    strip.fill((0, 0, 0))
    strip.write()
    strip.deinit()
    print("Spectrum analyzer stopped.")
