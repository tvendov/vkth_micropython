# T5 — Стрес тест: 1000 пакета burst.
# Един от двата DUT-а пуска t5_stress.tx(...), другия t5_stress.rx(...).
# Целта: PER ≤ 1%, без heap fragmentation, без crash.

import gc
import time
from _log import TestLog
from _radio import Radio
import _config as cfg


def tx(lib="A", count=None, payload_size=32, gap_ms=None):
    if count is None:
        count = cfg.STRESS_PACKETS
    if gap_ms is None:
        gap_ms = cfg.MIN_TX_GAP_MS

    log = TestLog("T5.stress.tx", lib)
    log.info("params", count=count, payload_size=payload_size, gap_ms=gap_ms)

    r = Radio(lib)
    try:
        r.init()
    except Exception as e:
        log.fail("init", err=str(e))
        return log.done()

    gc.collect()
    mem_start = gc.mem_free()

    sent = 0
    t_start = time.ticks_ms()
    for i in range(count):
        head = "S %05d " % i
        body = head.encode() + bytes(payload_size - len(head))
        body = body[:payload_size]
        try:
            r.send(body)
            sent += 1
        except Exception as e:
            log.fail("send", index=i, err=str(e))
            break

        if (i + 1) % 100 == 0:
            gc.collect()
            mem_now = gc.mem_free()
            log.info("checkpoint", sent=sent, mem_free=mem_now,
                     leaked=mem_start - mem_now)

        if i < count - 1:
            time.sleep_ms(gap_ms)

    total_s = time.ticks_diff(time.ticks_ms(), t_start) / 1000.0
    gc.collect()
    mem_end = gc.mem_free()

    log.info("summary",
             sent=sent, total_s=total_s,
             rate_pps=(sent / total_s if total_s > 0 else 0),
             mem_start=mem_start, mem_end=mem_end,
             leaked=mem_start - mem_end)

    log.check("T5.stress.tx.completed", sent == count, sent=sent, expected=count)
    log.check("T5.stress.tx.no_leak",
              (mem_start - mem_end) < 4096,
              leaked_bytes=mem_start - mem_end)

    r.close()
    return log.done()


def rx(lib="A", count=None, total_timeout_s=None, recv_timeout_ms=3000):
    if count is None:
        count = cfg.STRESS_PACKETS
    if total_timeout_s is None:
        # Дайте малко резерв над очакваното време при 1 s gap
        total_timeout_s = max(120, count * 2)

    log = TestLog("T5.stress.rx", lib)
    log.info("params", count=count, total_timeout_s=total_timeout_s)

    r = Radio(lib)
    try:
        r.init()
    except Exception as e:
        log.fail("init", err=str(e))
        return log.done()

    gc.collect()
    mem_start = gc.mem_free()

    received = 0
    crc_errors = 0
    seen = set()
    duplicates = 0
    rssi_sum = 0.0
    snr_sum = 0.0
    deadline = time.ticks_add(time.ticks_ms(), total_timeout_s * 1000)

    while received < count and time.ticks_diff(deadline, time.ticks_ms()) > 0:
        try:
            rx_pkt = r.recv(timeout_ms=recv_timeout_ms)
        except Exception as e:
            log.fail("recv", err=str(e))
            break
        if rx_pkt is None:
            continue

        payload, rssi, snr = rx_pkt
        received += 1
        rssi_sum += rssi
        snr_sum += snr

        idx = -1
        try:
            if payload.startswith(b"S ") and len(payload) >= 8:
                idx = int(payload[2:7])
                if idx in seen:
                    duplicates += 1
                else:
                    seen.add(idx)
        except Exception:
            crc_errors += 1

        if received % 100 == 0:
            gc.collect()
            mem_now = gc.mem_free()
            log.info("checkpoint", received=received, mem_free=mem_now,
                     leaked=mem_start - mem_now)

    gc.collect()
    mem_end = gc.mem_free()
    per = 1.0 - (received / count) if count else 0.0

    log.info("summary",
             received=received, expected=count, duplicates=duplicates,
             unique=len(seen), per_pct=per * 100.0,
             avg_rssi=(rssi_sum / received) if received else 0.0,
             avg_snr=(snr_sum / received) if received else 0.0,
             mem_start=mem_start, mem_end=mem_end,
             leaked=mem_start - mem_end)

    log.check("T5.stress.rx.per_le_1pct", per <= 0.01, per_pct=per * 100.0)
    log.check("T5.stress.rx.no_leak",
              (mem_start - mem_end) < 4096,
              leaked_bytes=mem_start - mem_end)

    r.close()
    return log.done()
