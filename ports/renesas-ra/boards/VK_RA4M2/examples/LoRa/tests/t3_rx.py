# T3 — RX тест. Слуша до N пакета или до изтичане на общ timeout.
# Стартирай на платка-приемник (DUT-B), докато t3_tx.py тече на DUT-A.
#
# Стартиране:
#   import t3_rx
#   t3_rx.run(lib="A", count=100, total_timeout_s=180)
#   t3_rx.run(lib="B", count=100)

import time
from _log import TestLog
from _radio import Radio
import _config as cfg


def run(lib="A", count=100, total_timeout_s=180, recv_timeout_ms=3000):
    log = TestLog("T3.rx", lib)
    log.info("params", count=count, total_timeout_s=total_timeout_s,
             freq_khz=cfg.FREQ_KHZ, sf=cfg.SF, bw=cfg.BW_KHZ, cr=cfg.CR,
             sync=hex(cfg.SYNC_WORD))

    r = Radio(lib)
    try:
        r.init()
    except Exception as e:
        log.fail("init", err=str(e))
        return log.done()

    received = 0
    rssi_sum = 0.0
    snr_sum = 0.0
    seen_indices = set()
    duplicates = 0
    deadline = time.ticks_add(time.ticks_ms(), total_timeout_s * 1000)

    while received < count and time.ticks_diff(deadline, time.ticks_ms()) > 0:
        try:
            rx = r.recv(timeout_ms=recv_timeout_ms)
        except Exception as e:
            log.fail("recv", err=str(e))
            break
        if rx is None:
            log.info("recv_timeout")
            continue

        payload, rssi, snr = rx
        received += 1
        rssi_sum += rssi
        snr_sum  += snr

        # Парсваме номера на пакета (формат "TX 00042 ...")
        idx = -1
        try:
            if payload.startswith(b"TX ") and len(payload) >= 8:
                idx = int(payload[3:8])
                if idx in seen_indices:
                    duplicates += 1
                else:
                    seen_indices.add(idx)
        except Exception:
            pass

        log.info("rx", n=received, idx=idx, rssi=rssi, snr=snr, len=len(payload))

    avg_rssi = rssi_sum / received if received else 0.0
    avg_snr  = snr_sum  / received if received else 0.0
    per = 1.0 - (received / count) if count else 0.0

    log.info("summary",
             received=received, expected=count, duplicates=duplicates,
             unique=len(seen_indices),
             avg_rssi=avg_rssi, avg_snr=avg_snr,
             per_pct=per * 100.0)

    log.check("T3.rx.received_at_least_99pct",
              received >= int(count * 0.99),
              received=received, expected=count)
    log.check("T3.rx.no_duplicates", duplicates == 0, duplicates=duplicates)

    r.close()
    return log.done()


if __name__ == "__main__":
    run("A", count=20, total_timeout_s=60)
