"""On-target contract test for VFO-anchored spectrum navigation.

Run against an already booted ``sdr_single`` app and matching firmware.  The test
does one reversible NCO-only move, proves that it cannot start a new VFO/spectrum
generation or move the frequency axis, and exercises the signed generation
handshake.  It never changes the physical LO itself.  Requesting a fresh generation
for the already-confirmed LO intentionally restarts the retained waterfall history.

The framebuffer does not expose a pixel checksum or marker X through Python.  Exact
data-column stability and dynamic-marker wiring are therefore guarded by the
companion host test ``sdr_navigation_source_contract.py`` and still need one visual
constant-tone check after flash.
"""

import time

import sdr_single


GENERATION_TIMEOUT_MS = 1500
SETTLE_TIMEOUT_MS = 2500


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def wait_until(predicate, timeout_ms, message):
    deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        value = predicate()
        if value:
            return value
        time.sleep_ms(5)
    raise AssertionError(message)


app = sdr_single._KEEP.get("app")
if app is None:
    app = sdr_single.start()

lcd = app._spec_lcd
check(app._spec_native, "native spectrum is not attached")
check(lcd is not None, "LCD bridge is missing")
check(hasattr(lcd, "spectrum_center"), "transactional spectrum_center API missing")
check(hasattr(lcd, "spectrum_publish"), "transactional spectrum_publish API missing")
check(app.be.running, "RX must be running")
check(app._nco_enabled(), "NCO block must be ON")
check(not app._modal and not lcd.spectrum_pause(),
      "HOME must be active with native drawing unpaused")
check(int(lcd.spectrum()[0]) in (0, 1),
      "left graph must be SPEC or WF, not OFF")
check(not app._inj_on, "TESTER must be OFF for the real receiver tap check")


# Let an ordinary startup route/CAL transaction settle before attributing any token
# change to navigation.  This is a precondition, not an invitation to run I2C here.
wait_until(lambda: not app._hw_pending and
           not getattr(app, "_axis_pending_token", 0),
           SETTLE_TIMEOUT_MS,
           "pre-existing hardware/generation request did not settle")
confirmed_lo = int(app._lo_hz)


# Capability gate: old firmware accepts only spectrum_center(hz).  The new getter is
# deliberately mandatory; do not silently degrade this test to the obsolete model.
try:
    initial_generation = int(lcd.spectrum_center())
except (TypeError, AttributeError) as exc:
    raise AssertionError(
        "firmware lacks spectrum_center() signed generation getter: %r" % (exc,))


# Re-announce the already-confirmed physical LO once.  Pause only the consumer while
# issuing it, making the pending observation deterministic even if an LVGL tick was
# already scheduled.  Resume immediately so a complete matching FFT can be drawn.
lcd.spectrum_pause(True)
try:
    token = int(lcd.spectrum_center(confirmed_lo))
    check(token > 0, "spectrum_center(vfo_hz) did not return a positive token")
    check(token != abs(initial_generation), "fresh VFO epoch reused the old token")
    pending = int(lcd.spectrum_center())
    check(pending == -token,
          "generation was not pending immediately: got %d expected %d" %
          (pending, -token))
finally:
    lcd.spectrum_pause(False)


def matching_generation():
    state = int(lcd.spectrum_center())
    if state == token:
        return state
    check(state in (-token, token),
          "foreign generation observed while waiting: %d token %d" % (state, token))
    return 0


wait_until(matching_generation, GENERATION_TIMEOUT_MS,
           "no complete matching FFT staged generation %d" % token)
check(lcd.spectrum_publish(token),
      "matching staged FFT rejected explicit publish for generation %d" % token)
sdr_single.lv.refr_now(app._dd)
check(int(lcd.spectrum_center()) == token,
      "generation was not committed by the label/data render transaction")
check(int(lcd.spectrum_publish()) == token,
      "publish API does not report the committed generation")
check(not lcd.spectrum_publish(token),
      "generation stayed pending/armed after RENDER_READY")


def label_text(name):
    return app.ui.get(name).get_text()


axis_before = (label_text("spec-lo"), label_text("spec-hi"))
original = int(app.p["f"])
lo_before = int(app._lo_hz)
fine_before = int(app.be.fine_hz)


# Prefer a move towards the physical LO, but admit only a target which is inside
# the global RF limits and remains inside the mode's NCO-only window.  At F_MAX,
# for example, AM/FM normally have a negative fine offset: following its sign
# blindly would choose a clamped no-op.  Smaller fallbacks keep this probe
# reversible even when the legal side is close to the mode-specific NCO edge.
preferred_sign = -1 if fine_before > 0 else 1
delta = None
for magnitude in (1000, 500, 100, 10, 1):
    for direction in (preferred_sign, -preferred_sign):
        candidate = original + direction * magnitude
        if (sdr_single.F_MIN <= candidate <= sdr_single.F_MAX and
                not app._nco_recenter_due(candidate, app.p["m"])):
            delta = candidate - original
            break
    if delta is not None:
        break
check(delta is not None, "no legal reversible NCO-only move from current station")

moved = False
try:
    check(app._move_live_frequency(delta), "small NCO-only move rejected")
    moved = True
    check(app.p["f"] == app._lo_hz + app.be.fine_hz,
          "selected/LO/NCO state mismatch after small move")
    check(int(app._lo_hz) == lo_before, "small NCO move changed physical LO")
    check(app._lo_pending_hz is None and app._station_pending_hz is None,
          "small NCO move queued an unnecessary VFO rollover")
    check(int(lcd.spectrum_center()) == token,
          "NCO-only move started/rebased a spectrum generation")
    check((label_text("spec-lo"), label_text("spec-hi")) == axis_before,
          "NCO-only move changed the VFO-anchored frequency axis")

    # Let both native consumers accept several frames: a delayed accidental rebase
    # must not appear after the immediate checks above.
    time.sleep_ms(250)
    check(int(lcd.spectrum_center()) == token,
          "delayed spectrum generation change after NCO-only move")
    check(int(app._lo_hz) == lo_before, "delayed physical-LO move after small NCO step")
    check((label_text("spec-lo"), label_text("spec-hi")) == axis_before,
          "frequency axis drifted after native frames")
finally:
    if moved and int(app.p["f"]) != original:
        restored = app._move_live_frequency(original - int(app.p["f"]))
        check(restored, "failed to restore original NCO selection")

check(int(app.p["f"]) == original, "station was not restored")
check(int(app._lo_hz) == lo_before, "restore changed physical LO")
check(int(app.be.fine_hz) == fine_before, "NCO was not restored")
check(int(lcd.spectrum_center()) == token, "restore rebased spectrum generation")


# Exercise the real HOME histogram action, not only its isolated coordinate math.
# Pick a point 1 kHz to one side of the physical-LO centre, then restore the exact
# previous selection.  Neither leg may move Si5351, rebase the FFT, or relabel X.
tap_original = int(app.p["f"])
tap_lo = int(app._lo_hz)
tap_axis = int(app._axis_hz)
tap_generation = int(lcd.spectrum_center())
tap_labels = (label_text("spec-lo"), label_text("spec-hi"))
tap_fine = int(app.be.fine_hz)
tap_step_before = int(app.p["s"])
tap_target = None
for magnitude in range(1000, 12000, 1000):
    for tap_offset in (magnitude, -magnitude):
        candidate = tap_axis + tap_offset
        if (sdr_single.F_MIN <= candidate <= sdr_single.F_MAX and
                candidate != tap_original):
            tap_target = candidate
            break
    if tap_target is not None:
        break
check(tap_target is not None, "no in-bounds 1-kHz tap point in the visible span")
tap_frac = 0.5 + ((tap_target - tap_axis) /
                  float(sdr_single.IQ_RATE // 2))
try:
    # Make the coordinate check independent of the operator's current 1/10/100-kHz
    # HOME step.  This direct temporary assignment is restored before the debounced
    # parameter save can run and does not alter the visible step control.
    app.p["s"] = 1000
    app.spec_jump(tap_frac)
    check(int(app.p["f"]) == tap_target,
          "real histogram tap did not select its physical-axis point")
    check(int(app._lo_hz) == tap_lo,
          "real histogram tap changed the physical LO")
    check(int(lcd.spectrum_center()) == tap_generation,
          "real histogram tap rebased the spectrum generation")
    check((label_text("spec-lo"), label_text("spec-hi")) == tap_labels,
          "real histogram tap changed the physical frequency axis")
finally:
    try:
        if int(app.p["f"]) != tap_original:
            check(app._select_live_nco_only(tap_original),
                  "failed to restore selection after real histogram tap")
    finally:
        app.p["s"] = tap_step_before

check(int(app.p["f"]) == tap_original,
      "real histogram tap did not restore the selected station")
check(int(app._lo_hz) == tap_lo,
      "real histogram tap restore changed the physical LO")
check(int(app.be.fine_hz) == tap_fine,
      "real histogram tap restore did not restore the NCO")
check(int(lcd.spectrum_center()) == tap_generation,
      "real histogram tap restore rebased the spectrum generation")


# Run spec_jump on a tiny fake app.  Two different starting NCO positions must land
# on the same absolute point of the physical-LO axis for the same tap coordinate.
class _TapBackend:
    running = True


class _TapProbe:
    def __init__(self, selected):
        self._lo_hz = 3_500_000
        self._axis_hz = self._lo_hz
        self._axis_pending_token = 0
        self._station_pending_hz = None
        self.p = {"f": selected, "s": 1000}
        self.be = _TapBackend()
        self._inj_on = False
        self.targets = []

    def _select_live_nco_only(self, wanted):
        self.targets.append(int(wanted))
        self.p["f"] = int(wanted)
        return True


for frac, expected in ((0.0, 3_488_000),
                       (0.5, 3_500_000),
                       (0.75, 3_506_000),
                       (1.0, 3_512_000)):
    tap_a = _TapProbe(3_497_000)
    tap_b = _TapProbe(3_503_000)
    type(app).spec_jump(tap_a, frac)
    type(app).spec_jump(tap_b, frac)
    check(tap_a.p["f"] == expected and tap_b.p["f"] == expected,
          "spectrum X %.2f did not select absolute axis frequency %d" %
          (frac, expected))
    check(tap_a.targets == [expected] and tap_b.targets == [expected],
          "spec_jump X %.2f is relative to selected/NCO centre" % frac)


# Pure control test for rollover policy.  It executes the real method against a fake
# backend, proving that an inner-window move uses only NCO while the exact mode edge
# queues VFO work without first programming an out-of-policy NCO.
class _MoveBackend:
    def __init__(self):
        self.fine_hz = 0
        self.calls = []

    def set_fine(self, value):
        self.calls.append(int(value))
        self.fine_hz = int(value)
        return self.fine_hz


class _MoveProbe:
    def __init__(self, mode):
        self._lo_hz = 3_500_000
        self._station_pending_hz = None
        self._axis_pending_token = 0
        self._axis_pending_mode = None
        self.p = {"f": self._lo_hz, "m": mode}
        self._requested_hz = self._lo_hz
        self.be = _MoveBackend()
        self.queued = []

    def _nco_enabled(self):
        return True

    def _nco_recenter_due(self, station_hz, mode=None):
        return type(app)._nco_recenter_due(self, station_hz, mode)

    def _queue_station_recenter(self, hz, vfo=None, mode=None):
        self.queued.append((int(hz), vfo, mode))

    def _queue_nco_recenter(self):
        self.queued.append(("nco", None, None))

    def _cancel_frequency_pending(self):
        pass

    def _commit_current_frequency(self, hz):
        self.p["f"] = int(hz)

    def _accept_live_nco_selection(self, selected, vfo=None, mode=None):
        self._requested_hz = int(selected)
        self.p["f"] = int(selected)


for mode, edge in (("USB", sdr_single.NCO_RECENTER_HZ), ("FM", 7000)):
    probe = _MoveProbe(mode)
    check(type(app)._move_live_frequency(probe, 1000),
          "%s inner-window move rejected" % mode)
    check(probe.be.calls == [1000] and probe.queued == [],
          "%s inner-window move did not stay NCO-only" % mode)

    probe = _MoveProbe(mode)
    check(type(app)._move_live_frequency(probe, edge),
          "%s edge rollover rejected" % mode)
    check(probe.be.calls == [], "%s edge programmed NCO before VFO rollover" % mode)
    check(probe.queued == [(3_500_000 + edge, None, None)],
          "%s exact edge did not queue one VFO rollover" % mode)


print("NAV_HIL PASS", "token", token, "LO", lo_before,
      "NCO", fine_before, "axis", axis_before, "spectrum", lcd.spectrum())
print("NAV_HIL VISUAL CHECK REQUIRED: constant-tone peak/history fixed; marker moves")
