# Dual DAC demo: DA0 = pure triangle (half period), DA1 = triangle+sine mix
# DA0 period = 20 ms (50 Hz), DA1 period = 40 ms (25 Hz)
from array import array
import math
import time
from machine import DAC, Pin

MID = 2048
AMP = 1800
SINE_AMP = 500
TRIANGLE_PERIOD_MS = 40
SINE_CYCLES_PER_TRIANGLE = 20
TBL = 256            # samples for DA1 (full period)
TBL_HALF = TBL // 2  # 128 samples for DA0 (half period)
FREQ_HZ = 1000 // TRIANGLE_PERIOD_MS  # 25 Hz (DA1)
FREQ_HZ_HALF = FREQ_HZ * 2            # 50 Hz (DA0)
SR = FREQ_HZ * TBL   # sample rate (same for both DACs)


def clamp12(v):
    if v < 0:
        return 0
    if v > 4095:
        return 4095
    return v


# --- CH0: pure triangle, HALF period (128 samples) ---
tri_buf = array("H", [MID] * TBL_HALF)
for i in range(TBL_HALF):
    phase = i / TBL_HALF
    tri = 4.0 * abs(phase - 0.5) - 1.0  # -1..+1
    tri_buf[i] = clamp12(MID + int(AMP * tri))

# --- CH1: triangle + sine mix, full period (256 samples) ---
mix_buf = array("H", [MID] * TBL)
for i in range(TBL):
    phase = i / TBL
    tri = 4.0 * abs(phase - 0.5) - 1.0
    sine = math.sin(2.0 * math.pi * SINE_CYCLES_PER_TRIANGLE * phase)
    mix_buf[i] = clamp12(MID + int(AMP * 0.7 * tri + SINE_AMP * sine))

dac0 = DAC(Pin("P014"))  # DA0
dac1 = DAC(Pin("P015"))  # DA1
dac0.write(MID)
dac1.write(MID)

sine_freq_hz = FREQ_HZ * SINE_CYCLES_PER_TRIANGLE
print("Dual DAC waveform demo")
print("DA0 (P014): pure triangle, {} Hz (half period, {} ms)".format(
    FREQ_HZ_HALF, TRIANGLE_PERIOD_MS // 2))
print("DA1 (P015): triangle+sine mix, {} Hz ({} ms)".format(
    FREQ_HZ, TRIANGLE_PERIOD_MS))
print("  Sine cycles per period: {}".format(SINE_CYCLES_PER_TRIANGLE))
print("  Sine freq: {} Hz".format(sine_freq_hz))
print("  DA0: {} samples, DA1: {} samples @ {} Hz".format(
    TBL_HALF, TBL, SR))
print("Ctrl+C to stop.")

dac0.write_timed(tri_buf, SR, mode=DAC.CIRCULAR, transfer=DAC.TRANSFER_DTC)
dac1.write_timed(mix_buf, SR, mode=DAC.CIRCULAR, transfer=DAC.TRANSFER_DTC)

try:
    while True:
        time.sleep_ms(500)
except KeyboardInterrupt:
    pass

dac0.stop()
dac1.stop()
dac0.write(MID)
dac1.write(MID)
print("Stopped.")
