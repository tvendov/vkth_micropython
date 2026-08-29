"""Inspect and safely restore the native right-panel oscilloscope state."""

import time

import sdr_single


app = sdr_single._KEEP.get("app")
if app is None:
    app = sdr_single.start()

lcd = app._spec_lcd
assert app._spec_native and lcd is not None

native_view = lcd.scope_view()
paused = lcd.spectrum_pause()
iq_scope = app.be.iq.scope() if app.be.iq is not None else -1
playing = app.be.dac.playing() if app.be.dac is not None else False
print("SCOPE_HIL BEFORE", app.be.running, playing, app._modal,
      native_view, app._scope_view, paused, app.be.scope_stage, iq_scope,
      tuple(k for k in ("gains_panel", "settings", "pick_menu", "route_menu")
            if k in sdr_single._KEEP))
print("SCOPE_HIL SIGNAL", app._inj_on, app._inj_source, app._inj_ampl,
      app.p["m"], app.p["v"], app._squelch, app.be.poll_status())

# A transient overlay must never leave native capture paused.  Restore the normal
# time-domain view without changing the selected DSP tap or the audio routing.
if "gains_panel" in sdr_single._KEEP:
    app.close_gains_panel()
if app._modal or lcd.spectrum_pause():
    app._set_modal(False)
if lcd.scope_view() != 0:
    assert lcd.scope_view(0)
app._scope_view = 0
app.ui.get("scope-view").set_text("TIME")

time.sleep_ms(750)
print("SCOPE_HIL AFTER", app.be.running,
      app.be.dac.playing() if app.be.dac is not None else False,
      app._modal, lcd.scope_view(), app._scope_view, lcd.spectrum_pause(),
      app.be.scope_stage, app.be.iq.scope() if app.be.iq is not None else -1,
      lcd.render_debug())
assert app.be.running
assert app.be.dac is not None and app.be.dac.playing()
assert lcd.scope_view() == 0
assert not lcd.spectrum_pause()
print("SCOPE_HIL PASS")
