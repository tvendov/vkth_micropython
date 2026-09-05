"""Host-side source contract for VFO-anchored spectrum navigation.

This test deliberately reads source instead of importing ``sdr_single``: the latter
requires the MicroPython/LVGL target runtime.  It protects the architectural rules
which are otherwise difficult to infer from a short visual HIL run:

* an NCO move changes the listening marker, never the measured FFT columns/history;
* the horizontal axis and spectrum taps are anchored to the confirmed physical LO;
* only a successful physical-LO transition starts a new spectrum generation; and
* a partial/stale FFT cannot be published under the new generation.

Run from any directory with CPython::

    python ports/renesas-ra/boards/VK_RA6M3/examples/sdr_navigation_source_contract.py
"""

from pathlib import Path
import os
import re


HERE = Path(__file__).resolve().parent
PY_PATH = HERE / "sdr_single.py"
if PY_PATH.is_file():
    BOARD = HERE.parent
    PORT = BOARD.parents[1]
else:
    PY_PATH = HERE.parent / "sdr_single.py"  # external project/tests layout
    _port_candidates = []
    _port_env = os.environ.get("RENESAS_RA_PORT")
    if _port_env:
        _port_candidates.append(Path(_port_env))
    _port_candidates.append(Path(
        r"C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra"))
    PORT = next((p for p in _port_candidates
                 if (p / "ra" / "ra_iq_adc.c").is_file()), None)
    if PORT is None:
        raise FileNotFoundError(
            "renesas-ra port not found; set RENESAS_RA_PORT")
    BOARD = PORT / "boards" / "VK_RA6M3"

LCD_PATH = BOARD / "machine_lcd.c"
IQ_PATH = PORT / "ra" / "ra_iq_adc.c"
IQ_H_PATH = PORT / "ra" / "ra_iq_adc.h"


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def read(path):
    return path.read_text(encoding="utf-8")


def python_method(source, name):
    """Return one four-space-indented class method, including its signature."""
    match = re.search(r"^    def " + re.escape(name) + r"\s*\(.*?^    (?=def |@|# ----)",
                      source, re.MULTILINE | re.DOTALL)
    if match is None:
        # The final method in a class may run directly into the next top-level item.
        match = re.search(r"^    def " + re.escape(name) + r"\s*\(.*?(?=^\S|\Z)",
                          source, re.MULTILINE | re.DOTALL)
    check(match is not None, "missing Python method %s" % name)
    return match.group(0)


def c_function(source, name):
    """Return a C function definition using a small brace-aware scanner."""
    pattern = re.compile(r"^[^\n;]*\b" + re.escape(name) +
                         r"\s*\([^;]*?\)\s*\{", re.MULTILINE | re.DOTALL)
    match = pattern.search(source)
    check(match is not None, "missing C function %s" % name)
    depth = 0
    started = False
    for index in range(match.start(), len(source)):
        char = source[index]
        if char == "{":
            depth += 1
            started = True
        elif char == "}":
            depth -= 1
            if started and depth == 0:
                return source[match.start():index + 1]
    raise AssertionError("unterminated C function %s" % name)


py = read(PY_PATH)
lcd = read(LCD_PATH)
iq = read(IQ_PATH)
iq_h = read(IQ_H_PATH)


# The tap coordinate belongs to the fixed physical-LO axis.  Repeated taps must
# select the same absolute frequency, irrespective of the currently selected NCO.
spec_jump = python_method(py, "spec_jump")
check("self._axis_hz" in spec_jump,
      "spec_jump must derive its target from the published physical-LO axis")
check(re.search(r"\bwanted\b", spec_jump),
      "spec_jump needs an explicit absolute target on the LO-anchored axis")
check(re.search(r"_select_live_nco_only\s*\(\s*wanted\s*\)", spec_jump),
      "HOME spectrum tap must pass its absolute-axis target to NCO-only tuning")
check("_queue_station_recenter" not in spec_jump and "hw_tune" not in spec_jump,
      "a tap inside the captured panorama must not directly retune the VFO")
check("self.tune(" not in spec_jump,
      "a stopped-RX histogram tap still becomes a deferred VFO tune")
check("NATIVE_GRAPH_HEADER_H = 18" in py,
      "native graph header/tuning boundary is not explicit")
check(re.search(r"if local_y < NATIVE_GRAPH_HEADER_H:\s+"
                r"self\.toggle_spectrum_view\(\)\s+else:\s+"
                r"self\.spec_jump\(local_x / 255\.0\)", py),
      "the live histogram is still consumed by the view-cycle gesture")


# These paths can run for an NCO-only change.  They must not rebase/pan measured
# spectrum data, directly or through the shared frequency-commit helper.
for method_name in ("_commit_current_frequency", "_tester_nco_set",
                    "_tester_nco_tune", "_set_live_nco_absolute",
                    "_accept_live_nco_selection", "_select_live_nco_only",
                    "_move_live_frequency", "fine", "spec_jump"):
    body = python_method(py, method_name)
    check("_publish_spectrum_center" not in body,
          "%s still rebases measured data during an NCO-only path" % method_name)
    check("spectrum_rebase" not in body,
          "%s starts a VFO generation from an NCO-only path" % method_name)
    check("_request_spectrum_generation" not in body,
          "%s requests a VFO generation from an NCO-only path" % method_name)


# The displayed RF scale is the physical capture window, not the listened/NCO centre.
update_freq = python_method(py, "update_freq")
check(re.search(r"axis\w*\s*=\s*(?:int\s*\(\s*)?self\._axis_hz", update_freq),
      "update_freq needs the last matching-frame physical-LO axis")
check(re.search(r'ui\.get\("spec-lo"\).*axis', update_freq),
      "left spectrum label is not based on the physical-LO axis")
check(re.search(r'ui\.get\("spec-hi"\).*axis', update_freq),
      "right spectrum label is not based on the physical-LO axis")


# Raw FFT bins are reduced at fixed X coordinates.  marker_bins may be returned as
# metadata, but it must never participate in the source-bin lookup.
reduce_frame = c_function(iq, "ra_iq_adc_spectrum_reduce")
check(not re.search(r"\+\s*(?:shift_bins|marker_bins)\b", reduce_frame),
      "fallback reducer still translates measured bins with the NCO marker")
check("int32_t shifted" not in reduce_frame,
      "fallback reducer still builds a shifted data coordinate")

column_level = c_function(lcd, "lcd_panorama_column_level")
check("shift_bins" not in column_level and "marker_bins" not in column_level,
      "native spectrum/waterfall column lookup still depends on the NCO marker")

waterfall_write = c_function(lcd, "lcd_waterfall_write")
check("memmove(" not in waterfall_write,
      "retained waterfall history is still horizontally panned")
check("s_lcd_waterfall_pan_pending_hz" not in lcd,
      "selected-frequency waterfall pan state still exists")


# Both framebuffer and LVGL fallback drawing must obtain X from marker metadata,
# rather than painting an unconditional centre cursor.
cursor_x = c_function(lcd, "lcd_tune_cursor_x")
check("s_lcd_spectrum_marker_px" in cursor_x,
      "cursor X does not consume the live NCO marker offset")
check(re.search(r"LCD_WATERFALL_PIXELS\s*/\s*2U\)\s*\+\s*\n?\s*"
                r"s_lcd_spectrum_marker_px", cursor_x),
      "cursor X remains hard-coded at the plot centre")

cursor = c_function(lcd, "lcd_tune_cursor_draw_fb")
check("lcd_tune_cursor_x" in cursor,
      "framebuffer cursor bypasses the shared dynamic-marker coordinate")
check("LCD_TUNE_CURSOR_PX" in cursor and
      re.search(r"cursor_top\s*\+\s*2.*?plot->y2", cursor, re.DOTALL),
      "framebuffer cursor does not span the live spectrum height")
check("cursor_x - 1" in cursor and "cursor_x + 1" in cursor and
      "#define LCD_TUNE_CURSOR_PX      (9U)" in lcd,
      "framebuffer cursor is not the high-contrast three-pixel/9-pixel marker")

event_draw = c_function(lcd, "lcd_lv_spectrum_event_cb")
check("lcd_tune_cursor_x" in event_draw,
      "LVGL fallback cursor bypasses the shared dynamic-marker coordinate")
check("cursor_top + 2" in event_draw and ".y2 = plot_area.y2" in event_draw,
      "LVGL fallback does not preserve the full-height marker appearance")
check(".x1 = cursor_x - 1" in event_draw and ".x2 = cursor_x + 1" in event_draw,
      "LVGL fallback does not preserve the three-pixel marker stem")


# A physical-LO rebase is producer/consumer transactional: request a generation,
# adopt it only at a DSP block boundary, discard the partial FFT and the possibly
# straddling block, stamp completed halves, and reject a stale half before copying.
rebase = c_function(iq, "ra_iq_adc_spectrum_rebase")
check("s_spec_generation_requested" in rebase and "+ 1U" in rebase,
      "spectrum_rebase does not advance the requested VFO generation")
check("s_spec_ready = -1" in rebase,
      "spectrum_rebase does not invalidate a previously completed stale frame")

dsp = c_function(iq, "ra_iq_dsp_process")
for token in ("s_spec_generation_requested", "s_spec_generation_active",
              "ra_iq_spec_discard_partial", "skip_spec_generation_block"):
    check(token in dsp, "DSP generation transition is missing %s" % token)

capture = c_function(iq, "ra_iq_spec_capture")
check("s_spec_generation[h] = s_spec_generation_active" in capture,
      "completed FFT halves are not stamped with their VFO generation")

claim = c_function(iq, "ra_iq_adc_spectrum_claim_snapshot")
check(re.search(r"generation\s*!=\s*s_spec_generation_requested", claim),
      "foreground claim does not reject stale-generation FFT halves")
check(claim.find("generation != s_spec_generation_requested") < claim.find("memcpy("),
      "stale-generation check must happen before the FFT copy")

frame = c_function(iq, "ra_iq_adc_spectrum_frame")
check("uint32_t *generation" in frame and "*generation =" in frame,
      "native frame API does not publish the matching VFO generation")
check("ra_iq_adc_spectrum_rebase" in iq_h and "uint32_t *generation" in iq_h,
      "generation/rebase contract is absent from the public IQADC header")


# LCD has a second transactional gate.  It accepts only the exact pending
# generation, reduces it to a stable compact frame without touching visible state,
# then commits it only in the label render's RENDER_READY transaction.
generation_prepare = c_function(lcd, "lcd_spectrum_generation_prepare")
check("generation != s_lcd_spectrum_generation_pending" in generation_prepare and
      "return false" in generation_prepare,
      "LCD does not reject a non-matching pending generation")
check(generation_prepare.find("generation != s_lcd_spectrum_generation_pending") <
      generation_prepare.find("*commit_pending = true"),
      "LCD marks a generation committable before checking its token")
check("!s_lcd_spectrum_vfo_valid" in generation_prepare,
      "LCD accepts FFT pixels before any physical-LO axis is confirmed")
check("s_lcd_waterfall_reset" not in generation_prepare and
      "s_lcd_spectrum_valid" not in generation_prepare,
      "generation prepare mutates the visible old panorama before publish")

generation_commit = c_function(lcd, "lcd_spectrum_generation_commit")
check("generation == s_lcd_spectrum_generation_pending" in generation_commit and
      "s_lcd_spectrum_generation_committed = generation" in generation_commit,
      "LCD generation commit is not bound to the exact pending token")

lcd_center = c_function(lcd, "lcd_spectrum_center")
for token_text in ("-((int64_t)s_lcd_spectrum_generation_pending)",
                   "s_lcd_spectrum_generation_ready",
                   "s_lcd_spectrum_generation_committed",
                   "ra_iq_adc_spectrum_rebase",
                   "s_lcd_spectrum_generation_pending = generation"):
    check(token_text in lcd_center,
          "signed spectrum_center generation API is missing %s" % token_text)
check("vfo_hz == s_lcd_spectrum_vfo_pending_hz" not in lcd_center,
      "a confirmed physical retune can incorrectly reuse a pending generation")

stage_generation = c_function(lcd, "lcd_spectrum_stage_generation")
for token_text in ("lcd_panorama_levels_from_frame",
                   "s_lcd_spectrum_pending_level",
                   "s_lcd_spectrum_pending_marker_px",
                   "s_lcd_spectrum_generation_ready = generation"):
    check(token_text in stage_generation,
          "stable staged generation is missing %s" % token_text)
check("lcd_spectrum_generation_commit" not in stage_generation and
      "lcd_spectrum_write" not in stage_generation and
      "lcd_waterfall_write" not in stage_generation,
      "staging a matching frame changes visible state before Python labels")

arm_generation = c_function(lcd, "lcd_spectrum_publish_generation")
arm_pos = arm_generation.find("s_lcd_spectrum_generation_armed = generation")
invalidate_pos = arm_generation.find("lv_obj_invalidate_area")
check(0 <= arm_pos < invalidate_pos,
      "explicit publish does not arm an exact frame before requesting label render")
check("s_lcd_spectrum_marker_px = s_lcd_spectrum_pending_marker_px" not in
      arm_generation and "lcd_spectrum_state_from_levels" not in arm_generation,
      "arming a visible staged generation mutates graph state before RENDER_READY")

abort_generation = c_function(lcd, "lcd_spectrum_abort_generation")
check("s_lcd_spectrum_generation_armed = 0U" in abort_generation,
      "an interrupted Python label transaction cannot disarm its C frame")

render_publish = c_function(lcd, "lcd_spectrum_publish_render_ready")
draw_positions = [pos for pos in (
    render_publish.find("lcd_spectrum_draw_direct"),
    render_publish.find("lcd_waterfall_draw_levels")) if pos >= 0]
render_commit_pos = render_publish.rfind("lcd_spectrum_generation_commit")
check(len(draw_positions) == 2 and max(draw_positions) < render_commit_pos,
      "RENDER_READY can commit the axis before drawing its matching staged data")
check("s_lcd_spectrum_generation_armed" in render_publish,
      "RENDER_READY publish is not tied to the explicitly armed token")
check("s_lcd_spectrum_marker_px = s_lcd_spectrum_pending_marker_px" in
      render_publish and "lcd_spectrum_state_from_levels" in render_publish,
      "staged graph state is not installed inside its publishing render")

render_ready_cb = c_function(lcd, "lcd_lv_render_ready_cb")
check(render_ready_cb.find("lcd_spectrum_publish_render_ready") <
      render_ready_cb.find("s_lcd_render_in_progress = 0U"),
      "staged graph publish runs outside the completing LVGL transaction")

timer = c_function(lcd, "lcd_lv_spectrum_timer_cb")
prepare_pos = timer.find("lcd_spectrum_generation_prepare")
stage_pos = timer.find("lcd_spectrum_stage_generation", prepare_pos)
check(0 <= prepare_pos < stage_pos,
      "timer does not stage the exact checked generation")
check("lcd_spectrum_generation_commit" not in timer,
      "timer still exposes a new generation before Python publishes its labels")

# With SPEC/WF deliberately OFF there are no pixels to draw, but the generation
# barrier must still consume one complete matching FFT so Python can publish the
# confirmed axis.  It must then turn the hidden producer back off.
hidden_generation = c_function(lcd, "lcd_spectrum_complete_hidden_generation")
for token_text in ("s_lcd_spectrum_vfo_pending", "ra_iq_adc_spectrum_frame",
                   "lcd_spectrum_generation_prepare",
                   "lcd_spectrum_stage_generation"):
    check(token_text in hidden_generation,
          "hidden-view generation completion is missing %s" % token_text)
check("s_lcd_spectrum_view == LCD_VIEW_OFF" in timer and
      "lcd_spectrum_complete_hidden_generation(now)" in timer,
      "timer cannot complete a physical-LO generation while SPEC/WF is OFF")


# Python may notify the producer only after hw_tune(target) has returned success.
apply_pending = python_method(py, "_apply_hw_pending")
rebase_pos = apply_pending.find("_request_spectrum_generation")
check(rebase_pos >= 0, "successful VFO transition does not request a spectrum generation")
success_guard = apply_pending.find("if not self.hw_tune")
check(success_guard >= 0 and success_guard < rebase_pos,
      "spectrum generation is requested before confirmed Si5351 success")

poll_generation = python_method(py, "_poll_axis_generation")
check("state != token" in poll_generation,
      "axis publisher does not require the exact matching positive token")
arm_pos = poll_generation.find("publish(token)")
publish_pos = poll_generation.find("_finish_axis_publish")
refresh_pos = poll_generation.find("lv.refr_now", publish_pos)
commit_check_pos = poll_generation.find("int(publish()) != token", refresh_pos)
clear_pos = poll_generation.find("_clear_axis_pending", commit_check_pos)
match_pos = poll_generation.find("state != token")
check(0 <= match_pos < arm_pos < publish_pos < refresh_pos < commit_check_pos <
      clear_pos,
      "axis/data transaction is not match -> arm -> labels -> render -> confirm -> clear")
check("self._spectrum_publish_fn" in poll_generation,
      "Python does not require the explicit staged-frame publish API")
check("publish(-token)" in poll_generation,
      "Python exception path cannot abort an armed C generation")
check("_axis_publish_snapshot" in poll_generation and
      "_restore_axis_publish" in poll_generation,
      "Python exception path cannot roll back partially changed axis/UI state")
restore_generation = python_method(py, "_restore_axis_publish")
check("if not self.be.set_mode" in restore_generation and
      "if not self.be.set_bandwidth" in restore_generation,
      "axis rollback ignores a rejected backend mode/bandwidth restore")
check("axis rollback incomplete" in restore_generation and
      "raise RuntimeError" in restore_generation,
      "partial axis/UI rollback is still silently swallowed")
check("abort_error" in poll_generation and "restore_error" in poll_generation and
      "spectrum publish failed=" in poll_generation,
      "publisher does not preserve the original and rollback failures together")


# A superseding physical recenter must derive its LO from the unpublished VFO/mode
# identity already attached to the in-flight generation, not from stale p[] state.
queue_recenter = python_method(py, "_queue_station_recenter")
queued_vfo_pos = queue_recenter.find("vfo = self._station_pending_vfo")
queued_mode_pos = queue_recenter.find("mode = self._station_pending_mode")
pending_vfo_pos = queue_recenter.find("vfo = self._axis_pending_vfo")
pending_mode_pos = queue_recenter.find("mode = self._axis_pending_mode")
selected_mode_pos = queue_recenter.find("selected_mode =")
lo_target_pos = queue_recenter.find("self._lo_pending_hz =")
check(0 <= queued_vfo_pos < pending_vfo_pos < selected_mode_pos < lo_target_pos,
      "superseding recenter loses a newer worker-owned VFO identity")
check(0 <= queued_mode_pos < pending_mode_pos < selected_mode_pos < lo_target_pos,
      "superseding recenter loses a newer worker-owned mode policy")
check(0 <= pending_vfo_pos < selected_mode_pos < lo_target_pos,
      "superseding recenter derives the LO before preserving pending VFO identity")
check(0 <= pending_mode_pos < selected_mode_pos < lo_target_pos,
      "superseding recenter derives the LO before preserving pending mode policy")

queue_nco_recenter = python_method(py, "_queue_nco_recenter")
check("self._queue_station_recenter(self._requested_hz)" in queue_nco_recenter,
      "NCO edge recenter bypasses the common pending-identity/low-IF path")
check("_normal_lo_hz" not in queue_nco_recenter,
      "NCO edge recenter still derives its LO before pending identity is resolved")


# A nearby VFO switch may stay NCO-only only when its physical low-IF policy is
# identical.  Otherwise AM/FM/zero-IF mode changes require a real Si5351 edge.
switch_vfo = python_method(py, "switch_vfo")
effective_vfo_pos = switch_vfo.find("current_vfo = self._axis_pending_vfo")
effective_mode_pos = switch_vfo.find("current_mode = self._axis_pending_mode")
effective_return_pos = switch_vfo.find("if i == current_vfo:")
policy_pos = switch_vfo.find("same_low_if =")
policy_guard_pos = switch_vfo.find("if (same_low_if and")
route_guard_pos = switch_vfo.find('p["rt"][i] == p["rt"][current_vfo]')
nco_switch_pos = switch_vfo.find("_set_live_nco_absolute")
check(0 <= effective_vfo_pos < effective_return_pos and
      0 <= effective_mode_pos < effective_return_pos,
      "second VFO switch ignores the in-flight physical VFO/mode identity")
check("if i == p[\"act\"]:" not in switch_vfo,
      "published VFO still blocks superseding an in-flight VFO selection")
check(0 <= policy_pos < policy_guard_pos < nco_switch_pos,
      "nearby VFO fast path does not reject a low-IF policy transition")
check("self._station_pending_hz is None" in switch_vfo[:nco_switch_pos],
      "nearby VFO fast path can cancel an unapplied physical policy change")
check(0 <= route_guard_pos < nco_switch_pos,
      "nearby VFO fast path compares routing against stale published VFO state")

move_live = python_method(py, "_move_live_frequency")
supersede_pos = move_live.find("self._queue_station_recenter(wanted)")
direct_nco_pos = move_live.find("self.be.set_fine")
check("self._station_pending_hz is not None" in move_live and
      0 <= supersede_pos < direct_nco_pos,
      "live tuning can cancel an unapplied physical recenter with a direct NCO write")
check("live_mode = self._axis_pending_mode" in move_live,
      "live tuning ignores the in-flight mode's safe NCO edge")
edge_calls = re.findall(r"_nco_recenter_due\((.*?)\)", move_live)
check(len(edge_calls) == 3,
      "live tuning NCO-edge paths changed without updating this contract")
for call in edge_calls:
    check("live_mode" in call,
          "live tuning applies an NCO edge using stale published mode")


# Modal navigation and the short retune hold share one pause state, but only a
# real modal asks new firmware to rebuild the native surface on resume.  Keep the
# one-argument fallback for already-flashed firmware.
sync_pause = python_method(py, "_sync_spectrum_pause")
pause_union_pos = sync_pause.find(
    "paused = self._modal or self._spectrum_transition_hold")
new_pause_pos = sync_pause.find("spectrum_pause(paused, self._modal)")
type_error_pos = sync_pause.find("except TypeError")
old_pause_pos = sync_pause.find("spectrum_pause(paused)", new_pause_pos + 1)
check(0 <= pause_union_pos < new_pause_pos < type_error_pos < old_pause_pos,
      "spectrum pause does not distinguish modal rebuild from a retune hold")


print("SDR NAVIGATION SOURCE CONTRACT PASS")
