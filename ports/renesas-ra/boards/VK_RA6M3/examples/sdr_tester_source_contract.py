"""Host-side contract checks for TESTER ownership and HOME navigation.

This deliberately parses source instead of importing ``sdr_single`` because the
application needs MicroPython and LVGL.  It catches regressions before a firmware
build or a board reset.
"""

from pathlib import Path
import os
import re


HERE = Path(__file__).resolve().parent
PY_PATH = HERE / "sdr_single.py"
if not PY_PATH.is_file():
    PY_PATH = HERE.parent / "sdr_single.py"  # external project/tests layout

_port_candidates = [HERE.parents[2]]
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
C_PATH = PORT / "ra" / "ra_iq_adc.c"
MACHINE_C_PATH = PORT / "machine_iq_adc.c"


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def method(source, name):
    match = re.search(r"^    def " + re.escape(name) + r"\s*\(.*?"
                      r"(?=^    (?:def |@|# ----)|^\S|\Z)",
                      source, re.MULTILINE | re.DOTALL)
    check(match is not None, "missing Python method " + name)
    return match.group(0)


def c_function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise AssertionError("unterminated C function " + signature)


source = PY_PATH.read_text(encoding="utf-8")
c_source = C_PATH.read_text(encoding="utf-8")
machine_source = MACHINE_C_PATH.read_text(encoding="utf-8")
backend_source = source[source.index("class Ra6m3Backend") :
                        source.index("class SdrApp")]
app_source = source[source.index("class SdrApp") :]

# A PGA reconstruction that preserves TESTER must never start the fresh DSP on
# real ADC data.  A zero-I/Q source owner is published before IQADC.start(), and
# every failure path drops the persistent TST claim instead of exposing ADC under it.
backend_start = method(backend_source, "start_rx")
check("source_hold=False" in backend_start,
      "backend start has no fail-closed TESTER hold")
check("if self.iq is not None:" in backend_start and
      "IQADC teardown incomplete; reset required" in backend_start and
      backend_start.index("if self.iq is not None:") <
      backend_start.index("self._IQADC("),
      "failed checked teardown does not latch out a later ADC start")
for token in ('self.iq.inject(True, 0, 0, self.iq.INJECT_IQ',
              'self.iq.INJECT_POINT_IN', 'self.iq.INJECT_WAVE_SINE'):
    check(token in backend_start, "TESTER startup hold is missing " + token)
check(backend_start.index("self.iq.inject(True") <
      backend_start.index("self.iq.start()") <
      backend_start.index("self.dac.stream_from(self.iq)"),
      "TESTER hold must precede the first ADC block and DAC stream")

update_rx = method(app_source, "update_rx")
check('label.set_text("TST" if self._inj_on else "RX")' in update_rx,
      "HOME RX button does not expose active TESTER ownership")

close_settings = method(app_source, "close_settings")
for forbidden in ("_inj_on = False", "_iq_file.stop", "inject(False)"):
    check(forbidden not in close_settings,
          "BACK must leave TESTER running: found " + forbidden)

set_mode = method(app_source, "set_mode")
check("not self._inj_on" in set_mode and "_queue_station_recenter" in set_mode,
      "HOME demod comparison can still retune Si5351 under TESTER")

apply_tester = method(app_source, "_apply_tester_source")
for token in ("self._iq_file.start", "fn(True", "FM FILE needs NCO block ON",
              "-FM_LOW_IF_HZ", "self._iq_file_loop"):
    check(token in apply_tester, "TESTER apply contract is missing " + token)
file_branch = apply_tester[apply_tester.index("if self._inj_source:"):
                           apply_tester.index("if fn is None:")]
check("fn(False)" not in file_branch,
      "Python disables GEN before FILE prefill/start")
check("_commit_tester_receiver_mode(receiver_mode)" in file_branch and
      "_commit_tester_receiver_mode(receiver_mode)" in apply_tester[len(file_branch):],
      "explicit GEN/FILE profile does not make HOME mode truthful")

arm_tester = method(app_source, "_arm_tester_source")
tune_pending = method(app_source, "_tester_tune_pending")
check(arm_tester.index("_tester_tune_pending()") <
      arm_tester.index("self._inj_on = True") and
      "self._tester_arm_pending = True" in arm_tester,
      "first TESTER arm does not wait for an outstanding physical tune")
check("_axis_pending_token" not in tune_pending,
      "paused VERIFY renderer can deadlock first TESTER arm on an axis token")
check("_request_tester_generation()" in arm_tester,
      "first TESTER arm has no fresh source-frame barrier")
commit_mode = method(app_source, "_commit_tester_receiver_mode")
check("self._axis_pending_vfo is not None" in commit_mode and
      "self._axis_pending_mode = mode" in commit_mode and
      commit_mode.index("self._axis_pending_mode = mode") <
      commit_mode.index('self.p["vfos"][self.p["act"]][1] = mode'),
      "TEST profile can corrupt the old VFO during a pending VFO publication")

stop_rx = method(app_source, "stop_rx")
check("preserve_tester=False" in stop_rx and
      "self._tester_rearm_pending = resume_tester" in stop_rx and
      "self._tester_rearm_nco" in stop_rx,
      "PGA reconstruction cannot preserve TESTER request/NCO state")
check(stop_rx.index("released = self.be.stop_rx()") <
      stop_rx.index("self._iq_file.stop()"),
      "FILE is detached before ADC acquisition stops")
release_fail = stop_rx[stop_rx.index("if not released:"):
                       stop_rx.index("self._iq_file.stop()")]
for token in ("self._tester_rearm_pending = False",
              "self._tester_rearm_nco = None",
              "self._rf_restart_pending = False",
              "self._hw_config_pending = False",
              "self._cancel_frequency_pending()",
              "self._clear_axis_pending()", "return False"):
    check(token in release_fail,
          "checked teardown failure does not abort safely: " + token)

capture_chain = method(app_source, "_capture_runtime_chain")
restore_chain = method(app_source, "_restore_runtime_chain")
for token in ("block_mask", "audio_filter", "dec_kernel", "hil_kernel",
              "chf_kernel", "mag_kernel"):
    check(token in capture_chain and token in restore_chain,
          "PGA runtime-chain restore is missing " + token)
check("if self.stop_rx(preserve_tester=True):" in source and
      "self.start_rx(runtime_state," in source and
      "source_hold=self._tester_rearm_pending" in source and
      "self._apply_tester_source(False)" in source and
      "self.be.set_fine(rearm_nco)" in source,
      "worker does not restore chain/mode/NCO around PGA reconstruction")
poll = source[source.index("        def sdr_poll(t):"):
              source.index("        self.sdr_timer =", source.index("        def sdr_poll(t):"))]
rearm = poll[poll.index("if self._tester_rearm_pending"):
             poll.index("if self._axis_pending_token")]
check(rearm.index("self.be.set_fine(rearm_nco)") <
      rearm.index("self._apply_tester_source(False)") <
      rearm.index("self._request_tester_generation()"),
      "PGA rearm order must be exact NCO -> source -> fresh frame")
for token in ("self.update_freq()", "self._update_tuning_role()",
              "self._paint_listen_markers()", "self._paint_tester()"):
    check(token in rearm, "PGA rearm success repaint is missing " + token)
start_rx = method(app_source, "start_rx")
check("source_hold=False" in start_rx and
      "self.be.start_rx(source_hold=source_hold)" in start_rx,
      "App cannot carry fail-closed TESTER ownership into backend start")
restore_fail = start_rx[start_rx.index("if not chain_ok:"):]
check("self._inj_on = False" in restore_fail and
      "self._apply_tester_source(False)" in restore_fail and
      "self._paint_tester()" in restore_fail,
      "runtime-chain restore failure leaves TESTER hold/paint ON")
check("if source_hold:" in restore_fail and
      "self._tester_rearm_pending = False" in restore_fail and
      "self._inj_on = False" in restore_fail,
      "backend start failure leaves HOME claiming TST")

restore = method(app_source, "_restore_tester_receiver")
check("self._normal_lo_hz(requested, mode)" in restore and
      "self._queue_station_recenter(requested, mode=mode)" in restore,
      "TESTER OFF does not restore the selected mode's physical low-IF policy")

for profile in ('("FM", "FM", "/flash/iqbank/fm48.sdriq",',
                '("R:FM", "FM", "/flash/iqbank/zfm48.sdriq",'):
    check(profile in source, "missing FILE profile " + profile)
check("available_profiles.append(_profile)" in source and
      "self._IQ_FILE_PRESETS + self._IQ_FILE_REAL_PRESETS" in source,
      "FILE availability remains all-or-nothing")

finish_file = method(app_source, "_finish_iq_file")
check("self._paint_tester()" in finish_file and
      'if "settings" in _KEEP' not in finish_file,
      "FILE EOF cannot restore HOME TST -> RX while VERIFY is closed")

# GEN <-> FILE ownership is committed under one short IRQ mask in both directions.
gen = c_function(c_source, "void ra_iq_adc_set_inject_ex(")
check("ra_iq_adc_file_stop()" not in gen,
      "FILE is stopped before the GEN tuple publication")
for left, right in (("__disable_irq();", "s_file.requested_on = 0U;"),
                    ("s_file.requested_on = 0U;", "s_inject_requested_enable = enable;"),
                    ("s_inject_requested_enable = enable;", "__set_PRIMASK(primask);")):
    check(gen.index(left) < gen.index(right),
          "non-atomic FILE->GEN publication: %s must precede %s" % (left, right))
file_start = c_function(c_source, "bool ra_iq_adc_file_start(void)")
for left, right in (("__disable_irq();", "ra_iq_inject_request_off();"),
                    ("ra_iq_inject_request_off();", "s_file.requested_on = 1U;"),
                    ("s_file.requested_on = 1U;", "__set_PRIMASK(primask);")):
    check(file_start.index(left) < file_start.index(right),
          "non-atomic GEN->FILE publication: %s must precede %s" % (left, right))

# A source owner/boundary change discards all old signal history, while keeping
# operator configuration such as manual gain and volume intact.
reset = c_function(c_source, "static void ra_iq_source_transition_reset(")
for token in ("s_source_epoch = epoch", "ra_iq_spec_discard_partial()",
              "s_const_ready = -1", "s_const_snapshot_valid = 0U",
              "s_tap_ready = 0U", "ra_iq_dec_cmsis_init",
              "s_dc_i_q16 = 0", "ra_iq_filt_reset()", "s_env_mean = 0",
              "ra_iq_fm_reset()", "memset(s_hil_i", "ra_iq_hil_cmsis_init",
              "s_cw_phase = 0U", "ra_iq_af_reset()", "s_sq_env = 0",
              "s_agc_ms = 0", "s_agc_env = 0", "s_smeter_rms = 0",
              "s_ring_tail = s_ring_head", "s_scope_q_tail = s_scope_q_head",
              "s_scope_wr = 0U", "s_scope_ready = -1"):
    check(token in reset, "source transition reset is missing " + token)
check("s_scope_half = 0U" not in reset,
      "source transition can overwrite a borrowed TIME frame")
check("s_agc_mode != RA_IQ_AGC_MODE_MANUAL" in reset,
      "source transition destroys configured manual AGC gain")
dsp = c_function(c_source, "static void ra_iq_dsp_process(")
check(dsp.index("ra_iq_source_transition_reset(n, m)") <
      dsp.index("if (run_raw_path)"),
      "old DSP history is not cleared before the first new-source sample")
for token in ("s_spec_source_epoch[h] = s_source_epoch",
              "source_epoch != s_source_epoch",
              "s_spec_snapshot_source_epoch != s_source_epoch",
              "s_const_source_epoch[h] = s_source_epoch",
              "s_const_snapshot_valid = 1U"):
    check(token in c_source, "source-epoch display contract is missing " + token)

# EOF/refill failure must remain owned by FILE (zero I/Q) until Python explicitly
# stops it; HOME therefore cannot claim TST while C has already exposed real ADC.
finish_eof = c_function(c_source, "static void ra_iq_file_finish_eof(")
check("s_file.terminal_hold = 1U" in finish_eof and
      "s_file.active_on = 0U" not in finish_eof,
      "FILE EOF falls back to ADC before HOME can clear TST")
file_apply = c_function(c_source, "static void ra_iq_file_apply_request(")
check("if (s_file.terminal_hold)" in file_apply,
      "FILE terminal hold is ignored at the next DSP boundary")
refill_trampoline = c_function(
    machine_source, "static mp_obj_t machine_iqadc_file_refill_trampoline(")
check("ra_iq_adc_file_hold();" in refill_trampoline and
      "ra_iq_adc_file_stop();" not in refill_trampoline,
      "refill exception exposes ADC instead of entering fail-closed FILE hold")
check("for (;;)" not in refill_trampoline and
      "while (" not in refill_trampoline and
      "for (" not in refill_trampoline and
      refill_trampoline.count("mp_call_function_1(") == 1,
      "one scheduler turn can drain FILE refills indefinitely and starve LVGL")
for token in ("file_refill_next", "self->file_refill_pending &=",
              "mp_sched_schedule(", "return mp_const_none;"):
    check(token in refill_trampoline,
          "fair FILE refill trampoline is missing " + token)
claim_at = refill_trampoline.index("self->file_refill_pending &=")
call_at = refill_trampoline.index("mp_call_function_1(")
requeue_at = refill_trampoline.index("mp_sched_schedule(", call_at)
check(claim_at < call_at < requeue_at and
      "return mp_const_none;" in refill_trampoline[requeue_at:],
      "FILE refill does not claim one buffer, call once, requeue, then yield")
file_start = c_function(
    machine_source, "static mp_obj_t machine_iqadc_file_start(")
file_start_flat = " ".join(file_start.split())
start_guard = ("if (status.attached && !status.requested_on && "
               "!status.active_on && (status.state[0] == "
               "(uint8_t)RA_IQ_FILE_READY) && (status.state[1] == "
               "(uint8_t)RA_IQ_FILE_READY))")
check(start_guard in file_start_flat and
      "self->file_refill_pending = 0U;" in file_start and
      "self->file_sched_fail = 0U;" in file_start and
      file_start.index("self->file_sched_fail = 0U;") <
      file_start.index("ra_iq_adc_file_start()"),
      "recovered synchronous prefill retains a stale attach scheduler failure")
refill = method(source, "_refill")
check("self._iq.file_stop()" not in refill,
      "Python refill exception requests ADC before native fail-closed hold")
poll_file = method(app_source, "_poll_iq_file")
check("_finish_iq_file(\"SDRIQ UND" not in poll_file and
      "if st[10] or st[14]:" not in poll_file and
      "if st[10]:" not in poll_file,
      "a recoverable FILE transport delay still aborts playback")
check("self._iq_file.eof and not st[1]" in poll_file and
      "and not st[2]" not in poll_file,
      "Python waits for native ADC fallback instead of ending fail-closed FILE")

print("SDR TESTER SOURCE CONTRACT PASS")
