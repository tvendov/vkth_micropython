from array import array
import math
import time

import audiomixer


SAMPLE_RATE = 22050
TRIANGLE_PERIOD_MS = 40
SINE_CYCLES_PER_TRIANGLE = 20


def make_triangle(period_ms, amplitude=0.78):
    count = max(64, SAMPLE_RATE * period_ms // 1000)
    out = array("h", [0] * count)

    for i in range(count):
        phase = i / count
        triangle = 4.0 * abs(phase - 0.5) - 1.0
        out[i] = int(32767 * amplitude * triangle)

    return out


def make_fast_sine(period_ms, sine_cycles, amplitude=0.22):
    count = max(64, SAMPLE_RATE * period_ms // 1000)
    out = array("h", [0] * count)

    for i in range(count):
        phase = i / count
        sine = math.sin(2.0 * math.pi * sine_cycles * phase)
        out[i] = int(32767 * amplitude * sine)

    return out


mixer = audiomixer.Mixer(
    voice_count=2,
    sample_rate=SAMPLE_RATE,
    channel_count=1,
    bits_per_sample=16,
    buffer_size=4096,
)

triangle_wave = make_triangle(TRIANGLE_PERIOD_MS)
sine_wave = make_fast_sine(TRIANGLE_PERIOD_MS, SINE_CYCLES_PER_TRIANGLE)

triangle_freq_hz = 1000 / TRIANGLE_PERIOD_MS
sine_freq_hz = triangle_freq_hz * SINE_CYCLES_PER_TRIANGLE

print("Starting scope-friendly audiomixer demo on DA0...")
print("Triangle period: {} ms".format(TRIANGLE_PERIOD_MS))
print("Triangle freq: {:.2f} Hz".format(triangle_freq_hz))
print("Sine cycles inside one triangle period: {}".format(SINE_CYCLES_PER_TRIANGLE))
print("Sine freq: {:.2f} Hz".format(sine_freq_hz))
print("Expected shape: triangle voice + sine voice mixed in real time.")
print("Press Ctrl+C to stop.")

try:
    mixer.voice[0].level = 1.0
    mixer.voice[1].level = 1.0
    mixer.voice[0].play(triangle_wave, repeat=True)
    mixer.voice[1].play(sine_wave, repeat=True)

    while True:
        time.sleep_ms(500)
except KeyboardInterrupt:
    print("Stopping audiomixer demo...")
finally:
    mixer.stop()
    mixer.deinit()
    print("Done.")
