"""
TR-5 - Pump dispatch latency, reentry, and SPI nested counters.
Phase exit gate for Phase 4 (guarded C pump). Regression gate for >= phase 4.

Hypothesis: the guarded C pump serializes LoRaMacProcess(). p95
pump_dispatch_latency_us <= 300 us (soft per decision 10); reentry and
SPI-nested counters MUST stay at exactly 0 under traffic.

Wall budget: <= 60 s.

Usage:
    mpremote connect COM18 run tr5_pump_scaffold.py

This is the ONLY TR that can run today against the Phase 4 firmware (the
pump body is gated out by LORAWAN_C_PUMP_ENABLE=0, but counters and the
mac.pump_diag() API are live). It does NOT require a working OTAA join.
The pump is exercised via:
  1. 100 x mac.process() in ~1 s loop (counter sanity, no traffic needed).
  2. OPTIONAL: if creds are present, 20 unconfirmed uplinks + a 50-call
     mac.process() burst to try to force the guard to fire. The optional
     traffic block is skipped without failing the TR if join fails.
"""
import sys
import time
import json
import machine
import gc

# === Section 0 - Common preamble (copy verbatim across all TRs) =============

ARTIFACT_DIR = "/QA_RESULTS"
SERVER_PRECHECK_DONE = True

MAX_RX_ERROR_MS_TOLERANCE = 15
RX1_ARM_TO_SETRX_US_P99_HARD_GATE = 2000
FORBIDDEN_METHODS = ("set_join_rx1_max_rx_error_override",)


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


def anti_fake_pass_assertions(mac):
    if hasattr(mac, "get_max_rx_error"):
        e_ms = int(mac.get_max_rx_error())
        assert e_ms <= MAX_RX_ERROR_MS_TOLERANCE, \
            "E inflation leaked: get_max_rx_error()=%d > %d" % (
                e_ms, MAX_RX_ERROR_MS_TOLERANCE)
    for name in FORBIDDEN_METHODS:
        assert name not in dir(mac), \
            "forbidden override API still present: %s" % name
    diag = _safe_pump_diag(mac)
    # Phase 4 dispatch surface MUST exist (this TR is its exit gate).
    assert hasattr(mac, "pump_diag") or hasattr(mac, "rx_window_diag"), \
        "no pump_diag / rx_window_diag method - Phase 4 not landed"
    # Old counter name must be GONE per Phase 4 D2 rename.
    if isinstance(diag, dict):
        legacy_keys = ("sx126x_spi_busy_reject_count",)
        for k in legacy_keys:
            assert k not in diag, "legacy counter name still present: %s" % k
    # MCPS group MUST exist (Phase 4 D3 storage declaration).
    if isinstance(diag, dict):
        for k in ("mcps_indication_queued_count",
                  "mcps_indication_dropped_count"):
            assert k in diag, "missing MCPS counter key: %s" % k
    # Pump guards at zero at start.
    assert _diag_int(diag, "mac_process_reentry_count", 0) == 0
    assert _diag_int(diag, "spi_nested_reject_count", 0) == 0


# === Section 1 - TR-5 parameters ===========================================

CREDS_PATH = "/lora_creds.json"
PROCESS_CALL_BURST_N = 100
PROCESS_CALL_BURST_WINDOW_MS = 1000
UPLINK_OPTIONAL_N = 20
UPLINK_OPTIONAL_SPACING_MS = 500
UPLINK_OPTIONAL_TIMEOUT_MS = 4000
EXTRA_PROCESS_BURST_N = 50
WALL_BUDGET_MS = 60000

PUMP_DISPATCH_P95_SOFT_US = 300
PUMP_DISPATCH_P99_SOFT_US = 1000
PUMP_DISPATCH_MAX_HARD_US = 5000  # sanity ceiling per Phase 4 dispatch


def _load_creds_optional():
    try:
        f = open(CREDS_PATH)
        try:
            d = json.loads(f.read())
        finally:
            f.close()
        return d.get("deveui"), d.get("appeui"), d.get("appkey")
    except Exception:
        return None, None, None


def _process_burst(mac, n, window_ms):
    """Call mac.process() N times spread across window_ms (ticks_diff-bounded)."""
    t_start = time.ticks_ms()
    interval_us = max(1, (window_ms * 1000) // max(1, n))
    actually_called = 0
    next_call_us = time.ticks_us()
    for _ in range(n):
        # Don't run past window_ms.
        if time.ticks_diff(time.ticks_ms(), t_start) > window_ms:
            break
        try:
            mac.process()
            actually_called += 1
        except Exception:
            pass
        next_call_us = time.ticks_add(next_call_us, interval_us)
        # Busy-wait to interval boundary (use ticks_diff, not sleep loop).
        while time.ticks_diff(next_call_us, time.ticks_us()) > 0:
            pass
    return actually_called


def run():
    boot = jlink_reset_marker()
    srv = server_precondition_note()
    t_start_ms = time.ticks_ms()

    import lorawan
    mac = lorawan.Mac()
    anti_fake_pass_assertions(mac)

    # TR-5 exercises the GUARDED PUMP, which observes MAC state
    # transitions. mac.process() is a documented no-op when MAC is
    # not initialised, so we must call lorawan_init() first to make
    # LoRaMacProcess() actually run and trigger request_pump(REASON_PY).
    # We do NOT call mac.join() - TR-5 is pump-only, no RF activity.
    lorawan_init_exc = None
    try:
        mac.lorawan_init()
    except Exception as e:
        lorawan_init_exc = repr(e)

    diag0 = _safe_pump_diag(mac)
    base_request = _diag_int(diag0, "pump_request_count", 0)
    base_run = _diag_int(diag0, "pump_run_count", 0)
    base_reentry = _diag_int(diag0, "mac_process_reentry_count", 0)
    base_nested = _diag_int(diag0, "spi_nested_reject_count", 0)
    base_deferred_spi = _diag_int(diag0, "pump_deferred_spi_busy_count", 0)
    base_deferred_flash = _diag_int(diag0, "pump_deferred_flash_busy_count", 0)
    base_deferred_running = _diag_int(diag0,
                                       "pump_deferred_process_running_count", 0)

    # Phase 4 core check: 100 mac.process() calls in 1 s -> 100 pump requests.
    n_called = _process_burst(mac, PROCESS_CALL_BURST_N,
                              PROCESS_CALL_BURST_WINDOW_MS)

    diag1 = _safe_pump_diag(mac)

    # Optional traffic block - only if creds AND we have wall budget left.
    optional_block = {"attempted": False, "joined": False,
                      "uplinks_sent": 0, "extra_process_calls": 0}
    deveui, appeui, appkey = _load_creds_optional()
    wall_left = WALL_BUDGET_MS - time.ticks_diff(time.ticks_ms(), t_start_ms)
    if deveui and appeui and appkey and wall_left > 35000:
        optional_block["attempted"] = True
        try:
            mac.set_class("A")
            joined = bool(mac.join(deveui, appeui, appkey,
                                   dr=5, timeout=10000))
            optional_block["joined"] = joined
            if joined:
                for i in range(UPLINK_OPTIONAL_N):
                    if time.ticks_diff(time.ticks_ms(), t_start_ms) > \
                            WALL_BUDGET_MS - 5000:
                        break
                    try:
                        mac.send(1, b"P%02d" % i, confirmed=False,
                                 timeout=UPLINK_OPTIONAL_TIMEOUT_MS)
                        optional_block["uplinks_sent"] += 1
                    except Exception:
                        pass
                    time.sleep_ms(UPLINK_OPTIONAL_SPACING_MS)
                # Extra burst to force the guard to fire while traffic active.
                if time.ticks_diff(time.ticks_ms(), t_start_ms) < \
                        WALL_BUDGET_MS - 2000:
                    optional_block["extra_process_calls"] = _process_burst(
                        mac, EXTRA_PROCESS_BURST_N, 500)
        except Exception as e:
            optional_block["exc"] = repr(e)

    diag_final = _safe_pump_diag(mac)

    metrics = {
        "process_burst_called": n_called,
        "process_burst_target": PROCESS_CALL_BURST_N,
        "lorawan_init_exc": lorawan_init_exc,
        "pump_request_base": base_request,
        "pump_request_post":
            _diag_int(diag_final, "pump_request_count", 0),
        "pump_run_base": base_run,
        "pump_run_post":
            _diag_int(diag_final, "pump_run_count", 0),
        "pump_request_delta":
            _diag_int(diag_final, "pump_request_count", 0) - base_request,
        "pump_run_delta":
            _diag_int(diag_final, "pump_run_count", 0) - base_run,
        "mac_process_reentry_delta":
            _diag_int(diag_final, "mac_process_reentry_count", 0)
            - base_reentry,
        "spi_nested_reject_delta":
            _diag_int(diag_final, "spi_nested_reject_count", 0) - base_nested,
        "pump_deferred_spi_busy_delta":
            _diag_int(diag_final, "pump_deferred_spi_busy_count", 0)
            - base_deferred_spi,
        "pump_deferred_flash_busy_delta":
            _diag_int(diag_final, "pump_deferred_flash_busy_count", 0)
            - base_deferred_flash,
        "pump_deferred_process_running_delta":
            _diag_int(diag_final, "pump_deferred_process_running_count", 0)
            - base_deferred_running,
        "pump_dispatch_latency_us": {
            "p50": _diag_hist_p(diag_final, "pump_dispatch_latency_us", "p50"),
            "p95": _diag_hist_p(diag_final, "pump_dispatch_latency_us", "p95"),
            "p99": _diag_hist_p(diag_final, "pump_dispatch_latency_us", "p99"),
            "max": _diag_hist_max(diag_final, "pump_dispatch_latency_us"),
        },
        "optional_traffic_block": optional_block,
        "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
    }

    verdict = "GREEN"
    fail_class = None

    # CRITICAL guards (decision 5).
    if metrics["mac_process_reentry_delta"] != 0 or \
            metrics["spi_nested_reject_delta"] != 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_GUARD"
    # Counters must move (proves request -> run wiring).
    elif metrics["pump_request_delta"] < PROCESS_CALL_BURST_N - 2:
        verdict = "RED"
        fail_class = "FAIL_REQUEST_COUNT_LOW"
    elif metrics["pump_run_delta"] < 1:
        verdict = "RED"
        fail_class = "FAIL_RUN_COUNT_ZERO"
    else:
        # Soft thresholds (decision 10).
        max_us = metrics["pump_dispatch_latency_us"]["max"]
        p95_us = metrics["pump_dispatch_latency_us"]["p95"]
        p99_us = metrics["pump_dispatch_latency_us"]["p99"]
        if max_us > 0 and max_us > PUMP_DISPATCH_MAX_HARD_US:
            verdict = "RED"
            fail_class = "FAIL_DISPATCH_MAX_SANITY"
        elif p95_us > 0 and p95_us > PUMP_DISPATCH_P95_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DISPATCH_P95"
        elif p99_us > 0 and p99_us > PUMP_DISPATCH_P99_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DISPATCH_P99"
        # FAIL_GUARD_NOT_EXERCISED is informational - only flag it if we
        # actually had the traffic block AND deferred_process_running stayed 0.
        if optional_block.get("joined") and \
                optional_block.get("extra_process_calls", 0) > 0 and \
                metrics["pump_deferred_process_running_delta"] == 0:
            if verdict == "GREEN":
                verdict = "YELLOW"
                fail_class = "WARN_GUARD_NOT_EXERCISED"

    return {
        "tr_id": "TR-5",
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
            "tr_id": "TR-5",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_ANTI_FAKE_PASS_GUARD",
            "metrics": {"assert_msg": str(e)},
        }
        write_artifact("TR-5", result)
        print(json.dumps(result))
        sys.exit(1)
    except Exception as e:
        result = {
            "tr_id": "TR-5",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_SCRIPT_EXCEPTION",
            "metrics": {"exc": repr(e)},
        }
        write_artifact("TR-5", result)
        print(json.dumps(result))
        sys.exit(1)
    write_artifact("TR-5", result)
    print(json.dumps(result))
    if result["verdict"] == "RED":
        sys.exit(1)
