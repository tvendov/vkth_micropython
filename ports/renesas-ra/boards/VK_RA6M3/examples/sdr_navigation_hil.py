"""Non-destructive HOME panorama/navigation smoke test for VK_RA6M3.

Run against an already booted ``sdr_single`` app.  The test makes one +1 kHz
and one -1 kHz NCO move, verifies the selected/LO/NCO invariant, and restores
the original station before the deferred parameter save can fire.
"""

import time

import sdr_single


app = sdr_single._KEEP.get("app")
if app is None:
    app = sdr_single.start()

print("NAV_HIL START")
assert app._spec_native, "native spectrum is not attached"
assert app._spec_lcd is not None, "LCD bridge is missing"
assert hasattr(app._spec_lcd, "spectrum_center"), "spectrum_center API missing"
assert app._spec_lcd.spectrum_center(app.p["f"]), "centre baseline rejected"

print("NAV_HIL STATE", app.be.running, app.p["f"], app._lo_hz,
      app.be.fine_hz)

if app.be.running and app._nco_enabled():
    original = app.p["f"]
    lo = app._lo_hz
    fine = app.be.fine_hz
    # Move towards the physical LO so the probe cannot hit the NCO edge and
    # accidentally exercise a real LO recenter.  At exact centre, prefer the
    # direction which remains inside the global tuning limits.
    if fine > 0:
        delta = -1000
    elif fine < 0:
        delta = 1000
    elif original + 1000 <= sdr_single.F_MAX:
        delta = 1000
    else:
        delta = -1000

    # Seed the exact stale-plain-recenter condition found in review.  A real
    # successful NCO move must cancel it when the new offset is below 9 kHz.
    app._lo_pending_hz = original
    app._hw_pending = True
    assert app._move_live_frequency(delta), "forward NCO move rejected"
    moved = app.p["f"]
    assert moved == app._lo_hz + app.be.fine_hz, "forward state mismatch"
    if abs(moved - app._lo_hz) < sdr_single.NCO_RECENTER_HZ:
        assert app._lo_pending_hz is None, "stale recenter survived"

    assert app._move_live_frequency(-delta), "return NCO move rejected"
    assert app.p["f"] == original, "station was not restored"
    assert app._lo_hz == lo, "small navigation changed physical LO"
    assert app.be.fine_hz == fine, "NCO was not restored"
    assert app.p["f"] == app._lo_hz + app.be.fine_hz, "final state mismatch"
    print("NAV_HIL MOVE", original, moved, app.p["f"], "PASS")
else:
    print("NAV_HIL MOVE SKIP RX_OR_NCO_OFF")

# Let one native frame consume any idempotent centre update.  No Si5351 target
# remains from the simulated stale-recenter branch.
time.sleep_ms(150)
print("NAV_HIL PASS", app._spec_lcd.spectrum(), app._spec_lcd.render_debug())
