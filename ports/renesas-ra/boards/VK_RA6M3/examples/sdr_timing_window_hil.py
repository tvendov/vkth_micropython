"""On-target proof for the live IQADC DSP AVG/PK timing window.

Run after flashing the matching firmware.  The script stops a live SDR app,
measures three completed 500-ms windows with the configurable DSP blocks ON,
then three with blocks 2..10 BYP.  A separate J-Link reset is mandatory after
the script, whether it passes or fails.
"""

import gc
import time
from machine import DAC, IQADC


RATE = 48000
BLOCK = 128
WINDOW_TIMEOUT_MS = 1800


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def quiesce_existing_app():
    try:
        import sdr_single
        keep = getattr(sdr_single, "_KEEP", {})
        loop = keep.get("loop")
        app = keep.get("app")
        if loop is not None:
            loop.deinit()
        if app is not None:
            app.stop_rx()
        if loop is not None or app is not None:
            print("TIMING HIL quiesced live UI")
    except Exception as exc:
        print("TIMING HIL quiesce", repr(exc))


def wait_window(iq, after_seq, timeout_ms=WINDOW_TIMEOUT_MS):
    deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        tm = iq.timing()
        if tm.get("window_valid", False) and int(tm.get("window_seq", 0)) > after_seq:
            blocks = int(tm["window_blocks"])
            elapsed = int(tm["window_ms"])
            avg = int(tm["window_avg_cyc"])
            peak = int(tm["window_peak_cyc"])
            observed = int(tm["observed_block_cyc"])
            check(450 <= elapsed <= 650, "window_ms %d" % elapsed)
            check(blocks > 0, "window_blocks")
            check(0 < avg <= peak, "cycles avg=%d peak=%d" % (avg, peak))
            check(observed > 0, "observed_block_cyc")
            avg_pct = (avg * 100 + observed // 2) // observed
            peak_pct = (peak * 100 + observed // 2) // observed
            check(int(tm["avg_pct"]) == avg_pct, "avg_pct formula")
            check(int(tm["peak_pct"]) == peak_pct, "peak_pct formula")
            check(peak_pct < 100, "DSP deadline %d%%" % peak_pct)
            return tm
        time.sleep_ms(10)
    raise RuntimeError("no completed timing window")


def collect_windows(iq, count):
    out = []
    seq = int(iq.timing().get("window_seq", 0))
    for _ in range(count):
        tm = wait_window(iq, seq)
        seq = int(tm["window_seq"])
        out.append(tm)
    return out


def median_value(windows, key):
    values = sorted(int(item[key]) for item in windows)
    return values[len(values) // 2]


iq = None
dac = None
try:
    quiesce_existing_app()
    gc.collect()
    iq = IQADC("P000", "P004", rate=RATE, block=BLOCK)
    iq.start()
    dac = DAC("P014")
    dac.stream_from(iq)

    # Mirror TESTER GEN / IN / ON with a live AM fixture and the normal mono route.
    iq.scope(0)
    iq.demod("am")
    iq.bandwidth(6000)
    iq.chf_kernel(True)
    iq.inject(True, 3000, 1200, iq.INJECT_AM, 500, 70, 0, 2,
              iq.INJECT_POINT_IN, iq.INJECT_WAVE_SINE)
    for block_id in range(2, 11):
        iq.block(block_id, True)

    full = collect_windows(iq, 3)
    full_avg = median_value(full, "window_avg_cyc")
    full_pct = median_value(full, "avg_pct")
    full_peak_pct = median_value(full, "peak_pct")
    full_blocks = median_value(full, "window_blocks")

    before = iq.timing()
    before_generation = int(before["window_generation"])
    for block_id in range(2, 11):
        iq.block(block_id, False)
    invalid = iq.timing()
    check(not invalid["window_valid"], "BYP must invalidate current window")
    check(int(invalid["window_generation"]) > before_generation,
          "BYP generation did not advance")

    bypass = collect_windows(iq, 3)
    bypass_avg = median_value(bypass, "window_avg_cyc")
    bypass_pct = median_value(bypass, "avg_pct")
    bypass_peak_pct = median_value(bypass, "peak_pct")
    bypass_blocks = median_value(bypass, "window_blocks")

    check(bypass_avg < full_avg,
          "BYP did not reduce AVG: %d >= %d" % (bypass_avg, full_avg))
    check(dac.playing(), "DAC stream stopped")
    print("DSP TIMING WINDOW HIL PASS")
    print("ON  AVG/PK", full_pct, full_peak_pct, "cycles", full_avg,
          "blocks", full_blocks)
    print("BYP AVG/PK", bypass_pct, bypass_peak_pct, "cycles", bypass_avg,
          "blocks", bypass_blocks)
    print("AVG cycle saving", full_avg - bypass_avg)
    print("J-LINK RESET REQUIRED")
finally:
    if iq is not None:
        try:
            iq.inject(False)
        except Exception as exc:
            print("CLEANUP INJECT", repr(exc))
    if dac is not None:
        try:
            dac.stop()
        except Exception as exc:
            print("CLEANUP DAC", repr(exc))
    if iq is not None:
        for name in ("stop", "deinit"):
            try:
                getattr(iq, name)()
            except Exception as exc:
                print("CLEANUP IQ", name, repr(exc))
