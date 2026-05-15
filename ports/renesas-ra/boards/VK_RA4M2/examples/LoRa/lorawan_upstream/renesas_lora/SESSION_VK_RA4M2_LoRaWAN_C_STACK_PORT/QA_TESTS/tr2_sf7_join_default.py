"""
TR-2 - SF7/DR5 single-shot OTAA join (Phase 5 exit gate, clean port).

Hypothesis: Phase 5 wiring (LoRaMacProcess() un-gated inside
lorawan_driver_pump_run, mac_process_notify -> request_pump(REASON_NOTIFY),
LORAWAN_C_PUMP_ENABLE=1) produces a successful SF7 OTAA join under clean
upstream defaults (SystemMaxRxError=10 ms, region MinRxSymbols), with the
guarded C pump actually running and zero pump re-entrancy / SPI nested
rejects.

Single-shot, single-run. Operator drives one JLink hardware reset BEFORE
invocation (RSetType 5). The script never retries, never tunes timing.

Wall budget: <= 100 s (typical SF7 join < 8 s; +12 s join wait + 5 s diag).

Usage:
    mpremote connect COM18 run tr2_sf7_join_default.py
"""
import sys
import time
import json
import machine

# === Section 0 - Common preamble (copied from tr5_pump_scaffold.py) =========
# Helpers are duplicated locally so the script is a single-file drop-in for
# `mpremote run`. Names + behaviour mirror tr5 exactly.

ARTIFACT_DIR = "/flash/QA_RESULTS"
SERVER_PRECHECK_DONE = True

MAX_RX_ERROR_MS_TOLERANCE = 15
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


def _safe_rx_diag(mac):
    if hasattr(mac, "rx_diag"):
        try:
            return mac.rx_diag()
        except Exception as e:
            return {"_rx_diag_error": repr(e)}
    return {}


def _safe_stats(mac):
    if hasattr(mac, "stats"):
        try:
            return mac.stats()
        except Exception as e:
            return {"_stats_error": repr(e)}
    return {}


def _read_max_rx_error_ms(mac):
    """Phase 5 clean build exposes E via rx_diag()['max_rx_error_ms'].
    There is NO get_max_rx_error() binding (verified mod_lorawan.c grep)."""
    d = _safe_rx_diag(mac)
    if isinstance(d, dict) and "max_rx_error_ms" in d:
        try:
            return int(d["max_rx_error_ms"])
        except Exception:
            pass
    return -1


# === Section 1 - TR-2 parameters ============================================

CREDS_PATH = "/flash/lora_creds.json"
SF7_DR = 5
JOIN_WAIT_MS = 12000           # master MSG step 7
JOIN_POLL_INTERVAL_MS = 20     # tight pump cadence while waiting JoinAccept
SPI_BAUDRATE = 8000000         # mandatory pre-init per memory project_renesas_lora_spi_precondition

# Decision 10 soft thresholds (mirrored from tr5).
PUMP_DISPATCH_P95_SOFT_US = 300
PUMP_DISPATCH_P99_SOFT_US = 1000
PUMP_DISPATCH_MAX_HARD_US = 5000


def _load_creds():
    """Returns (deveui_b, joineui_b, appkey_b) or (None, None, None).

    Expected JSON shape:
      {"deveui": "<16 hex>", "appeui": "<16 hex>", "appkey": "<32 hex>"}
    (Key name `appeui` is the JoinEUI in LoRaWAN 1.0.4 nomenclature.)"""
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


def _guard3_source_check():
    """Guard 3: prove this script never calls the tuning shims.
    Try inspect.getsource first; fall back to reading __file__; if neither
    works, return ('skipped', None).  Returns (mechanism, src_or_None)."""
    # Try inspect first.
    try:
        import inspect
        src = inspect.getsource(sys.modules[__name__])
        return "inspect", src
    except Exception:
        pass
    # Fallback: read this script via __file__.
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


# === Section 2 - TR-2 body =================================================

def run():
    boot = jlink_reset_marker()
    srv = server_precondition_note()
    t_start_ms = time.ticks_ms()

    # ------------------------------------------------------------------
    # Step 0 - import lorawan first so the module surface is loaded.
    # ------------------------------------------------------------------
    import lorawan

    # ------------------------------------------------------------------
    # Step 1 - SPI(3) precondition (memory project_renesas_lora_spi_precondition)
    # MUST happen BEFORE lorawan.Mac() else 5 s stall + silent RF failure.
    # ------------------------------------------------------------------
    spi = machine.SPI(3, baudrate=SPI_BAUDRATE)

    # ------------------------------------------------------------------
    # Step 2 - construct Mac. Constructor does NOT init the stack; it
    # only allocates the singleton + registers ISR primitives.
    # ------------------------------------------------------------------
    mac = lorawan.Mac()

    # ------------------------------------------------------------------
    # Step 3 - clean-import / forbidden surface checks BEFORE lorawan_init.
    # Guard 2 (forbidden override surface): clean port must NOT export it.
    # Guard 8 (Phase 4 rename): old SPI reject counter name must be gone.
    # ------------------------------------------------------------------
    override_present = any(name in dir(mac) for name in FORBIDDEN_METHODS)

    stats_pre = _safe_stats(mac)
    old_counter_present = False
    if isinstance(stats_pre, dict):
        # The counter could live in any of the documented top-level groups
        # or directly under the dict (we are paranoid because Phase 4
        # renamed it under spi.* + removed the legacy alias).
        for grp_name, grp in stats_pre.items():
            if grp_name == "sx126x_spi_busy_reject_count":
                old_counter_present = True
                break
            if isinstance(grp, dict) and "sx126x_spi_busy_reject_count" in grp:
                old_counter_present = True
                break

    # ------------------------------------------------------------------
    # Step 4 - explicit lorawan_init() (clean Phase 5 build is not auto-init;
    # Mac() only constructs primitives — see mod_lorawan.c:1002).
    # ------------------------------------------------------------------
    lorawan_init_rc = None
    lorawan_init_exc = None
    try:
        lorawan_init_rc = mac.lorawan_init()
    except Exception as e:
        lorawan_init_exc = repr(e)

    # ------------------------------------------------------------------
    # Step 5 - baseline pump_diag (guard 4 expects pump_run_count > 0 AFTER).
    # ------------------------------------------------------------------
    diag_base = _safe_pump_diag(mac)
    pump_request_base = _diag_int(diag_base, "pump_request_count", 0)
    pump_run_base = _diag_int(diag_base, "pump_run_count", 0)
    reentry_base = _diag_int(diag_base, "mac_process_reentry_count", 0)
    nested_base = _diag_int(diag_base, "spi_nested_reject_count", 0)

    # ------------------------------------------------------------------
    # Step 6 - read SystemMaxRxError E (guard 1). Clean upstream EU868
    # default is 10 ms; tolerance window is <=15.
    # ------------------------------------------------------------------
    max_rx_error_ms = _read_max_rx_error_ms(mac)

    # ------------------------------------------------------------------
    # Step 7 - load credentials. RED + clean exit if absent.
    # ------------------------------------------------------------------
    deveui_b, joineui_b, appkey_b = _load_creds()
    if deveui_b is None:
        return {
            "tr_id": "TR-2",
            "iso": now_iso(),
            "boot": boot,
            "server_precheck": srv,
            "verdict": "RED",
            "fail_class": "FAIL_NO_CREDS",
            "metrics": {
                "creds_path": CREDS_PATH,
                "lorawan_init_rc": lorawan_init_rc,
                "lorawan_init_exc": lorawan_init_exc,
                "max_rx_error_ms": max_rx_error_ms,
                "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
            },
        }

    # ------------------------------------------------------------------
    # Step 8 - set keys + class A. Single binding mac.set_keys(deveui,
    # joineui, appkey) — see mod_lorawan.c:1066. There is NO separate
    # set_dev_eui / set_join_eui / set_app_key in this build.
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
    # Step 9 - single-shot OTAA join at DR5 (SF7).
    # mac.join(dr) is ASYNC: it returns the LoRaMacStatus_t immediately,
    # then the JoinAccept arrives later via mlme_confirm which flips
    # self->joined inside the C side. We drive mac.process() until either
    # is_joined() is True or JOIN_WAIT_MS elapses.
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
        # Drive the pump until joined or budget exhausted. Each iteration
        # calls mac.process() (cheap; the C pump batches actual work).
        while time.ticks_diff(time.ticks_ms(), t_join_start_ms) < JOIN_WAIT_MS:
            try:
                mac.process()
            except Exception:
                pass
            try:
                if mac.is_joined():
                    joined = True
                    break
            except Exception:
                pass
            # Short, bounded busy gap so we do not flood the bus.
            t_wait_until = time.ticks_add(time.ticks_ms(),
                                          JOIN_POLL_INTERVAL_MS)
            while time.ticks_diff(t_wait_until, time.ticks_ms()) > 0:
                pass

    t_join_end_ms = time.ticks_ms()
    join_wall_ms = time.ticks_diff(t_join_end_ms, t_join_start_ms)

    # ------------------------------------------------------------------
    # Step 10 - post-join diagnostics.
    # ------------------------------------------------------------------
    diag_post = _safe_pump_diag(mac)
    pump_request_post = _diag_int(diag_post, "pump_request_count", 0)
    pump_run_post = _diag_int(diag_post, "pump_run_count", 0)
    reentry_post = _diag_int(diag_post, "mac_process_reentry_count", 0)
    nested_post = _diag_int(diag_post, "spi_nested_reject_count", 0)

    lat_p50 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p50")
    lat_p95 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p95")
    lat_p99 = _diag_hist_p(diag_post, "pump_dispatch_latency_us", "p99")
    lat_max = _diag_hist_max(diag_post, "pump_dispatch_latency_us")

    # Recheck E after join (in case region init shifted it). guard 1 binds
    # on the post value because that is what the join just used.
    max_rx_error_ms_post = _read_max_rx_error_ms(mac)
    if max_rx_error_ms_post >= 0:
        max_rx_error_ms = max_rx_error_ms_post

    # ------------------------------------------------------------------
    # Step 11 - 8 anti-fake-pass guards (verbatim semantics per master).
    # ------------------------------------------------------------------
    guards = {}

    # Guard 1: region-default E (<=15 ms tolerance per decision 13).
    if max_rx_error_ms < 0:
        guards["g1_max_rx_error"] = "FAIL: E unreadable (rx_diag missing key)"
    elif max_rx_error_ms <= MAX_RX_ERROR_MS_TOLERANCE:
        guards["g1_max_rx_error"] = \
            "PASS: max_rx_error_ms=%d <= %d" % (
                max_rx_error_ms, MAX_RX_ERROR_MS_TOLERANCE)
    else:
        guards["g1_max_rx_error"] = \
            "FAIL: E inflated max_rx_error_ms=%d > %d (clean import suspect)" \
            % (max_rx_error_ms, MAX_RX_ERROR_MS_TOLERANCE)

    # Guard 2: forbidden override surface absent.
    if not override_present:
        guards["g2_no_override"] = "PASS: no forbidden override method present"
    else:
        guards["g2_no_override"] = \
            "FAIL: override surface still present in dir(mac)"

    # Guard 3: this script never calls the tuning shims.
    g3_mech, g3_src = _guard3_source_check()
    if g3_src is None:
        # We literally cannot read our own source. We accept this as
        # FAIL because the master rule is binary (decision 13 / v3 §1.2 (1)).
        guards["g3_no_tuning_shim"] = \
            "FAIL: cannot read script source via inspect or __file__"
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

    # Guard 4: pump actually ran during the test window.
    pump_run_delta = pump_run_post - pump_run_base
    if pump_run_delta > 0:
        guards["g4_pump_ran"] = \
            "PASS: pump_run_count delta=%d" % pump_run_delta
    else:
        guards["g4_pump_ran"] = \
            "FAIL: pump never ran (delta=%d) - LORAWAN_C_PUMP_ENABLE may be 0" \
            % pump_run_delta

    # Guard 5: is_joined() flipped True (side-effect proof of LoRaMacProcess
    # body executing).
    if joined:
        guards["g5_is_joined"] = "PASS: is_joined()=True"
    else:
        guards["g5_is_joined"] = \
            "FAIL: not joined after %d ms - pump ran but JoinAccept not " \
            "processed (flag gating bug?)" % join_wall_ms

    # Guard 6: no nested re-entry, no SPI nested reject.
    reentry_delta = reentry_post - reentry_base
    nested_delta = nested_post - nested_base
    if reentry_delta == 0 and nested_delta == 0:
        guards["g6_no_reentry_nested"] = \
            "PASS: reentry=0 nested=0 across join"
    else:
        guards["g6_no_reentry_nested"] = \
            "FAIL: reentry_delta=%d nested_delta=%d" % (
                reentry_delta, nested_delta)

    # Guard 7: TR-5 regression - latency p95 soft (decision 10).
    if lat_p95 > 0 and lat_p95 > PUMP_DISPATCH_P95_SOFT_US:
        guards["g7_latency_p95_soft"] = \
            "YELLOW: p95=%d us > %d us soft threshold" % (
                lat_p95, PUMP_DISPATCH_P95_SOFT_US)
    else:
        guards["g7_latency_p95_soft"] = \
            "PASS: p95=%d us (soft<=%d)" % (
                lat_p95, PUMP_DISPATCH_P95_SOFT_US)

    # Guard 8: Phase 4 rename held - old sx126x_spi_busy_reject_count gone.
    if not old_counter_present:
        guards["g8_old_counter_absent"] = \
            "PASS: legacy sx126x_spi_busy_reject_count not present"
    else:
        guards["g8_old_counter_absent"] = \
            "FAIL: legacy sx126x_spi_busy_reject_count still present " \
            "(Phase 4 rename regressed)"

    # ------------------------------------------------------------------
    # Step 12 - verdict logic.
    # ------------------------------------------------------------------
    verdict = "GREEN"
    fail_class = None

    # RED conditions (hard).
    if reentry_delta > 0 or nested_delta > 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_GUARD"
    elif pump_run_delta == 0:
        verdict = "RED"
        fail_class = "FAIL_PUMP_NEVER_RAN"
    elif not joined:
        verdict = "RED"
        fail_class = "FAIL_NOT_JOINED"
    elif lat_max > 0 and lat_max > PUMP_DISPATCH_MAX_HARD_US:
        verdict = "RED"
        fail_class = "FAIL_DISPATCH_MAX_SANITY"
    elif override_present:
        verdict = "RED"
        fail_class = "FAIL_FORBIDDEN_SURFACE"
    elif max_rx_error_ms >= 0 and \
            max_rx_error_ms > MAX_RX_ERROR_MS_TOLERANCE:
        verdict = "RED"
        fail_class = "FAIL_E_INFLATED"
    elif old_counter_present:
        verdict = "RED"
        fail_class = "FAIL_PHASE4_RENAME_REGRESSED"
    elif guards["g3_no_tuning_shim"].startswith("FAIL"):
        verdict = "RED"
        fail_class = "FAIL_TUNING_SHIM_PRESENT"
    else:
        # YELLOW conditions (soft latency only).
        if lat_p95 > 0 and lat_p95 > PUMP_DISPATCH_P95_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DISPATCH_P95"
        elif lat_p99 > 0 and lat_p99 > PUMP_DISPATCH_P99_SOFT_US:
            verdict = "YELLOW"
            fail_class = "WARN_DISPATCH_P99"

    metrics = {
        "join_wall_ms": join_wall_ms,
        "join_dr": SF7_DR,
        "join_submit_rc": join_submit_rc,
        "join_submit_exc": join_submit_exc,
        "join_returned": (join_submit_rc is not None),
        "is_joined": bool(joined),
        "max_rx_error_ms": max_rx_error_ms,
        "override_method_present": bool(override_present),
        "lorawan_init_rc": lorawan_init_rc,
        "lorawan_init_exc": lorawan_init_exc,
        "set_keys_exc": set_keys_exc,
        "set_class_exc": set_class_exc,
        "pump_request_base": pump_request_base,
        "pump_run_base": pump_run_base,
        "pump_request_post": pump_request_post,
        "pump_run_post": pump_run_post,
        "pump_request_count": pump_request_post,
        "pump_run_count": pump_run_post,
        "pump_request_delta": pump_request_post - pump_request_base,
        "pump_run_delta": pump_run_delta,
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
        "guards": guards,
        "guard3_source_mechanism": g3_mech,
        "old_counter_present_at_start": bool(old_counter_present),
        "wall_ms": time.ticks_diff(time.ticks_ms(), t_start_ms),
    }

    return {
        "tr_id": "TR-2",
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
            "tr_id": "TR-2",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_ANTI_FAKE_PASS_GUARD",
            "metrics": {"assert_msg": str(e)},
        }
        write_artifact("TR-2", result)
        print(json.dumps(result))
        sys.exit(1)
    except Exception as e:
        result = {
            "tr_id": "TR-2",
            "iso": now_iso(),
            "verdict": "RED",
            "fail_class": "FAIL_SCRIPT_EXCEPTION",
            "metrics": {"exc": repr(e)},
        }
        write_artifact("TR-2", result)
        print(json.dumps(result))
        sys.exit(1)
    write_artifact("TR-2", result)
    print(json.dumps(result))
    if result["verdict"] != "GREEN":
        sys.exit(1)
