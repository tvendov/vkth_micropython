"""Non-destructive inline gain-panel construction and visibility probe."""

import gc

import lvgl as lv
import sdr_single


app = sdr_single._KEEP.get("app")
if app is None:
    app = sdr_single.start()

if "gains_panel" in sdr_single._KEEP:
    app.close_gains_panel()

gc.collect()
print("GAINS_HIL START", gc.mem_free())
print("GAINS_HIL EVENT_API", hasattr(lv, "event_send"),
      hasattr(lv, "obj_send_event"),
      hasattr(app.ui.get("vol-label"), "send_event"))

try:
    # Exercise the exact child-label event target used by a finger on the text.
    app.ui.get("vol-label").send_event(lv.EVENT.CLICKED, None)
    panel = app.ui.get("gains-inline")
    assert panel is not None, "inline panel was not built"
    assert "gains_panel" in sdr_single._KEEP, "open flag missing"
    assert not panel.has_flag(lv.obj.FLAG.HIDDEN), "inline panel stayed hidden"
    assert app.ui.get("frequency-display").has_flag(lv.obj.FLAG.HIDDEN)
    assert app.ui.get("spectrum-area").has_flag(lv.obj.FLAG.HIDDEN)
    assert len(app._gain_widgets) == 3, "expected AF/AGC/SQL live controls"
    print("GAINS_HIL OPEN PASS", gc.mem_free(), len(app._gain_widgets))

    # Exercise the one shared pin checkbox without leaving a changed preference.
    old_active = app._active_gain
    app._gain_candidate = "SQL"
    app._paint_gain_pin()
    app.ui.get("gain-pin").send_event(lv.EVENT.CLICKED, None)
    assert app._active_gain == "SQL", "shared pin did not select candidate"
    app._active_gain = old_active
    app._gain_candidate = old_active
    app._paint_gain_pin()
    print("GAINS_HIL PIN PASS", old_active)

    # The same visible VOL label closes the inline view again.
    app.ui.get("vol-label").send_event(lv.EVENT.CLICKED, None)
    assert "gains_panel" not in sdr_single._KEEP, "VOL label did not close panel"
finally:
    if "gains_panel" in sdr_single._KEEP:
        app.close_gains_panel()

assert not app.ui.get("frequency-display").has_flag(lv.obj.FLAG.HIDDEN)
assert not app.ui.get("spectrum-area").has_flag(lv.obj.FLAG.HIDDEN)
print("GAINS_HIL CLOSE PASS", gc.mem_free())
