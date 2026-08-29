"""Read-only DAC scope-capture probe for the current firmware ELF layout."""

import machine
import time

import sdr_single


# Addresses are from build-VK_RA6M3/firmware.elf (arm-none-eabi-nm -a -S).
SCOPE_ENABLE = 0x1FFE6058
SCOPE_HALF = 0x1FFE6059
SCOPE_WR = 0x1FFE605A
SCOPE_AUDIO = 0x1FFE605C

app = sdr_single._KEEP.get("app")
if app is None:
    app = sdr_single.start()

assert app._spec_lcd.scope_view() == 0
assert app._scope_view == 0
assert not app._modal
# The application must have re-armed this producer automatically after IQADC
# init; the test intentionally performs no pause/view transition of its own.
time.sleep_ms(20)

def scope_stats():
    lo = 32767
    hi = -32768
    nonzero = 0
    for i in range(1024):
        v = machine.mem16[SCOPE_AUDIO + 2 * i]
        if v >= 0x8000:
            v -= 0x10000
        if v < lo:
            lo = v
        if v > hi:
            hi = v
        if v:
            nonzero += 1
    return lo, hi, nonzero


w0 = machine.mem16[SCOPE_WR]
time.sleep_ms(37)
w1 = machine.mem16[SCOPE_WR]
time.sleep_ms(37)
w2 = machine.mem16[SCOPE_WR]
lo, hi, nonzero = scope_stats()

print("SCOPE_RAM", machine.mem8[SCOPE_ENABLE], machine.mem8[SCOPE_HALF],
      w0, w1, w2, lo, hi, nonzero, app._inj_on, app.p["v"])
assert machine.mem8[SCOPE_ENABLE] == 1, "scope producer disabled"
assert w0 != w1 or w1 != w2, "DAC capture write index is not moving"

# Prove amplitude as well as liveness with a temporary project-owned AM/SINE
# source.  Restore every backend setting before the runner resets the board.
iq = app.be.iq
saved = (app.be.mode, app.be.bw, app.be.vol)
try:
    assert app.be.set_mode("AM")
    assert app.be.set_bandwidth(18000)
    assert app.be.set_volume(50)
    iq.inject(True, 3000, 500, iq.INJECT_AM, 1000, 50, 0, 0,
              iq.INJECT_POINT_IN, iq.INJECT_WAVE_SINE)
    time.sleep_ms(250)
    tone_lo, tone_hi, tone_nz = scope_stats()
    print("SCOPE_TONE", tone_lo, tone_hi, tone_nz, tone_hi - tone_lo)
    assert tone_hi - tone_lo > 100, "test tone did not reach DAC scope capture"
finally:
    iq.inject(False)
    app.be.set_mode(saved[0])
    app.be.set_bandwidth(saved[1])
    app.be.set_volume(saved[2])

print("SCOPE_RAM PASS")
