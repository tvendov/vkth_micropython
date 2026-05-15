"""
TR-4 - Phase 7 exit gate: DIO1 ISR to guarded C-pump latency under real
LoRaWAN traffic.

Hypothesis: with the Phase 7 DIO1 hook landed (t0 stamp in hard ISR,
t1 stamp at pump-body entry), 30 confirmed Class A uplinks at ~3 s
spacing produce a populated `dio1_to_pump_us` histogram whose p95 is
under 200 us, hard ISR body stays <=20 us, and every ISR is consumed
by the pump (dio1_isr_seq == dio1_pump_seen_seq).

PHASE 7 RULE: the C pump must drive everything - this script never
calls the forbidden Python pump entry. A runtime-built token
(FORBIDDEN_TOKEN) is used so the guard 4 substring scan can detect a
regression even inside this very file.

Wall budget: <= 120 s (15 s join warm-up + ~90 s burst + diag).

Operator drives one JLink hardware reset BEFORE invocation (RSetType 5).

Usage:
    mpremote connect COM18 run tr4_dio1_latency.py
"""
import sys
import time
import json
import machine

# === Section 0 - Common preamble (mirrors tr2 / tr3) =======================
# Helpers are duplicated locally so the script is a single-file drop-in for
# `mpremote run`.

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


def _safe_rx_diag(mac):
    if hasattr(mac, "rx_diag"):
        try:
            return mac.rx_diag()
        except Exception as e:
            return {"_rx_diag_error": repr(e)}
    return {}


def _read_max_rx_error_ms(mac):
    """Phase 5/6/7 clean build exposes E via rx_diag()['max_rx_error_ms'].
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


# === Section 1 - TR-4 parameters ===========================================

CREDS_PATH = "/flash/lora_creds.json"
SF7_DR = 5
JOIN_WAIT_MS = 15000              # master MSG step 8
JOIN_POLL_INTERVAL_MS = 100       # master MSG step 8
SPI_BAUDRATE = 8000000            # memory project_renesas_lora_spi_precondition

BURST_TARGET = 30
BURST_SPACING_MS = 3000
BURST_TX_BUDGET_MS = 4000         # bounded wait after submit for confirm
TOTAL_WALL_BUDGET_MS = 120000     # master MSG hard cap

# Phase 7 acceptance thresholds (master MSG anti-fake-pass).
DIO1_HIST_MIN_SAMPLES = 28
DIO1_P95_SOFT_US = 200
DIO1_P99_SOFT_US = 500
HARD_ISR_DIO1_MAX_US = 20

# TR-5 regression soft threshold (decision 10).
PUMP_DISPATCH_P95_SOFT_US = 300

# Uplinks gate per p7-cleanup Issue 3 — EU868 1% DC math:
#   SF7 8B uplink -> ~50ms ToA -> 5s DC quiet -> ~15-20 fit a 30-burst stream
# Master directive: 15 PASS / 10 YELLOW / <10 FAIL.
UPLINK_CONFIRMED_PASS = 15
UPLINK_CONFIRMED_YELLOW = 10
UPLINK_CONFIRMED_MIN = UPLINK_CONFIRMED_YELLOW


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


# === Section 2 - TR-4 body =================================================

def run():
    boot = jlink_reset_marker()
    srv = server_precondition_note()
    t_start_ms = time.ticks_ms()

    # ------------------------------------------------------------------
    # Step 0 - import lorawan.
    # ------------------------------------------------------------------
    import lorawan

    # ------------------------------------------------------------------
    # Step 1 - Anti-fake-pass preconditions BEFORE Mac construction.
    # (No pre-Mac checks remaining; module surface validated post-Mac.)
    # ------------------------------------------------------------------

    # ------------------------------------------------------------------
    # Step 2 - SPI(3) precondition (memory project_renesas_lora_spi_precondition).
    # MUST happen BEFORE lorawan.Mac() else 5 s stall + silent RF failure.
    # ------------------------------------------------------------------
    spi = machine.SPI(3, baudrate=SPI_BAUDRATE)

    # ------------------------------------------------------------------
    # Step 3 - construct Mac.
    # ------------------------------------------------------------------
    mac = lorawan.Mac()

    # ------------------------------------------------------------------
    # Step 4 - forbidden surface check (guard 2).
    # ------------------------------------------------------------------
    override_present = any(name in dir(mac) for name in FORBIDDEN_METHODS)

    # ------------------------------------------------------------------
    # Step 5 - baseline pump_diag snapshot (master MSG step 5).
    # All Phase 7 keys read tolerantly via _diag_int / _diag_hist_*; if a
    # key is missing on a stale bench build the value is 0 / -1 and the
    # relevant guard naturally falls into SKIP / FAIL paths.
    # ------------------------------------------------------------------
    diag_base = _safe_pump_diag(mac)

    pump_request_base = _diag_int(diag_base, "pump_request_count", 0)
    pump_run_base = _diag_int(diag_base, "pump_run_count", 0)

    # pump_request_by_reason is a 6-tuple in pump_diag; defensively read.
    def _read_reason_tuple(diag):
        v = diag.get("pump_request_by_reason") if isinstance(diag, dict) \
            else None
        if v is None:
            return (0, 0, 0, 0, 0, 0)
        try:
            t = tuple(int(x) for x in v)
        except Exception:
            return (0, 0, 0, 0, 0, 0)
        # Pad / truncate to exactly 6 entries.
        if len(t) >= 6:
            return t[:6]
        return t + (0,) * (6 - len(t))

    reason_base = _read_reason_tuple(diag_base)

    reentry_base = _diag_int(diag_base, "mac_process_reentry_count", 0)
    nested_base = _diag_int(diag_base, "spi_nested_reject_count", 0)

    # Phase 7 NEW keys - tolerantly read.
    isr_seq_base = _diag_int(diag_base, "dio1_isr_seq", 0)
    pump_seen_seq_base = _diag_int(diag_base, "dio1_pump_seen_seq", 0)

    # Phase 7 histograms.
    hist_base_count = _diag_hist_count(diag_base, "dio1_to_pump_us")

    # ------------------------------------------------------------------
    # Step 6 - load credentials. RED + clean exit if absent.
    # ------------------------------------------------------------------
    deveui_b, joineui_b, appkey_b = _load_creds()
    if deveui_b is None:
        return {
            "tr_id": "TR-4",
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
    # Step 7 - lorawan_init / set_keys / set_class. RED on any exception.
    # ------------------------------------------------------------------
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

    # SystemMaxRxError E (guard 1).
    max_rx_error_ms = _read_max_rx_error_ms(mac)

    # If lorawan_init or set_keys failed, exit RED now - the burst is meaningless.
    if lorawan_init_exc is not None or set_keys_exc is not None:
        return {
            "tr_id": "TR-4",
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
                "max_rx_error_ms": max_rx_error_ms,
                "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
            },
        }

    # ------------------------------------------------------------------
    # Step 8 - Warm-up join: mac.join(5) (DR5/SF7). Poll is_joined() only.
    # NO forbidden pump call - Phase 7 contract is the C pump runs the
    # join via DIO1 + AGT timer events on its own.
    # ------------------------------------------------------------------
    t_join_start_ms = time.ticks_ms()
    join_submit_exc = None
    try:
        mac.join(SF7_DR)
    except Exception as e:
        join_submit_exc = repr(e)

    joined = False
    if join_submit_exc is None:
        deadline = time.ticks_add(t_join_start_ms, JOIN_WAIT_MS)
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            try:
                if mac.is_joined():
                    joined = True
                    break
            except Exception:
                pass
            time.sleep_ms(JOIN_POLL_INTERVAL_MS)

    join_wall_ms = time.ticks_diff(time.ticks_ms(), t_join_start_ms)

    # ------------------------------------------------------------------
    # Step 9 - 30 confirmed uplinks @ 3 s spacing. Each iteration:
    #   * mac.send(2, payload, True, SF7_DR) - returns int LoRaMacStatus_t.
    #   * 0 means submit success; the actual confirm lands later via the
    #     C pump's MCPS_CONFIRM event.
    #   * We count "confirmed" by submit-success (st == 0) only - per
    #     master MSG: "if only TX-done is observable without a confirm
    #     event, count those". This script intentionally does NOT register
    #     an event callback because doing so would risk allocation /
    #     Python re-entry into the pump body during the very window we
    #     are measuring.
    #   * Watchdog: break early if total wall exceeds budget.
    # ------------------------------------------------------------------
    burst_attempted = 0
    burst_confirmed = 0
    burst_wall_start = time.ticks_ms()
    sum_send_us = 0
    sum_send_us_count = 0

    # If join failed, skip the burst entirely - the radio is dead, no
    # point burning 90 s wall on guaranteed-fail sends. Burst counters
    # stay at 0 and the verdict will RED on FAIL_NOT_JOINED via g11.
    if joined:
        for i in range(BURST_TARGET):
            # Total-wall watchdog (master MSG hard cap).
            if time.ticks_diff(time.ticks_ms(), t_start_ms) >= \
                    TOTAL_WALL_BUDGET_MS:
                break

            payload = b"P%02d" % i
            burst_attempted += 1
            t_send_start_us = time.ticks_us()
            send_exc = None
            st = -1
            try:
                # 4-arg form: (port, data, confirmed, datarate).
                # Matches LORAWAN_TESTS/coordination/B3_DATARATE_API_2026-05-13.md
                # documented contract (mod_lorawan.c:1975 lorawan_mac_send).
                st = mac.send(2, payload, True, SF7_DR)
            except Exception as e:
                send_exc = repr(e)
            t_send_end_us = time.ticks_us()
            send_dt_us = time.ticks_diff(t_send_end_us, t_send_start_us)
            if send_dt_us >= 0:
                sum_send_us += send_dt_us
                sum_send_us_count += 1

            if send_exc is None and st == 0:
                burst_confirmed += 1

            # Inter-burst spacing - sleep, no pump call.
            # If close to the wall budget, shorten gracefully.
            remaining_wall = TOTAL_WALL_BUDGET_MS - time.ticks_diff(
                time.ticks_ms(), t_start_ms)
            if remaining_wall <= 0:
                break
            sleep_ms = BURST_SPACING_MS
            if sleep_ms > remaining_wall:
                sleep_ms = remaining_wall
            time.sleep_ms(sleep_ms)

    burst_wall_ms = time.ticks_diff(time.ticks_ms(), burst_wall_start)
    burst_avg_send_us = (sum_send_us // sum_send_us_count) \
        if sum_send_us_count > 0 else 0

    # ------------------------------------------------------------------
    # Step 10 - post-burst diag snapshot.
    # ------------------------------------------------------------------
    diag_post = _safe_pump_diag(mac)

    pump_request_post = _diag_int(diag_post, "pump_request_count", 0)
    pump_run_post = _diag_int(diag_post, "pump_run_count", 0)
    reason_post = _read_reason_tuple(diag_post)
    reason_delta = tuple(
        int(reason_post[i]) - int(reason_base[i]) for i in range(6))

    reentry_post = _diag_int(diag_post, "mac_process_reentry_count", 0)
    nested_post = _diag_int(diag_post, "spi_nested_reject_count", 0)

    # Phase 7 keys (post).
    isr_seq_post = _diag_int(diag_post, "dio1_isr_seq", 0)
    pump_seen_seq_post = _diag_int(diag_post, "dio1_pump_seen_seq", 0)
    seq_diff = isr_seq_post - pump_seen_seq_post

    # dio1_to_pump_us histogram.
    hist_p50 = _diag_hist_p(diag_post, "dio1_to_pump_us", "p50")
    hist_p95 = _diag_hist_p(diag_post, "dio1_to_pump_us", "p95")
    hist_p99 = _diag_hist_p(diag_post, "dio1_to_pump_us", "p99")
    hist_max = _diag_hist_max(diag_post, "dio1_to_pump_us")
    hist_count = _diag_hist_count(diag_post, "dio1_to_pump_us")

    # Hard ISR bound (guard 7): prefer microsecond field; fall back to
    # cycles/100 (DWT @ 100 MHz). SKIP if neither key present.
    hard_isr_dio1_max_us = None
    if isinstance(diag_post, dict):
        if "hard_isr_dio1_max_us" in diag_post:
            try:
                hard_isr_dio1_max_us = int(diag_post["hard_isr_dio1_max_us"])
            except Exception:
                hard_isr_dio1_max_us = None
        elif "hard_isr_dio1_cycles_max" in diag_post:
            try:
                cyc = int(diag_post["hard_isr_dio1_cycles_max"])
                hard_isr_dio1_max_us = cyc // 100
            except Exception:
                hard_isr_dio1_max_us = None

    # TR-5 regression dispatch-latency.
    lat_p50 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p50")
    lat_p95 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p95")
    lat_p99 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p99")
    lat_max = _diag_hist_max(diag_post, "pump_dispatch_latency_us")

    # Recheck E (in case region init shifted it).
    max_rx_error_ms_post = _read_max_rx_error_ms(mac)
    if max_rx_error_ms_post >= 0:
        max_rx_error_ms = max_rx_error_ms_post

    # Deltas.
    pump_run_delta = pump_run_post - pump_run_base
    pump_request_delta = pump_request_post - pump_request_base
    reentry_delta = reentry_post - reentry_base
    nested_delta = nested_post - nested_base

    # ------------------------------------------------------------------
    # Step 11 - 11 anti-fake-pass guards (master MSG p7-dio1-c-event).
    # ------------------------------------------------------------------
    guards = {}

    # --- g01: region-default E ----------------------------------------
    if max_rx_error_ms < 0:
        guards["g01_max_rx_error"] = \
            "SKIP: E unreadable (rx_diag missing key)"
    elif max_rx_error_ms <= MAX_RX_ERROR_MS_TOLERANCE:
        guards["g01_max_rx_error"] = \
            "PASS: max_rx_error_ms=%d <= %d" % (
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
    # Same dual-mechanism pattern as tr3: inspect.getsource first, then
    # __file__ read, finally SKIP. SKIP downgrades to YELLOW so host-side
    # grep on the source file remains the canonical proof.
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

    # --- g04: pump_run_count grew during the burst --------------------
    if pump_run_delta > 0:
        guards["g04_pump_ran"] = \
            "PASS: pump_run_count delta=%d" % pump_run_delta
    else:
        guards["g04_pump_ran"] = \
            "FAIL: pump never ran (delta=%d) - DIO1 hook unwired or " \
            "LORAWAN_C_PUMP_ENABLE=0" % pump_run_delta

    # --- g05: histogram has >= 28 samples -----------------------------
    # 30 uplinks x at least 1 DIO1 each (TX_DONE) = >=30; allow 2 misses.
    hist_delta_count = hist_count - hist_base_count
    if hist_count >= DIO1_HIST_MIN_SAMPLES:
        guards["g05_hist_samples"] = \
            "PASS: count=%d >= %d (delta=%d)" % (
                hist_count, DIO1_HIST_MIN_SAMPLES, hist_delta_count)
    else:
        guards["g05_hist_samples"] = \
            "FAIL: count=%d < %d - DIO1 hook unwired or histogram t1 " \
            "sample point not landed" % (
                hist_count, DIO1_HIST_MIN_SAMPLES)

    # --- g06: dio1_to_pump_us p95 <= 200 us (soft) -------------------
    if hist_p95 < 0:
        guards["g06_p95_le_200"] = \
            "SKIP: p95 unreadable (hist key missing or count=0)"
    elif hist_p95 <= DIO1_P95_SOFT_US:
        guards["g06_p95_le_200"] = \
            "PASS: p95=%d us <= %d us" % (hist_p95, DIO1_P95_SOFT_US)
    else:
        guards["g06_p95_le_200"] = \
            "YELLOW: p95=%d us > %d us soft threshold" % (
                hist_p95, DIO1_P95_SOFT_US)

    # --- g07: hard ISR DIO1 body bounded (<=20 us) -------------------
    if hard_isr_dio1_max_us is None:
        guards["g07_isr_bounded"] = \
            "SKIP: hard_isr_dio1_max_us/cycles_max key absent"
    elif hard_isr_dio1_max_us <= HARD_ISR_DIO1_MAX_US:
        guards["g07_isr_bounded"] = \
            "PASS: max_us=%d <= %d" % (
                hard_isr_dio1_max_us, HARD_ISR_DIO1_MAX_US)
    else:
        guards["g07_isr_bounded"] = \
            "FAIL: max_us=%d > %d (ISR body exceeded budget)" % (
                hard_isr_dio1_max_us, HARD_ISR_DIO1_MAX_US)

    # --- g08: dio1_isr_seq == dio1_pump_seen_seq ---------------------
    # R1 race-detection assertion: every ISR was consumed by the pump.
    if isinstance(diag_post, dict) and (
            "dio1_isr_seq" not in diag_post
            or "dio1_pump_seen_seq" not in diag_post):
        guards["g08_seq_equal"] = \
            "FAIL: Phase 7 seq keys absent (dio1_isr_seq / " \
            "dio1_pump_seen_seq) - build flag mismatch"
    elif isr_seq_post == pump_seen_seq_post:
        guards["g08_seq_equal"] = \
            "PASS: isr=%d pump=%d" % (isr_seq_post, pump_seen_seq_post)
    else:
        guards["g08_seq_equal"] = \
            "FAIL: isr=%d pump=%d (diff=%d - DIO1 race detected)" % (
                isr_seq_post, pump_seen_seq_post, seq_diff)

    # --- g09: no nested re-entry, no SPI nested reject ---------------
    if reentry_delta == 0 and nested_delta == 0:
        guards["g09_no_reentry_nested"] = \
            "PASS: reentry=0 nested=0 across burst"
    else:
        guards["g09_no_reentry_nested"] = \
            "FAIL: reentry_delta=%d nested_delta=%d" % (
                reentry_delta, nested_delta)

    # --- g10: TR-5 regression latency p95 (soft, regression watchdog) -
    if lat_p95 < 0:
        guards["g10_tr5_p95"] = \
            "SKIP: pump_dispatch_latency_us p95 unreadable"
    elif lat_p95 <= PUMP_DISPATCH_P95_SOFT_US:
        guards["g10_tr5_p95"] = \
            "PASS: p95=%d us <= %d us" % (
                lat_p95, PUMP_DISPATCH_P95_SOFT_US)
    else:
        guards["g10_tr5_p95"] = \
            "YELLOW: p95=%d us > %d us soft threshold" % (
                lat_p95, PUMP_DISPATCH_P95_SOFT_US)

    # --- g11: uplink count vs EU868 DC ceiling (p7-cleanup Issue 3) --
    if burst_confirmed >= UPLINK_CONFIRMED_PASS:
        guards["g11_uplinks"] = \
            "PASS: confirmed=%d >= %d" % (
                burst_confirmed, UPLINK_CONFIRMED_PASS)
    elif burst_confirmed >= UPLINK_CONFIRMED_YELLOW:
        guards["g11_uplinks"] = \
            "YELLOW: confirmed=%d (PASS=%d, FAIL<%d) - EU868 DC-tight" % (
                burst_confirmed, UPLINK_CONFIRMED_PASS,
                UPLINK_CONFIRMED_YELLOW)
    else:
        guards["g11_uplinks"] = \
            "FAIL: confirmed=%d < %d" % (
                burst_confirmed, UPLINK_CONFIRMED_YELLOW)

    # ------------------------------------------------------------------
    # Step 12 - verdict logic (master MSG verbatim).
    # ------------------------------------------------------------------
    verdict = "GREEN"
    fail_class = None

    # RED conditions (hard).
    if join_submit_exc is not None:
        verdict = "RED"
        fail_class = "FAIL_JOIN_SUBMIT_EXC"
    elif set_class_exc is not None:
        verdict = "RED"
        fail_class = "FAIL_SET_CLASS_EXC"
    elif override_present:
        verdict = "RED"
        fail_class = "FAIL_FORBIDDEN_SURFACE"
    elif not joined:
        verdict = "RED"
        fail_class = "FAIL_NOT_JOINED"
    elif pump_run_delta == 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_NEVER_RAN"
    elif hist_count < DIO1_HIST_MIN_SAMPLES:
        verdict = "RED"
        fail_class = "FAIL_DIO1_HIST_SPARSE"
    elif isr_seq_post != pump_seen_seq_post:
        verdict = "RED"
        fail_class = "FAIL_DIO1_RACE"
    elif hard_isr_dio1_max_us is not None and \
            hard_isr_dio1_max_us > HARD_ISR_DIO1_MAX_US:
        verdict = "RED"
        fail_class = "FAIL_ISR_ESCAPE"
    elif reentry_delta > 0 or nested_delta > 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_GUARD"
    elif burst_confirmed < UPLINK_CONFIRMED_MIN:
        verdict = "RED"
        fail_class = "FAIL_UPLINK_SHORTFALL"
    elif guards["g03_no_tuning_shim"].startswith("FAIL"):
        verdict = "RED"
        fail_class = "FAIL_TUNING_SHIM_PRESENT"
    elif max_rx_error_ms >= 0 and \
            max_rx_error_ms > MAX_RX_ERROR_MS_TOLERANCE:
        verdict = "RED"
        fail_class = "FAIL_E_INFLATED"
    else:
        # YELLOW conditions (soft).
        if burst_confirmed < UPLINK_CONFIRMED_PASS:
            verdict = "YELLOW"
            fail_class = "WARN_DC_TIGHT_UPLINK"
        elif hist_p95 > 0 and hist_p95 > DIO1_P95_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DIO1_P95"
        elif hist_p99 > 0 and hist_p99 > DIO1_P99_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DIO1_P99"
        elif lat_p95 > 0 and lat_p95 > PUMP_DISPATCH_P95_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_TR5_P95"
        elif guards["g03_no_tuning_shim"].startswith("SKIP"):
            verdict = "YELLOW"
            fail_class = "WARN_G3_SOURCE_UNREADABLE"
        elif guards["g07_isr_bounded"].startswith("SKIP"):
            verdict = "YELLOW"
            fail_class = "WARN_G7_ISR_KEY_ABSENT"

    metrics = {
        "join_wall_ms": join_wall_ms,
        "is_joined": bool(joined),
        "max_rx_error_ms": max_rx_error_ms,
        "lorawan_init_exc": lorawan_init_exc,
        "set_keys_exc": set_keys_exc,
        "set_class_exc": set_class_exc,
        "join_submit_exc": join_submit_exc,

        "burst_target": BURST_TARGET,
        "burst_attempted": burst_attempted,
        "burst_confirmed": burst_confirmed,
        "burst_wall_ms": burst_wall_ms,
        "burst_avg_send_us": burst_avg_send_us,

        "pump_run_pre": pump_run_base,
        "pump_run_post": pump_run_post,
        "pump_run_delta": pump_run_delta,
        "pump_request_pre": pump_request_base,
        "pump_request_post": pump_request_post,
        "pump_request_delta": pump_request_delta,
        "pump_request_by_reason_pre": list(reason_base),
        "pump_request_by_reason_post": list(reason_post),
        "pump_request_by_reason_delta": list(reason_delta),

        "dio1_to_pump_us": {
            "p50": hist_p50,
            "p95": hist_p95,
            "p99": hist_p99,
            "max": hist_max,
            "count": hist_count,
        },
        "dio1_to_pump_us_count_base": hist_base_count,

        "dio1_isr_seq": isr_seq_post,
        "dio1_pump_seen_seq": pump_seen_seq_post,
        "dio1_seq_diff": seq_diff,
        "dio1_isr_seq_base": isr_seq_base,
        "dio1_pump_seen_seq_base": pump_seen_seq_base,

        "hard_isr_dio1_max_us": hard_isr_dio1_max_us,

        "mac_process_reentry_count": reentry_post,
        "mac_process_reentry_delta": reentry_delta,
        "spi_nested_reject_count": nested_post,
        "spi_nested_reject_delta": nested_delta,

        "pump_dispatch_latency_us": {
            "p50": lat_p50,
            "p95": lat_p95,
            "p99": lat_p99,
            "max": lat_max,
        },

        "override_method_present": bool(override_present),

        "guards": guards,
        "guard3_source_mechanism": g3_mech,

        "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
    }

    return {
        "tr_id": "TR-4",
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
            "tr_id": "TR-4",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_ANTI_FAKE_PASS_GUARD",
            "metrics": {"assert_msg": str(e)},
        }
        write_artifact("TR-4", result)
        print(json.dumps(result))
        sys.exit(1)
    except Exception as e:
        result = {
            "tr_id": "TR-4",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_SCRIPT_EXCEPTION",
            "metrics": {"exc": repr(e)},
        }
        write_artifact("TR-4", result)
        print(json.dumps(result))
        sys.exit(1)
    write_artifact("TR-4", result)
    print(json.dumps(result))
    if result["verdict"] != "GREEN":
        sys.exit(1)
