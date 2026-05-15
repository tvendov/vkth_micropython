"""
TR-3 - SF7/DR5 OTAA join driven by the AGT timer-backed pump only.

Phase 6 exit gate: proves the AGT timer backend wired into the guarded C
pump can drive a full OTAA join WITHOUT any Python-side cadence call
into the C MAC process entry point. The test script deliberately does
NOT invoke the forbidden Python pump call anywhere in its source: a
runtime-built token (see FORBIDDEN_TOKEN) is used so the guard 4
self-substring-scan can detect a regression even inside this very file.

Hypothesis: with LORAWAN_C_PUMP_ENABLE=1, the SetSysTime/TimerSet ELC
plumbing introduced in Phase 6 calls request_pump(...) from the AGT
ISR, lorawan_driver_pump_run executes LoRaMacProcess() and the join
sequence completes within the 30 s observation window.

Wall budget: <= 120 s (typical SF7 join completes in 5-8 s on this
bench; observation window break-early keeps wall at ~6 s typical).

Operator drives one JLink hardware reset BEFORE invocation (RSetType 5).

Usage:
    mpremote connect COM18 run tr3_sf7_timer_partial.py
"""
import sys
import time
import json
import machine

# === Section 0 - Common preamble (mirrors tr2_sf7_join_default.py) =========
# Helpers are duplicated locally so the script is a single-file drop-in for
# `mpremote run`.

ARTIFACT_DIR = "/flash/QA_RESULTS"
SERVER_PRECHECK_DONE = True

MAX_RX_ERROR_MS_TOLERANCE = 15
FORBIDDEN_METHODS = ("set_join_rx1_max_rx_error_override",)

# Runtime-built token: this script must NEVER contain the literal forbidden
# substring in its source, because guard 4 substring-scans this very file.
# Concatenating at runtime produces the string for comparison without
# placing the literal in the source.
FORBIDDEN_TOKEN = "mac" + "." + "process" + "("


def now_iso():
    t = time.gmtime()
    return "%04d%02d%02dT%02d%02d%02dZ" % (t[0], t[1], t[2], t[3], t[4], t[5])


def jlink_reset_marker():
    try:
        cause = machine.reset_cause()
    except Exception:
        cause = -1
    return {"boot_ticks_ms": time.ticks_ms(), "reset_cause": cause}


def server_precondition_note():
    return {"server_precheck_done": bool(SERVER_PRECHECK_DONE),
            "noted_by": "operator"}


def _ensure_artifact_dir():
    try:
        import os
        try:
            os.mkdir(ARTIFACT_DIR)
        except OSError:
            pass
    except Exception:
        pass


def write_artifact(tr_id, payload):
    _ensure_artifact_dir()
    path = "%s/%s_%s.json" % (ARTIFACT_DIR, tr_id, now_iso())
    try:
        f = open(path, "w")
        try:
            f.write(json.dumps(payload))
        finally:
            f.close()
        return path
    except Exception as e:
        print("ARTIFACT_WRITE_FAIL", path, repr(e))
        print("ARTIFACT_INLINE", json.dumps(payload))
        return None


def _safe_pump_diag(mac):
    if hasattr(mac, "pump_diag"):
        try:
            return mac.pump_diag()
        except Exception as e:
            return {"_pump_diag_error": repr(e)}
    if hasattr(mac, "rx_window_diag"):
        try:
            return mac.rx_window_diag()
        except Exception as e:
            return {"_rx_window_diag_error": repr(e)}
    return {}


def _diag_int(diag, key, default=0):
    v = diag.get(key, default) if isinstance(diag, dict) else default
    try:
        return int(v)
    except Exception:
        return default


def _diag_hist_p(diag, key, pct):
    if not isinstance(diag, dict):
        return -1
    v = diag.get(key)
    if isinstance(v, dict):
        return int(v.get(pct, -1))
    if pct in ("p50", "p95", "p99") and isinstance(v, (int, float)):
        return int(v)
    return -1


def _diag_hist_max(diag, key):
    if not isinstance(diag, dict):
        return -1
    v = diag.get(key)
    if isinstance(v, dict):
        return int(v.get("max", -1))
    return -1


def _diag_hist_count(diag, key):
    """Tolerantly return histogram bucket count. Missing key OR missing
    `count` sub-field returns 0 (Phase 6 case where t1 sample-point is
    not landed yet). Returns -1 only when diag itself is not a dict."""
    if not isinstance(diag, dict):
        return -1
    v = diag.get(key)
    if isinstance(v, dict):
        try:
            return int(v.get("count", 0))
        except Exception:
            return 0
    return 0


def _safe_rx_diag(mac):
    if hasattr(mac, "rx_diag"):
        try:
            return mac.rx_diag()
        except Exception as e:
            return {"_rx_diag_error": repr(e)}
    return {}


def _read_max_rx_error_ms(mac):
    """Phase 5/6 clean build exposes E via rx_diag()['max_rx_error_ms'].
    There is NO get_max_rx_error() binding (verified mod_lorawan.c grep)."""
    d = _safe_rx_diag(mac)
    if isinstance(d, dict) and "max_rx_error_ms" in d:
        try:
            return int(d["max_rx_error_ms"])
        except Exception:
            pass
    return -1


def _guard3_source_check():
    """Guard 3 source self-inspection. Try inspect first; fall back to
    reading __file__. If neither works, return ('skipped', None) so the
    caller can downgrade to a SKIP/YELLOW outcome (Phase 6 accepts SKIP
    here because host-side grep is the canonical proof)."""
    try:
        import inspect
        src = inspect.getsource(sys.modules[__name__])
        return "inspect", src
    except Exception:
        pass
    try:
        path = __file__
        f = open(path)
        try:
            src = f.read()
        finally:
            f.close()
        return "file", src
    except Exception:
        return "skipped", None


# === Section 1 - TR-3 parameters ============================================

CREDS_PATH = "/flash/lora_creds.json"
SF7_DR = 5
OBSERVATION_WINDOW_MS = 30000        # Phase 6 generous observation window
POLL_INTERVAL_MS = 50                # tight enough to catch break-early
SPI_BAUDRATE = 8000000               # memory project_renesas_lora_spi_precondition

# Decision 10 soft thresholds (mirror tr2).
PUMP_DISPATCH_P95_SOFT_US = 300
PUMP_DISPATCH_P99_SOFT_US = 1000
PUMP_DISPATCH_MAX_HARD_US = 5000


def _load_creds():
    """Returns (deveui_b, joineui_b, appkey_b) or (None, None, None)."""
    try:
        f = open(CREDS_PATH)
        try:
            d = json.loads(f.read())
        finally:
            f.close()
    except Exception:
        return None, None, None
    try:
        import binascii
        deveui = binascii.unhexlify(d["deveui"])
        joineui = binascii.unhexlify(d["appeui"])
        appkey = binascii.unhexlify(d["appkey"])
        if len(deveui) != 8 or len(joineui) != 8 or len(appkey) != 16:
            return None, None, None
        return deveui, joineui, appkey
    except Exception:
        return None, None, None


# === Section 2 - TR-3 body =================================================

def run():
    boot = jlink_reset_marker()
    srv = server_precondition_note()
    t_start_ms = time.ticks_ms()

    # ------------------------------------------------------------------
    # Step 0 - import lorawan first so the module surface is loaded.
    # ------------------------------------------------------------------
    import lorawan

    # ------------------------------------------------------------------
    # Step 1 - SPI(3) precondition (memory project_renesas_lora_spi_precondition).
    # MUST happen BEFORE lorawan.Mac() else 5 s stall + silent RF failure.
    # ------------------------------------------------------------------
    spi = machine.SPI(3, baudrate=SPI_BAUDRATE)

    # ------------------------------------------------------------------
    # Step 2 - Anti-fake-pass preconditions BEFORE Mac construction.
    # These checks bind on guarantees that must hold at object birth.
    # ------------------------------------------------------------------
    # (no pre-Mac checks remaining; module surface validated below)

    # ------------------------------------------------------------------
    # Step 3 - construct Mac.
    # ------------------------------------------------------------------
    mac = lorawan.Mac()

    # ------------------------------------------------------------------
    # Step 4 - forbidden surface check (guard 2).
    # ------------------------------------------------------------------
    override_present = any(name in dir(mac) for name in FORBIDDEN_METHODS)

    # ------------------------------------------------------------------
    # Step 5 - baseline pump_diag.
    # Phase 6 introduces rx1_arm_t0_stamp_count: number of times the AGT
    # ISR stamped a t0 sample at RX1 arm. Tolerant on missing key (would
    # indicate build-flag mismatch / pre-Phase-6 binary).
    # ------------------------------------------------------------------
    diag_base = _safe_pump_diag(mac)
    pump_request_base = _diag_int(diag_base, "pump_request_count", 0)
    pump_run_base = _diag_int(diag_base, "pump_run_count", 0)
    reentry_base = _diag_int(diag_base, "mac_process_reentry_count", 0)
    nested_base = _diag_int(diag_base, "spi_nested_reject_count", 0)
    t0_stamp_base = _diag_int(diag_base, "rx1_arm_t0_stamp_count", 0)

    # Hard baseline expectations (Phase 6 contract): no pump runs and no
    # t0 stamps before the user has called lorawan_init/join. Soft check:
    # we record but don't FAIL on non-zero base — a stale build that
    # auto-pumps on Mac() construction would surface here.
    baseline_pump_run_clean = (pump_run_base == 0)
    baseline_t0_stamp_clean = (t0_stamp_base == 0)

    # ------------------------------------------------------------------
    # Step 6 - load credentials. RED + clean exit if absent.
    # ------------------------------------------------------------------
    deveui_b, joineui_b, appkey_b = _load_creds()
    if deveui_b is None:
        return {
            "tr_id": "TR-3",
            "iso": now_iso(),
            "boot": boot,
            "server_precheck": srv,
            "verdict": "RED",
            "fail_class": "FAIL_NO_CREDS",
            "metrics": {
                "creds_path": CREDS_PATH,
                "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
            },
        }

    # ------------------------------------------------------------------
    # Step 7 - lorawan_init().
    # ------------------------------------------------------------------
    lorawan_init_rc = None
    lorawan_init_exc = None
    try:
        lorawan_init_rc = mac.lorawan_init()
    except Exception as e:
        lorawan_init_exc = repr(e)

    # ------------------------------------------------------------------
    # Step 8 - set keys + class A.
    # ------------------------------------------------------------------
    set_keys_exc = None
    try:
        mac.set_keys(deveui_b, joineui_b, appkey_b)
    except Exception as e:
        set_keys_exc = repr(e)

    set_class_exc = None
    try:
        mac.set_class("A")
    except Exception as e:
        set_class_exc = repr(e)

    # ------------------------------------------------------------------
    # Step 9 - SystemMaxRxError read for guard 1 (pre-join sample is fine;
    # E is set at region init and does not move during join in clean build).
    # ------------------------------------------------------------------
    max_rx_error_ms = _read_max_rx_error_ms(mac)

    # ------------------------------------------------------------------
    # Step 10 - single-shot OTAA join at DR5 (SF7).
    # mac.join(dr) is ASYNC: returns LoRaMacStatus_t immediately. The
    # JoinAccept arrives later via mlme_confirm which flips self->joined
    # inside the C side. PHASE 6 RULE: this observation loop deliberately
    # does NOT call the forbidden Python pump entry. The AGT timer ISR
    # must drive the pump on its own.
    # ------------------------------------------------------------------
    t_join_start_ms = time.ticks_ms()
    join_submit_rc = None
    join_submit_exc = None
    try:
        join_submit_rc = mac.join(SF7_DR)
    except Exception as e:
        join_submit_exc = repr(e)

    joined = False
    if join_submit_exc is None:
        # Phase 6 observation loop: poll is_joined() ONLY. No pump call.
        # Break early as soon as the join lands to keep wall under budget.
        deadline = time.ticks_add(t_join_start_ms, OBSERVATION_WINDOW_MS)
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            try:
                if mac.is_joined():
                    joined = True
                    break
            except Exception:
                pass
            time.sleep_ms(POLL_INTERVAL_MS)

    t_join_end_ms = time.ticks_ms()
    join_wall_ms = time.ticks_diff(t_join_end_ms, t_join_start_ms)

    # ------------------------------------------------------------------
    # Step 11 - post-join diagnostics.
    # ------------------------------------------------------------------
    diag_post = _safe_pump_diag(mac)
    pump_request_post = _diag_int(diag_post, "pump_request_count", 0)
    pump_run_post = _diag_int(diag_post, "pump_run_count", 0)
    reentry_post = _diag_int(diag_post, "mac_process_reentry_count", 0)
    nested_post = _diag_int(diag_post, "spi_nested_reject_count", 0)
    t0_stamp_post = _diag_int(diag_post, "rx1_arm_t0_stamp_count", 0)

    lat_p50 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p50")
    lat_p95 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p95")
    lat_p99 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p99")
    lat_max = _diag_hist_max(diag_post, "pump_dispatch_latency_us")

    # rx1_arm_to_setrx_us histogram (Phase 8 will populate the t1 sample
    # point; Phase 6 should see count==0). Tolerate missing key entirely.
    arm_setrx_p50 = _diag_hist_p(diag_post, "rx1_arm_to_setrx_us", "p50")
    arm_setrx_p95 = _diag_hist_p(diag_post, "rx1_arm_to_setrx_us", "p95")
    arm_setrx_p99 = _diag_hist_p(diag_post, "rx1_arm_to_setrx_us", "p99")
    arm_setrx_max = _diag_hist_max(diag_post, "rx1_arm_to_setrx_us")
    arm_setrx_count = _diag_hist_count(diag_post, "rx1_arm_to_setrx_us")

    # Recheck E (in case region init shifted it; mirror tr2).
    max_rx_error_ms_post = _read_max_rx_error_ms(mac)
    if max_rx_error_ms_post >= 0:
        max_rx_error_ms = max_rx_error_ms_post

    # Deltas.
    pump_run_delta = pump_run_post - pump_run_base
    pump_request_delta = pump_request_post - pump_request_base
    reentry_delta = reentry_post - reentry_base
    nested_delta = nested_post - nested_base
    t0_stamp_delta = t0_stamp_post - t0_stamp_base

    # ------------------------------------------------------------------
    # Step 12 - 10 anti-fake-pass guards.
    # ------------------------------------------------------------------
    guards = {}

    # --- Guard 1: region-default SystemMaxRxError E -------------------
    if max_rx_error_ms < 0:
        guards["g1_max_rx_error"] = \
            "SKIP: E unreadable (rx_diag missing key)"
    elif max_rx_error_ms <= MAX_RX_ERROR_MS_TOLERANCE:
        guards["g1_max_rx_error"] = \
            "PASS: max_rx_error_ms=%d <= %d" % (
                max_rx_error_ms, MAX_RX_ERROR_MS_TOLERANCE)
    else:
        guards["g1_max_rx_error"] = \
            "FAIL: E inflated max_rx_error_ms=%d > %d" % (
                max_rx_error_ms, MAX_RX_ERROR_MS_TOLERANCE)

    # --- Guard 2: forbidden override surface absent -------------------
    if not override_present:
        guards["g2_no_override"] = \
            "PASS: no forbidden override method present"
    else:
        guards["g2_no_override"] = \
            "FAIL: override surface still present in dir(mac)"

    # --- Guard 3: this script never calls tuning shims ----------------
    g3_mech, g3_src = _guard3_source_check()
    if g3_src is None:
        # Phase 6 explicitly accepts SKIP — host-side grep is canonical.
        guards["g3_no_tuning_shim"] = \
            "SKIP: source unreadable via both mechanisms - " \
            "host-side verification required"
    else:
        g3a = "mac.set_max_rx_error" not in g3_src
        g3b = "mac.set_min_rx_symbols" not in g3_src
        if g3a and g3b:
            guards["g3_no_tuning_shim"] = \
                "PASS: no tuning shim calls (mechanism=%s)" % g3_mech
        else:
            offenders = []
            if not g3a:
                offenders.append("mac.set_max_rx_error")
            if not g3b:
                offenders.append("mac.set_min_rx_symbols")
            guards["g3_no_tuning_shim"] = \
                "FAIL: script contains tuning shim call(s): %s" % \
                ",".join(offenders)

    # --- Guard 4: script never calls the forbidden Python pump entry ---
    # Substring scan via the runtime-built FORBIDDEN_TOKEN. The literal
    # is never present in this source.
    if g3_src is None:
        guards["g4_no_macprocess_call"] = \
            "SKIP: source unreadable - host-side grep on file is " \
            "canonical proof (operator runs externally)"
    else:
        # Count occurrences. The FORBIDDEN_TOKEN string literal itself is
        # built via concatenation, so the source must contain zero hits
        # of the assembled substring.
        if FORBIDDEN_TOKEN in g3_src:
            guards["g4_no_macprocess_call"] = \
                "FAIL: script source contains forbidden token " \
                "(Phase 6 cadence rule regressed)"
        else:
            guards["g4_no_macprocess_call"] = \
                "PASS: forbidden token absent from script source " \
                "(mechanism=%s)" % g3_mech

    # --- Guard 5: pump_run_count delta > 0 ----------------------------
    if pump_run_delta > 0:
        guards["g5_pump_ran"] = \
            "PASS: pump_run_count delta=%d" % pump_run_delta
    else:
        guards["g5_pump_ran"] = \
            "FAIL: pump never ran (delta=%d) - timer hook likely " \
            "unwired or LORAWAN_C_PUMP_ENABLE=0" % pump_run_delta

    # --- Guard 6: rx1_arm_t0_stamp_count delta >= 1 -------------------
    # Phase 6 keystone: proves the AGT ISR fired and stamped a t0 sample
    # at RX1 arm. If zero, the timer-to-pump hook is unwired.
    if "rx1_arm_t0_stamp_count" not in diag_post:
        guards["g6_t0_stamped"] = \
            "FAIL: rx1_arm_t0_stamp_count key absent from pump_diag - " \
            "Phase 6 build flag mismatch (counter not compiled in)"
    elif t0_stamp_delta >= 1:
        guards["g6_t0_stamped"] = \
            "PASS: rx1_arm_t0_stamp_count delta=%d" % t0_stamp_delta
    else:
        guards["g6_t0_stamped"] = \
            "FAIL: rx1_arm_t0_stamp_count delta=%d (AGT ISR never " \
            "stamped t0 - timer hook unwired)" % t0_stamp_delta

    # --- Guard 7: is_joined() flipped True ----------------------------
    if joined:
        guards["g7_is_joined"] = "PASS: is_joined()=True"
    else:
        guards["g7_is_joined"] = \
            "FAIL: is_joined()=False after %d ms observation - " \
            "timer-driven pump did not complete join" % join_wall_ms

    # --- Guard 8: no nested re-entry, no SPI nested reject ------------
    if reentry_delta == 0 and nested_delta == 0:
        guards["g8_no_reentry_nested"] = \
            "PASS: reentry=0 nested=0 across observation"
    else:
        guards["g8_no_reentry_nested"] = \
            "FAIL: reentry_delta=%d nested_delta=%d" % (
                reentry_delta, nested_delta)

    # --- Guard 9: latency p95 soft threshold --------------------------
    if lat_p95 > 0 and lat_p95 > PUMP_DISPATCH_P95_SOFT_US:
        guards["g9_latency_p95"] = \
            "YELLOW: p95=%d us > %d us soft threshold" % (
                lat_p95, PUMP_DISPATCH_P95_SOFT_US)
    else:
        guards["g9_latency_p95"] = \
            "PASS: p95=%d us (soft<=%d)" % (
                lat_p95, PUMP_DISPATCH_P95_SOFT_US)

    # --- Guard 10: rx1_arm_to_setrx_us histogram count == 0 at Phase 6 -
    # t1 sample point lands at Phase 8. Non-zero is INFORMATIONAL only:
    # either a future phase landed early, or the histogram is otherwise
    # populated. Phase 6 verdict does NOT FAIL on this signal.
    if arm_setrx_count == 0:
        guards["g10_hist_zero"] = \
            "PASS: rx1_arm_to_setrx_us count=0 (expected at Phase 6)"
    elif arm_setrx_count > 0:
        guards["g10_hist_zero"] = \
            "INFO: rx1_arm_to_setrx_us count=%d (UNEXPECTED at Phase 6 - " \
            "verify t1 sample point not landed early)" % arm_setrx_count
    else:
        guards["g10_hist_zero"] = \
            "INFO: rx1_arm_to_setrx_us count unreadable (key absent)"

    # ------------------------------------------------------------------
    # Step 13 - verdict logic (Phase 6 rules).
    # ------------------------------------------------------------------
    verdict = "GREEN"
    fail_class = None

    # Exception-driven RED.
    if lorawan_init_exc is not None:
        verdict = "RED"
        fail_class = "FAIL_LORAWAN_INIT_EXC"
    elif set_keys_exc is not None:
        verdict = "RED"
        fail_class = "FAIL_SET_KEYS_EXC"
    # Pump-guard hardness.
    elif reentry_delta > 0 or nested_delta > 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_GUARD"
    # Forbidden surface present.
    elif override_present:
        verdict = "RED"
        fail_class = "FAIL_FORBIDDEN_SURFACE"
    # Phase 6 keystone counters.
    elif pump_run_delta == 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_NEVER_RAN"
    elif t0_stamp_delta == 0:
        verdict = "RED"
        fail_class = "FAIL_TIMER_HOOK_UNWIRED"
    # Join must complete.
    elif not joined:
        verdict = "RED"
        fail_class = "FAIL_NOT_JOINED_TIMER_ONLY"
    # Forbidden token regression in script source.
    elif guards["g4_no_macprocess_call"].startswith("FAIL"):
        verdict = "RED"
        fail_class = "FAIL_SCRIPT_CONTAINS_FORBIDDEN_TOKEN"
    # Tuning shim regression.
    elif guards["g3_no_tuning_shim"].startswith("FAIL"):
        verdict = "RED"
        fail_class = "FAIL_TUNING_SHIM_PRESENT"
    # Hard dispatch-max sanity.
    elif lat_max > 0 and lat_max > PUMP_DISPATCH_MAX_HARD_US:
        verdict = "RED"
        fail_class = "FAIL_DISPATCH_MAX_SANITY"
    # E inflated.
    elif max_rx_error_ms >= 0 and \
            max_rx_error_ms > MAX_RX_ERROR_MS_TOLERANCE:
        verdict = "RED"
        fail_class = "FAIL_E_INFLATED"
    else:
        # YELLOW conditions.
        if guards["g3_no_tuning_shim"].startswith("SKIP"):
            verdict = "YELLOW"
            fail_class = "WARN_G3_SOURCE_UNREADABLE"
        elif guards["g4_no_macprocess_call"].startswith("SKIP"):
            verdict = "YELLOW"
            fail_class = "WARN_G4_SOURCE_UNREADABLE"
        elif lat_p95 > 0 and lat_p95 > PUMP_DISPATCH_P95_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DISPATCH_P95"
        elif lat_p99 > 0 and lat_p99 > PUMP_DISPATCH_P99_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DISPATCH_P99"
        elif arm_setrx_count > 0:
            verdict = "YELLOW"
            fail_class = "WARN_HIST_NONZERO_AT_PHASE6"

    metrics = {
        "join_wall_ms": join_wall_ms,
        "join_dr": SF7_DR,
        "join_submit_rc": join_submit_rc,
        "join_submit_exc": join_submit_exc,
        "is_joined": bool(joined),
        "max_rx_error_ms": max_rx_error_ms,
        "override_method_present": bool(override_present),
        "lorawan_init_rc": lorawan_init_rc,
        "lorawan_init_exc": lorawan_init_exc,
        "set_keys_exc": set_keys_exc,
        "set_class_exc": set_class_exc,
        "pump_request_base": pump_request_base,
        "pump_request_post": pump_request_post,
        "pump_request_delta": pump_request_delta,
        "pump_run_base": pump_run_base,
        "pump_run_post": pump_run_post,
        "pump_run_delta": pump_run_delta,
        "rx1_arm_t0_stamp_count_base": t0_stamp_base,
        "rx1_arm_t0_stamp_count_post": t0_stamp_post,
        "rx1_arm_t0_stamp_count_delta": t0_stamp_delta,
        "mac_process_reentry_count": reentry_post,
        "mac_process_reentry_delta": reentry_delta,
        "spi_nested_reject_count": nested_post,
        "spi_nested_reject_delta": nested_delta,
        "baseline_pump_run_clean": bool(baseline_pump_run_clean),
        "baseline_t0_stamp_clean": bool(baseline_t0_stamp_clean),
        "pump_dispatch_latency_us": {
            "p50": lat_p50,
            "p95": lat_p95,
            "p99": lat_p99,
            "max": lat_max,
        },
        "rx1_arm_to_setrx_us": {
            "p50": arm_setrx_p50,
            "p95": arm_setrx_p95,
            "p99": arm_setrx_p99,
            "max": arm_setrx_max,
            "count": arm_setrx_count,
        },
        "observation_window_ms": OBSERVATION_WINDOW_MS,
        "guards": guards,
        "guard3_source_mechanism": g3_mech,
        "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
    }

    return {
        "tr_id": "TR-3",
        "iso": now_iso(),
        "boot": boot,
        "server_precheck": srv,
        "verdict": verdict,
        "fail_class": fail_class,
        "metrics": metrics,
    }


if __name__ == "__main__":
    try:
        result = run()
    except AssertionError as e:
        result = {
            "tr_id": "TR-3",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_ANTI_FAKE_PASS_GUARD",
            "metrics": {"assert_msg": str(e)},
        }
        write_artifact("TR-3", result)
        print(json.dumps(result))
        sys.exit(1)
    except Exception as e:
        result = {
            "tr_id": "TR-3",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_SCRIPT_EXCEPTION",
            "metrics": {"exc": repr(e)},
        }
        write_artifact("TR-3", result)
        print(json.dumps(result))
        sys.exit(1)
    write_artifact("TR-3", result)
    print(json.dumps(result))
    if result["verdict"] != "GREEN":
        sys.exit(1)
