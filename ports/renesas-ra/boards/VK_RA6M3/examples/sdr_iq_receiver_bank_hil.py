"""On-target HIL for the TESTER receiver bank and movable injection path.

Provision ``/flash/iqbank`` with ``make_sdriq_receiver_bank.py`` output first.
The script deliberately quiesces a live SdrApp/event loop before claiming the
static IQADC/DAC hardware.  A J-Link reset is required after it finishes.

Phase A runs all eight synthetic/real profiles through the actual demodulator
with SCOPE off and reads mono audio.  Phase B selects stage-5 I/Q explicitly,
tests IN, all three movable MID positions and OUT with the synthetic fixtures,
then exercises every real 24-kS/s asset at M:NCO.  Listening/visual inspection
remains the final proof of programme content.
"""

import array
import gc
import time
from machine import ADC, DAC, IQADC
import sdr_single


RATE = 48000
BLOCK = 128
PAIRS = BLOCK // 2
TAP = array.array("h", bytes(PAIRS * 4))
BARS = array.array("h", bytes(27 * 2))
COUNTERS = array.array("i", bytes(6 * 4))
AUDIO = array.array("H", bytes(PAIRS * 2))

PROFILES = (
    ("AM", "am", 6000, "am48.sdriq", "am24.sdriq"),
    ("USB", "usb", 2400, "usb48.sdriq", "usb24.sdriq"),
    ("LSB", "lsb", 2400, "lsb48.sdriq", "lsb24.sdriq"),
    ("CW", "cw", 500, "cw48.sdriq", "cw24.sdriq"),
    ("R:AM", "am", 6000, "zam48.sdriq", "zam24.sdriq"),
    ("R:USB", "usb", 2400, "zusb48.sdriq", "zusb24.sdriq"),
    ("R:LSB", "lsb", 2400, "zlsb48.sdriq", "zlsb24.sdriq"),
    ("R:CW", "cw", 500, "zcw48.sdriq", "zcw24.sdriq"),
)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def new_source():
    return sdr_single._IqFileSource(
        bytearray(8192), bytearray(8192), bytearray(32),
        array.array("i", bytes(15 * 4)))


def quiesce_existing_app():
    """Stop the timer/UI backend before taking the static IQADC singleton."""
    keep = getattr(sdr_single, "_KEEP", {})
    loop = keep.get("loop")
    app = keep.get("app")
    if loop is not None:
        loop.deinit()
    if app is not None:
        app.stop_rx()
    if loop is not None or app is not None:
        print("BANK HIL quiesced live UI; J-Link reset required afterwards")


def tap_frame(iq, timeout_ms=250):
    iq.tap(0)
    iq.tap(3)
    deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        count = iq.tap(3, TAP)
        if count is not None:
            return int(count)
        time.sleep_ms(2)
    raise RuntimeError("no fresh channel-filter tap")


def bars_frame(iq, timeout_ms=250):
    deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        count = iq.spectrum_bars(BARS)
        if count is not None:
            return int(count)
        time.sleep_ms(2)
    raise RuntimeError("no fresh spectrum")


def checked_status(source, label, sample_rate):
    status = source.poll()
    check(status is not None, label + " status")
    check(status[0] and status[1] and status[2], label + " inactive")
    check(status[10] == 0 and status[14] == 0, label + " refill failure")
    check(source.sample_rate == sample_rate and source.sample_bits == 16,
          label + " metadata")
    return status


def audio_frame(iq, label):
    # One discard lets the Hilbert/DC/audio state settle while keeping the
    # 512-sample ring well below overflow without a concurrent DAC consumer.
    time.sleep_ms(12)
    check(iq.read_audio(AUDIO) == PAIRS, label + " audio discard")
    time.sleep_ms(8)
    check(iq.read_audio(AUDIO) == PAIRS, label + " audio length")
    lo = min(AUDIO)
    hi = max(AUDIO)
    mean = sum(AUDIO) // len(AUDIO)
    energy = sum((int(value) - mean) * (int(value) - mean)
                 for value in AUDIO)
    check(hi - lo >= 6 and energy > len(AUDIO) * 4,
          label + " blank demod audio")
    return lo, hi, energy


def run_demod_profile(iq, source, label, mode, bandwidth, filename):
    path = "/flash/iqbank/" + filename
    try:
        iq.scope(0)                 # scope OFF is normal mono demodulation
        iq.demod(mode)
        iq.bandwidth(bandwidth)
        source.start(iq, iq.INJECT_POINT_IN, 48000, True, path)
        lo, hi, energy = audio_frame(iq, label)
        status = checked_status(source, label, 48000)
        print("DEMOD", label, "audio", lo, hi, "energy", energy,
              "samples", status[12])
    finally:
        source.stop()


def run_route_profile(iq, source, label, mode, bandwidth, route_label,
                      point, mid_stage, sample_rate, filename):
    path = "/flash/iqbank/" + filename
    try:
        iq.demod(mode)
        iq.bandwidth(bandwidth)
        if mid_stage is not None:
            check(iq.inject_mid(mid_stage) == mid_stage,
                  label + " " + route_label + " MID readback")
        source.start(iq, point, sample_rate, True, path)
        # A real CW recording contains intentional key-up silence.  Search a
        # bounded interval for an active fresh frame instead of sampling once
        # at 50 ms, which lands in a roughly 552-ms key-up gap in zcw24.sdriq.
        deadline = time.ticks_add(time.ticks_ms(), 1000)
        energy = 0
        peak = 0
        count = 0
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            count = tap_frame(iq, 80)
            check(count == PAIRS, label + " tap length")
            energy = sum(TAP[2 * i] * TAP[2 * i] +
                         TAP[2 * i + 1] * TAP[2 * i + 1]
                         for i in range(count))
            peak = max(max(abs(TAP[2 * i]), abs(TAP[2 * i + 1]))
                       for i in range(count))
            if energy > count * 16:
                break
        status = checked_status(source, label + " " + route_label, sample_rate)
        check(energy > count * 16, label + " blank filtered I/Q")
        check(peak <= 2200, label + " point-level mismatch/clipping")
        check(bars_frame(iq) == len(BARS) and max(BARS) > 0,
              label + " blank spectrum")
        print("ROUTE", label, route_label, sample_rate, "peak", peak,
              "energy", energy, "samples", status[12], "blocks", status[11])
    finally:
        source.stop()


iq = None
dac_i = None
dac_q = None
source = None
try:
    quiesce_existing_app()
    gc.collect()
    source = new_source()
    iq = IQADC("P000", "P004", rate=RATE, block=BLOCK,
               pga=ADC.PGA_BYPASS)
    check(iq.FILE_API_VERSION == 1, "FILE API")
    iq.start()
    iq.tune(0)
    iq.iq_correction(enable=False)
    iq.chf_kernel(True)
    iq.agc("off")
    iq.volume(0.5)
    # Phase A: prove that the chosen AM/USB/LSB/CW demodulator really runs.
    for profile in PROFILES:
        label, mode, bandwidth, path48, path24 = profile
        run_demod_profile(iq, source, label, mode, bandwidth, path48)

    # Phase B: explicit I/Q scope route.  This intentionally suppresses demod and
    # proves both DAC streams plus every distinct insertion boundary.
    iq.scope(5)
    dac_i = DAC("P014")
    dac_i.stream_from(iq)
    dac_q = DAC("P015")
    dac_q.stream_from(iq)
    routes = (
        ("IN", iq.INJECT_POINT_IN, None, 48000),
        ("M:IQC", iq.INJECT_POINT_MID, iq.INJECT_MID_IQCORR, 24000),
        ("M:NCO", iq.INJECT_POINT_MID, iq.INJECT_MID_NCO, 24000),
        ("M:CHF", iq.INJECT_POINT_MID, iq.INJECT_MID_CHFILT, 24000),
        ("OUT", iq.INJECT_POINT_OUT, None, 24000),
    )
    for profile in PROFILES[:4]:
        label, mode, bandwidth, path48, path24 = profile
        for route_label, point, mid_stage, sample_rate in routes:
            filename = path48 if sample_rate == 48000 else path24
            run_route_profile(iq, source, label, mode, bandwidth, route_label,
                              point, mid_stage, sample_rate, filename)

    # The real 48-kS/s files were demodulated above; now also prove all real 24-kS/s
    # assets through the historical MID=NCO boundary.
    for profile in PROFILES[4:]:
        label, mode, bandwidth, _path48, path24 = profile
        run_route_profile(iq, source, label, mode, bandwidth, "M:NCO",
                          iq.INJECT_POINT_MID, iq.INJECT_MID_NCO, 24000, path24)

    check(iq.counters(COUNTERS) == 6, "counter read")
    check(COUNTERS[2] == 0 and COUNTERS[3] == 0 and COUNTERS[4] == 0,
          "realtime counter failure")
    check(dac_i.playing() and dac_q.playing(), "paired DAC stopped")
    print("IQ RECEIVER BANK HIL PASS", len(PROFILES), "profiles",
          list(COUNTERS), iq.timing())
    print("J-LINK RESET REQUIRED")
finally:
    if source is not None:
        try:
            source.stop()
        except Exception as exc:
            print("CLEANUP FILE", repr(exc))
    if dac_q is not None:
        try:
            dac_q.stop()
        except Exception as exc:
            print("CLEANUP DAC1", repr(exc))
    if dac_i is not None:
        try:
            dac_i.stop()
        except Exception as exc:
            print("CLEANUP DAC0", repr(exc))
    if iq is not None:
        for name, args in (("file_free", ()), ("scope", (0,)),
                           ("stop", ()), ("deinit", ())):
            try:
                getattr(iq, name)(*args)
            except Exception as exc:
                print("CLEANUP IQ", name, repr(exc))
