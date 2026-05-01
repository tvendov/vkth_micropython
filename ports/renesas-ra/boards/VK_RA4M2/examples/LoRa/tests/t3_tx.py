# T3 — TX тест. Изпраща N номерирани пакета, спазва EU868 duty cycle.
# Стартирай на платка-предавател (DUT-A), докато t3_rx.py тече на DUT-B.
#
# Стартиране:
#   import t3_tx
#   t3_tx.run(lib="A", count=100)
#   t3_tx.run(lib="B", count=100, payload_size=64)

import time
from _log import TestLog
from _radio import Radio
import _config as cfg


def run(lib="A", count=100, payload_size=32, gap_ms=None):
    log = TestLog("T3.tx", lib)
    if gap_ms is None:
        gap_ms = cfg.MIN_TX_GAP_MS    # ETSI duty cycle защита

    log.info("params", count=count, payload_size=payload_size, gap_ms=gap_ms,
             freq_khz=cfg.FREQ_KHZ, sf=cfg.SF, bw=cfg.BW_KHZ, cr=cfg.CR,
             tx_power=cfg.TX_POWER_DBM, sync=hex(cfg.SYNC_WORD))

    r = Radio(lib)
    try:
        r.init()
    except Exception as e:
        log.fail("init", err=str(e))
        return log.done()

    sent = 0
    t_total = 0
    for i in range(count):
        # Генерираме payload с детерминистично съдържание
        head = "TX %05d " % i
        body = head.encode() + bytes([(j + i) & 0xFF for j in range(payload_size - len(head))])
        body = body[:payload_size]

        t0 = time.ticks_ms()
        try:
            r.send(body)
        except Exception as e:
            log.fail("send", index=i, err=str(e))
            break
        toa = time.ticks_diff(time.ticks_ms(), t0)
        t_total += toa
        sent += 1

        if (i + 1) % 10 == 0:
            log.info("progress", sent=sent, last_toa_ms=toa)

        # Duty cycle: чакаме gap_ms между пакети
        if i < count - 1:
            time.sleep_ms(gap_ms)

    log.check("T3.tx.completed", sent == count, sent=sent, expected=count,
              avg_toa_ms=(t_total // sent if sent else 0))

    r.close()
    return log.done()


if __name__ == "__main__":
    run("A", count=20)
