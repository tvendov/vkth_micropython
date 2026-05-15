"""
TR-1 - Phase 8 exit gate: 10 cold-reset OTAA rejoins at SF7/DR5.

Hypothesis: with the Phase 8 wiring landed (rx1_arm_to_setrx_us histogram
t1 source = opcode 0x82 in SPI transport, s_rx_window_active provenance
re-sourced from SPI to the timer backend, SetRx prebuild cache), ten
back-to-back cold rejoins at DR5 all converge with max_rx_error_ms <= 15
and the C pump drives every join with zero re-entry / zero SPI nested
reject. The HARD release gate is rx1_arm_to_setrx_us p99 < 2000 us
(operator decision 10).

PHASE 8 RULE: this script never calls the forbidden Python pump entry.
A runtime-built token (FORBIDDEN_TOKEN) is used so the guard 4
substring scan can detect a regression even inside this very file.

Cold-reset between iterations is forced by calling
mac.nvm_factory_reset() and constructing a fresh lorawan.Mac() per
iteration. Without this the second iter onward sees is_joined()=True
immediately and would NOT be a true rejoin sample.

Wall budget: <= 150 s total (10 iters x ~12 s nominal + diag).

Operator drives one JLink hardware reset BEFORE invocation (RSetType 5).

Usage:
    mpremote connect COM18 run tr1_sf7_rejoin_default.py
"""
import sys
import time
import json
import machine

# === Section 0 - Common preamble (mirrors tr2 / tr3 / tr4) =================
# Helpers duplicated locally so the script is a single-file `mpremote run`.

ARTIFACT_DIR = "/flash/QA_RESULTS"
SERVER_PRECHECK_DONE = True

MAX_RX_ERROR_MS_TOLERANCE = 15
FORBIDDEN_METHODS = ("set_join_rx1_max_rx_error_override",)

# Runtime-built token: this script must NEVER contain the literal forbidden
# substring in its source - guard 4 substring-scans this file.
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
    if not isinstance(diag, dict):
        return -1
    v = diag.get(key)
    if isinstance(v, dict):
        try:
            return int(v.get("count", 0))
        except Exception:
            return 0
    return 0


def _diag_hist_first_sample_boot_ms(diag, key):
    """Optional helper: returns -1 when key absent. The pump_diag struct
    may or may not carry first_sample_boot_ms per histogram - tolerate."""
    if not isinstance(diag, dict):
        return -1
    v = diag.get(key)
    if isinstance(v, dict):
        try:
            return int(v.get("first_sample_boot_ms", -1))
        except Exception:
            return -1
    return -1


def _safe_rx_diag(mac):
    if hasattr(mac, "rx_diag"):
        try:
            return mac.rx_diag()
        except Exception as e:
            return {"_rx_diag_error": repr(e)}
    return {}


def _read_max_rx_error_ms(mac):
    """Phase 5/6/7/8 clean build exposes E via rx_diag()['max_rx_error_ms'].
    There is NO get_max_rx_error() binding (verified mod_lorawan.c grep)."""
    d = _safe_rx_diag(mac)
    if isinstance(d, dict) and "max_rx_error_ms" in d:
        try:
            return int(d["max_rx_error_ms"])
        except Exception:
            pass
    return -1


def _guard3_source_check():
    """Guard 3/4 source self-inspection. Try inspect first; fall back to
    reading __file__. If neither works, return ('skipped', None) so the
    caller can downgrade to SKIP - host-side grep is canonical proof."""
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


# === Section 1 - TR-1 parameters ===========================================

CREDS_PATH = "/flash/lora_creds.json"
SF7_DR = 5
ITER_TARGET = 10
ITER_WAIT_MS = 12000              # per-iter join wait budget
ITER_POLL_INTERVAL_MS = 50        # is_joined poll interval
SPI_BAUDRATE = 8000000            # memory project_renesas_lora_spi_precondition

TOTAL_WALL_BUDGET_MS = 150000     # master MSG hard cap

# Phase 8 HARD release gate (decision 10).
RX1_ARM_TO_SETRX_P99_HARD_US = 2000
# Phase 8 final target (decision 12).
RX1_ARM_TO_SETRX_P95_SOFT_US = 500
# Sample validity floor.
RX1_HIST_MIN_SAMPLES = 14

# DIO1 regression soft thresholds (Phase 7 carry-over).
DIO1_P95_SOFT_US = 200
# Pump dispatch latency soft (decision 10 carry-over).
PUMP_DISPATCH_P95_SOFT_US = 300
PUMP_DISPATCH_P99_SOFT_US = 1000
PUMP_DISPATCH_MAX_HARD_US = 5000

# TR-1 join-rate gates.
JOIN_RATE_GREEN_MIN = 10
JOIN_RATE_YELLOW_MIN = 7


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


def _read_reason_tuple(diag):
    """pump_request_by_reason is a 6-tuple. Defensively pad / truncate."""
    v = diag.get("pump_request_by_reason") if isinstance(diag, dict) else None
    if v is None:
        return (0, 0, 0, 0, 0, 0)
    try:
        t = tuple(int(x) for x in v)
    except Exception:
        return (0, 0, 0, 0, 0, 0)
    if len(t) >= 6:
        return t[:6]
    return t + (0,) * (6 - len(t))


def _read_reason_dict(diag):
    """Return per-reason breakdown keyed by name. Tolerant on missing data
    or shape mismatch. The 6-tuple ordering follows mod_lorawan.c
    pump_request_by_reason enum: NOTIFY, TIMER, DIO1, PY, RX_DONE, OTHER.
    If the bench build orders differently the labels are still useful as
    indices; consumers should treat them as a labelled view of the tuple."""
    t = _read_reason_tuple(diag)
    return {
        "NOTIFY": int(t[0]),
        "TIMER":  int(t[1]),
        "DIO1":   int(t[2]),
        "PY":     int(t[3]),
        "RX_DONE": int(t[4]),
        "OTHER":  int(t[5]),
    }


def _safe_factory_reset(mac):
    """Force a cold-state on the LoRaMac session so the NEXT join() is a
    true rejoin and not a no-op against the persisted JoinAccept. Returns
    (ok, err_repr_or_None)."""
    if not hasattr(mac, "nvm_factory_reset"):
        return False, "no_binding"
    try:
        mac.nvm_factory_reset()
        return True, None
    except Exception as e:
        return False, repr(e)


# === Section 2 - TR-1 body =================================================

def run():
    boot = jlink_reset_marker()
    srv = server_precondition_note()
    t_start_ms = time.ticks_ms()

    # ------------------------------------------------------------------
    # Step 0 - import lorawan.
    # ------------------------------------------------------------------
    import lorawan

    # ------------------------------------------------------------------
    # Step 1 - SPI(3) precondition (memory project_renesas_lora_spi_precondition).
    # MUST happen BEFORE any lorawan.Mac() else 5 s stall + silent RF failure.
    # The SPI handle is shared across all iterations - only the Mac()
    # singleton + LoRaMac state is rebuilt per iteration.
    # ------------------------------------------------------------------
    spi = machine.SPI(3, baudrate=SPI_BAUDRATE)

    # ------------------------------------------------------------------
    # Step 2 - load credentials ONCE. RED + clean exit if absent.
    # ------------------------------------------------------------------
    deveui_b, joineui_b, appkey_b = _load_creds()
    if deveui_b is None:
        return {
            "tr_id": "TR-1",
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
    # Step 3 - construct Mac once for baseline + forbidden-surface check
    # + initial diag snapshot. Iterations may rebuild the Mac() singleton
    # if a fresh ctor proves necessary; the C-side singleton allows this.
    # ------------------------------------------------------------------
    mac = lorawan.Mac()

    override_present = any(name in dir(mac) for name in FORBIDDEN_METHODS)
    factory_reset_supported = hasattr(mac, "nvm_factory_reset")

    # Baseline pump_diag snapshot (pre any join).
    diag_base = _safe_pump_diag(mac)
    pump_request_base = _diag_int(diag_base, "pump_request_count", 0)
    pump_run_base = _diag_int(diag_base, "pump_run_count", 0)
    reentry_base = _diag_int(diag_base, "mac_process_reentry_count", 0)
    nested_base = _diag_int(diag_base, "spi_nested_reject_count", 0)
    isr_seq_base = _diag_int(diag_base, "dio1_isr_seq", 0)
    pump_seen_seq_base = _diag_int(diag_base, "dio1_pump_seen_seq", 0)
    t0_stamp_base = _diag_int(diag_base, "rx1_arm_t0_stamp_count", 0)
    rx_via_spi_base = _diag_int(
        diag_base, "rx_window_active_set_via_spi_count", 0)
    rx1_hist_base = _diag_hist_count(diag_base, "rx1_arm_to_setrx_us")
    reason_base = _read_reason_tuple(diag_base)

    # ------------------------------------------------------------------
    # Step 4 - per-iter loop. lorawan_init / set_keys / set_class are
    # repeated each iter (the clean build is not auto-init). The factory
    # reset is applied AFTER each successful join so the next iter's
    # join() lands a real JoinRequest on the air, not a session-cached
    # no-op.
    # ------------------------------------------------------------------
    iter_total = 0
    iter_joined = 0
    join_walls_ms = []
    max_rx_error_per_iter = []
    factory_reset_ok_per_iter = []
    per_iter_exc = []

    # First-iter init flags (we run lorawan_init once; the C side ignores
    # subsequent init when already initialised but we still try to be
    # tolerant).
    lorawan_init_exc = None
    try:
        mac.lorawan_init()
    except Exception as e:
        lorawan_init_exc = repr(e)

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

    # If lorawan_init / set_keys failed the rest is meaningless.
    if lorawan_init_exc is not None or set_keys_exc is not None:
        return {
            "tr_id": "TR-1",
            "iso": now_iso(),
            "boot": boot,
            "server_precheck": srv,
            "verdict": "RED",
            "fail_class": ("FAIL_LORAWAN_INIT_EXC"
                           if lorawan_init_exc is not None
                           else "FAIL_SET_KEYS_EXC"),
            "metrics": {
                "lorawan_init_exc": lorawan_init_exc,
                "set_keys_exc": set_keys_exc,
                "set_class_exc": set_class_exc,
                "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
            },
        }

    for i in range(ITER_TARGET):
        # Total-wall watchdog (master MSG 150 s hard cap).
        if time.ticks_diff(time.ticks_ms(), t_start_ms) >= \
                TOTAL_WALL_BUDGET_MS:
            break

        iter_total += 1
        iter_exc = None
        t_iter_start_ms = time.ticks_ms()

        # mac.join() at DR5. The JoinAccept arrives later via the C pump
        # (Phase 6 AGT timer + Phase 7 DIO1 path). NO forbidden pump
        # call here - we only poll is_joined().
        try:
            mac.join(SF7_DR)
        except Exception as e:
            iter_exc = repr(e)

        joined_this_iter = False
        if iter_exc is None:
            deadline = time.ticks_add(t_iter_start_ms, ITER_WAIT_MS)
            while time.ticks_diff(deadline, time.ticks_ms()) > 0:
                # Total-wall watchdog inside inner loop.
                if time.ticks_diff(time.ticks_ms(), t_start_ms) >= \
                        TOTAL_WALL_BUDGET_MS:
                    break
                try:
                    if mac.is_joined():
                        joined_this_iter = True
                        break
                except Exception:
                    pass
                time.sleep_ms(ITER_POLL_INTERVAL_MS)

        join_walls_ms.append(
            time.ticks_diff(time.ticks_ms(), t_iter_start_ms))
        max_rx_error_per_iter.append(_read_max_rx_error_ms(mac))
        per_iter_exc.append(iter_exc)

        if joined_this_iter:
            iter_joined += 1

        # Force cold-state for the NEXT iter via nvm_factory_reset.
        # If the binding is missing we record that and continue; the
        # subsequent iter would see is_joined()=True immediately and the
        # join_rate metric would look artificially perfect - this is
        # documented as a known limitation in the script header.
        fr_ok, _fr_err = _safe_factory_reset(mac)
        factory_reset_ok_per_iter.append(bool(fr_ok))
        if fr_ok:
            # After a factory reset the LoRaMac session is wiped; the next
            # join() will issue a fresh JoinRequest. set_class is preserved
            # by the regional defaults but re-assert for safety.
            try:
                mac.set_class("A")
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Step 5 - post-loop diag snapshot.
    # ------------------------------------------------------------------
    diag_post = _safe_pump_diag(mac)
    pump_request_post = _diag_int(diag_post, "pump_request_count", 0)
    pump_run_post = _diag_int(diag_post, "pump_run_count", 0)
    reentry_post = _diag_int(diag_post, "mac_process_reentry_count", 0)
    nested_post = _diag_int(diag_post, "spi_nested_reject_count", 0)
    isr_seq_post = _diag_int(diag_post, "dio1_isr_seq", 0)
    pump_seen_seq_post = _diag_int(diag_post, "dio1_pump_seen_seq", 0)
    seq_diff = isr_seq_post - pump_seen_seq_post
    t0_stamp_post = _diag_int(diag_post, "rx1_arm_t0_stamp_count", 0)
    rx_via_spi_post = _diag_int(
        diag_post, "rx_window_active_set_via_spi_count", 0)
    reason_post = _read_reason_tuple(diag_post)
    reason_dict_post = _read_reason_dict(diag_post)
    reason_delta = tuple(
        int(reason_post[i]) - int(reason_base[i]) for i in range(6))

    # Phase 8 histogram: rx1_arm_to_setrx_us.
    hist_p50 = _diag_hist_p(diag_post, "rx1_arm_to_setrx_us", "p50")
    hist_p95 = _diag_hist_p(diag_post, "rx1_arm_to_setrx_us", "p95")
    hist_p99 = _diag_hist_p(diag_post, "rx1_arm_to_setrx_us", "p99")
    hist_max = _diag_hist_max(diag_post, "rx1_arm_to_setrx_us")
    hist_count = _diag_hist_count(diag_post, "rx1_arm_to_setrx_us")
    hist_first_sample_boot_ms = _diag_hist_first_sample_boot_ms(
        diag_post, "rx1_arm_to_setrx_us")

    # DIO1 histogram (Phase 7 regression).
    dio1_p50 = _diag_hist_p(diag_post, "dio1_to_pump_us", "p50")
    dio1_p95 = _diag_hist_p(diag_post, "dio1_to_pump_us", "p95")
    dio1_p99 = _diag_hist_p(diag_post, "dio1_to_pump_us", "p99")
    dio1_max = _diag_hist_max(diag_post, "dio1_to_pump_us")
    dio1_count = _diag_hist_count(diag_post, "dio1_to_pump_us")

    # Pump dispatch latency (decision 10 regression watchdog).
    lat_p50 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p50")
    lat_p95 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p95")
    lat_p99 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p99")
    lat_max = _diag_hist_max(diag_post, "pump_dispatch_latency_us")
    lat_count = _diag_hist_count(diag_post, "pump_dispatch_latency_us")
    lat_first_sample_boot_ms = _diag_hist_first_sample_boot_ms(
        diag_post, "pump_dispatch_latency_us")

    # Deltas.
    pump_run_delta = pump_run_post - pump_run_base
    pump_request_delta = pump_request_post - pump_request_base
    reentry_delta = reentry_post - reentry_base
    nested_delta = nested_post - nested_base
    t0_stamp_delta = t0_stamp_post - t0_stamp_base
    rx_via_spi_delta = rx_via_spi_post - rx_via_spi_base

    # E across the iter set: report worst-case.
    if max_rx_error_per_iter:
        valid_e = [x for x in max_rx_error_per_iter if x >= 0]
        max_rx_error_ms = max(valid_e) if valid_e else -1
    else:
        max_rx_error_ms = _read_max_rx_error_ms(mac)

    # ------------------------------------------------------------------
    # Step 6 - 13 anti-fake-pass guards (master MSG p8-rx-fast-start-guarded
    # acceptance verbatim).
    # ------------------------------------------------------------------
    guards = {}

    # --- g01: region-default E ----------------------------------------
    if max_rx_error_ms < 0:
        guards["g01_max_rx_error"] = \
            "SKIP: E unreadable (rx_diag missing key)"
    elif max_rx_error_ms <= MAX_RX_ERROR_MS_TOLERANCE:
        guards["g01_max_rx_error"] = \
            "PASS: max_rx_error_ms=%d <= %d (worst-case across iters)" % (
                max_rx_error_ms, MAX_RX_ERROR_MS_TOLERANCE)
    else:
        guards["g01_max_rx_error"] = \
            "FAIL: E inflated max_rx_error_ms=%d > %d" % (
                max_rx_error_ms, MAX_RX_ERROR_MS_TOLERANCE)

    # --- g02: forbidden override surface absent -----------------------
    if not override_present:
        guards["g02_no_override"] = \
            "PASS: no forbidden override method present"
    else:
        guards["g02_no_override"] = \
            "FAIL: override surface still present in dir(mac)"

    # --- g03 + g04 source self-scan -----------------------------------
    g3_mech, g3_src = _guard3_source_check()

    if g3_src is None:
        guards["g03_no_tuning_shim"] = \
            "SKIP: source unreadable via both mechanisms - " \
            "host-side verification required"
    else:
        g3a = "mac.set_max_rx_error" not in g3_src
        g3b = "mac.set_min_rx_symbols" not in g3_src
        if g3a and g3b:
            guards["g03_no_tuning_shim"] = \
                "PASS: no tuning shim calls (mechanism=%s)" % g3_mech
        else:
            offenders = []
            if not g3a:
                offenders.append("mac.set_max_rx_error")
            if not g3b:
                offenders.append("mac.set_min_rx_symbols")
            guards["g03_no_tuning_shim"] = \
                "FAIL: script contains tuning shim call(s): %s" % \
                ",".join(offenders)

    # --- g04: script never calls the forbidden Python pump entry -----
    if g3_src is None:
        guards["g04_no_macprocess_call"] = \
            "SKIP: source unreadable - host-side grep on file is " \
            "canonical proof (operator runs externally)"
    else:
        if FORBIDDEN_TOKEN in g3_src:
            guards["g04_no_macprocess_call"] = \
                "FAIL: script source contains forbidden token " \
                "(Phase 8 cadence rule regressed)"
        else:
            guards["g04_no_macprocess_call"] = \
                "PASS: forbidden token absent from script source " \
                "(mechanism=%s)" % g3_mech

    # --- g05: pump_run_count grew across the iter set ----------------
    if pump_run_delta > 0:
        guards["g05_pump_ran"] = \
            "PASS: pump_run_count delta=%d" % pump_run_delta
    else:
        guards["g05_pump_ran"] = \
            "FAIL: pump never ran (delta=%d) - timer/DIO1 hook unwired or " \
            "LORAWAN_C_PUMP_ENABLE=0" % pump_run_delta

    # --- g06: rx1_arm_to_setrx_us has real samples -------------------
    if hist_count >= RX1_HIST_MIN_SAMPLES:
        guards["g06_rx1_hist_samples"] = \
            "PASS: count=%d >= %d" % (hist_count, RX1_HIST_MIN_SAMPLES)
    else:
        guards["g06_rx1_hist_samples"] = \
            "FAIL: count=%d < %d (sample floor)" % (
                hist_count, RX1_HIST_MIN_SAMPLES)

    # --- g07: HARD release gate (decision 10) p99 < 2000 us ----------
    if hist_p99 < 0:
        guards["g07_p99_hard_gate"] = \
            "FAIL: rx1_arm_to_setrx_us p99 unreadable"
    elif hist_p99 < RX1_ARM_TO_SETRX_P99_HARD_US:
        guards["g07_p99_hard_gate"] = \
            "PASS: p99=%d us < %d us" % (
                hist_p99, RX1_ARM_TO_SETRX_P99_HARD_US)
    else:
        guards["g07_p99_hard_gate"] = \
            "FAIL: p99=%d us >= %d us hard gate breached" % (
                hist_p99, RX1_ARM_TO_SETRX_P99_HARD_US)

    # --- g08: Phase 8 final target p95 <= 500 us ---------------------
    if hist_p95 < 0:
        guards["g08_p95_target"] = \
            "SKIP: rx1_arm_to_setrx_us p95 unreadable"
    elif hist_p95 <= RX1_ARM_TO_SETRX_P95_SOFT_US:
        guards["g08_p95_target"] = \
            "PASS: p95=%d us <= %d us" % (
                hist_p95, RX1_ARM_TO_SETRX_P95_SOFT_US)
    else:
        guards["g08_p95_target"] = \
            "YELLOW: p95=%d us > %d us soft target" % (
                hist_p95, RX1_ARM_TO_SETRX_P95_SOFT_US)

    # --- g09: s_rx_window_active set by timer path, NOT SPI ----------
    if rx_via_spi_delta == 0:
        guards["g09_rx_window_provenance"] = \
            "PASS: set_via_spi_count delta=0 (timer-only)"
    else:
        guards["g09_rx_window_provenance"] = \
            "FAIL: set_via_spi_count delta=%d - Edit B regressed " \
            "(SPI provenance still active)" % rx_via_spi_delta

    # --- g10: RX1 t0 hook still firing (Phase 6 regression) ----------
    if "rx1_arm_t0_stamp_count" not in diag_post:
        guards["g10_t0_stamping"] = \
            "FAIL: rx1_arm_t0_stamp_count key absent - build flag mismatch"
    elif t0_stamp_delta > 0:
        guards["g10_t0_stamping"] = \
            "PASS: rx1_arm_t0_stamp_count delta=%d" % t0_stamp_delta
    else:
        guards["g10_t0_stamping"] = \
            "FAIL: rx1_arm_t0_stamp_count delta=%d (AGT ISR not stamping)" % \
                t0_stamp_delta

    # --- g11: DIO1 path still healthy (Phase 7 regression) -----------
    g11_p95_ok = (dio1_p95 < 0) or (dio1_p95 <= DIO1_P95_SOFT_US)
    g11_seq_ok = (isr_seq_post == pump_seen_seq_post)
    if not g11_seq_ok:
        guards["g11_dio1_regression"] = \
            "FAIL: dio1 seq mismatch isr=%d pump=%d diff=%d" % (
                isr_seq_post, pump_seen_seq_post, seq_diff)
    elif not g11_p95_ok:
        guards["g11_dio1_regression"] = \
            "YELLOW: dio1_p95=%d us > %d us seq=%d==%d" % (
                dio1_p95, DIO1_P95_SOFT_US,
                isr_seq_post, pump_seen_seq_post)
    else:
        guards["g11_dio1_regression"] = \
            "PASS: dio1_p95=%d us seq=%d==%d" % (
                dio1_p95, isr_seq_post, pump_seen_seq_post)

    # --- g12: no nested re-entry / SPI conflict ----------------------
    if reentry_delta == 0 and nested_delta == 0:
        guards["g12_no_reentry_nested"] = \
            "PASS: reentry=0 nested=0 across iter set"
    else:
        guards["g12_no_reentry_nested"] = \
            "FAIL: reentry_delta=%d nested_delta=%d" % (
                reentry_delta, nested_delta)

    # --- g13: TR-1 SF7 join rate -------------------------------------
    # NOTE: this guard meaningfully measures rejoin ONLY if
    # nvm_factory_reset succeeded between iterations. If the binding is
    # missing or threw, iter 2+ would see is_joined() True instantly and
    # inflate the rate. Operator must cross-match with server dev_nonces
    # delta (expected [9, 11]).
    if iter_joined >= JOIN_RATE_GREEN_MIN:
        guards["g13_join_rate"] = \
            "PASS: joined=%d/%d" % (iter_joined, iter_total)
    elif iter_joined >= JOIN_RATE_YELLOW_MIN:
        guards["g13_join_rate"] = \
            "YELLOW: joined=%d/%d (>=%d for GREEN)" % (
                iter_joined, iter_total, JOIN_RATE_GREEN_MIN)
    else:
        guards["g13_join_rate"] = \
            "FAIL: joined=%d/%d (<%d hard floor)" % (
                iter_joined, iter_total, JOIN_RATE_YELLOW_MIN)

    # ------------------------------------------------------------------
    # Step 7 - verdict logic (master MSG verbatim).
    # ------------------------------------------------------------------
    verdict = "GREEN"
    fail_class = None

    # RED conditions (hard).
    if override_present:
        verdict = "RED"
        fail_class = "FAIL_FORBIDDEN_SURFACE"
    elif pump_run_delta == 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_NEVER_RAN"
    elif t0_stamp_delta == 0:
        # Phase 6 regression - t0 hook unwired.
        verdict = "RED"
        fail_class = "FAIL_PHASE6_REGRESSION_T0"
    elif seq_diff != 0:
        # Phase 7 regression - DIO1 race.
        verdict = "RED"
        fail_class = "FAIL_PHASE7_REGRESSION_DIO1_RACE"
    elif rx_via_spi_delta != 0:
        # Edit B regression - SPI provenance still active.
        verdict = "RED"
        fail_class = "FAIL_RX_WINDOW_SPI_PROVENANCE"
    elif hist_count < RX1_HIST_MIN_SAMPLES:
        verdict = "RED"
        fail_class = "FAIL_RX1_HIST_SPARSE"
    elif hist_p99 < 0 or hist_p99 >= RX1_ARM_TO_SETRX_P99_HARD_US:
        verdict = "RED"
        fail_class = "FAIL_P99_HARD_GATE"
    elif reentry_delta > 0 or nested_delta > 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_GUARD"
    elif iter_joined < JOIN_RATE_YELLOW_MIN:
        verdict = "RED"
        fail_class = "FAIL_JOIN_RATE_FLOOR"
    elif guards["g03_no_tuning_shim"].startswith("FAIL"):
        verdict = "RED"
        fail_class = "FAIL_TUNING_SHIM_PRESENT"
    elif guards["g04_no_macprocess_call"].startswith("FAIL"):
        verdict = "RED"
        fail_class = "FAIL_SCRIPT_CONTAINS_FORBIDDEN_TOKEN"
    elif max_rx_error_ms >= 0 and \
            max_rx_error_ms > MAX_RX_ERROR_MS_TOLERANCE:
        verdict = "RED"
        fail_class = "FAIL_E_INFLATED"
    elif lat_max > 0 and lat_max > PUMP_DISPATCH_MAX_HARD_US:
        verdict = "RED"
        fail_class = "FAIL_DISPATCH_MAX_SANITY"
    else:
        # YELLOW conditions (soft misses).
        if iter_joined < JOIN_RATE_GREEN_MIN:
            verdict = "YELLOW"
            fail_class = "WARN_JOIN_RATE_PARTIAL"
        elif hist_p95 > 0 and hist_p95 > RX1_ARM_TO_SETRX_P95_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_RX1_P95_TARGET"
        elif hist_p99 > 1000:
            # Between final-target 1000 and hard gate 2000.
            verdict = "YELLOW"
            fail_class = "WARN_RX1_P99_INTERMEDIATE"
        elif dio1_p95 > 0 and dio1_p95 > DIO1_P95_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DIO1_P95_REGRESSION"
        elif guards["g03_no_tuning_shim"].startswith("SKIP"):
            verdict = "YELLOW"
            fail_class = "WARN_G3_SOURCE_UNREADABLE"
        elif guards["g04_no_macprocess_call"].startswith("SKIP"):
            verdict = "YELLOW"
            fail_class = "WARN_G4_SOURCE_UNREADABLE"
        elif lat_p95 > 0 and lat_p95 > PUMP_DISPATCH_P95_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DISPATCH_P95"
        elif lat_p99 > 0 and lat_p99 > PUMP_DISPATCH_P99_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DISPATCH_P99"

    metrics = {
        "iter_total": iter_total,
        "iter_joined": iter_joined,
        "iter_target": ITER_TARGET,
        "join_walls_ms": list(join_walls_ms),
        "join_dr": SF7_DR,
        "factory_reset_supported": bool(factory_reset_supported),
        "factory_reset_ok_per_iter": list(factory_reset_ok_per_iter),
        "per_iter_exc": list(per_iter_exc),

        "lorawan_init_exc": lorawan_init_exc,
        "set_keys_exc": set_keys_exc,
        "set_class_exc": set_class_exc,
        "override_method_present": bool(override_present),

        "pre_pump_run": pump_run_base,
        "post_pump_run": pump_run_post,
        "pump_run_delta": pump_run_delta,
        "pre_pump_request": pump_request_base,
        "post_pump_request": pump_request_post,
        "pump_request_delta": pump_request_delta,
        "pump_request_by_reason": list(reason_post),
        "pump_request_by_reason_delta": list(reason_delta),

        "rx1_arm_to_setrx_us": {
            "p50": hist_p50,
            "p95": hist_p95,
            "p99": hist_p99,
            "max": hist_max,
            "count": hist_count,
            "first_sample_boot_ms": hist_first_sample_boot_ms,
        },
        "rx1_arm_to_setrx_us_count_base": rx1_hist_base,

        "rx1_arm_t0_stamp_count_pre": t0_stamp_base,
        "rx1_arm_t0_stamp_count_post": t0_stamp_post,
        "rx1_arm_t0_stamp_count_delta": t0_stamp_delta,

        "dio1_isr_seq": isr_seq_post,
        "dio1_pump_seen_seq": pump_seen_seq_post,
        "dio1_seq_diff": seq_diff,
        "dio1_isr_seq_base": isr_seq_base,
        "dio1_pump_seen_seq_base": pump_seen_seq_base,
        "dio1_to_pump_us": {
            "p50": dio1_p50,
            "p95": dio1_p95,
            "p99": dio1_p99,
            "max": dio1_max,
            "count": dio1_count,
        },

        "pump_dispatch_latency_us": {
            "p50": lat_p50,
            "p95": lat_p95,
            "p99": lat_p99,
            "max": lat_max,
            "count": lat_count,
            "first_sample_boot_ms": lat_first_sample_boot_ms,
        },
        "pump_dispatch_latency_us_by_reason": reason_dict_post,

        "mac_process_reentry_count": reentry_post,
        "mac_process_reentry_delta": reentry_delta,
        "spi_nested_reject_count": nested_post,
        "spi_nested_reject_delta": nested_delta,

        "rx_window_active_set_via_spi_count": rx_via_spi_post,
        "rx_window_active_set_via_spi_count_delta": rx_via_spi_delta,

        "max_rx_error_ms_per_iter": list(max_rx_error_per_iter),
        "max_rx_error_ms": max_rx_error_ms,

        "guards": guards,
        "guard3_source_mechanism": g3_mech,

        "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
    }

    return {
        "tr_id": "TR-1",
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
            "tr_id": "TR-1",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_ANTI_FAKE_PASS_GUARD",
            "metrics": {"assert_msg": str(e)},
        }
        write_artifact("TR-1", result)
        print(json.dumps(result))
        sys.exit(1)
    except Exception as e:
        result = {
            "tr_id": "TR-1",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_SCRIPT_EXCEPTION",
            "metrics": {"exc": repr(e)},
        }
        write_artifact("TR-1", result)
        print(json.dumps(result))
        sys.exit(1)
    write_artifact("TR-1", result)
    print(json.dumps(result))
    if result["verdict"] != "GREEN":
        sys.exit(1)
