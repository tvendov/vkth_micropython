# T4 — Паритет A vs B.
# Сценарий: на DUT-1 пуска t4_parity.dual_rx() — приема пакети с A,
# след това с B, и сравнява RSSI/SNR. На DUT-2 пуска t4_parity.tx_loop()
# със стабилен поток.
#
# Стартиране:
#   На DUT-2: t4_parity.tx_loop(lib="A", duration_s=300)
#   На DUT-1: t4_parity.dual_rx(samples_per_lib=20)

import time
from _log import TestLog
from _radio import Radio
import _config as cfg


def tx_loop(lib="A", duration_s=300, gap_ms=None):
    """Постоянен поток от пакети за паритет тестовете."""
    if gap_ms is None:
        gap_ms = cfg.MIN_TX_GAP_MS

    log = TestLog("T4.parity.tx", lib)
    r = Radio(lib)
    try:
        r.init()
    except Exception as e:
        log.fail("init", err=str(e))
        return log.done()

    deadline = time.ticks_add(time.ticks_ms(), duration_s * 1000)
    sent = 0
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        body = ("PAR %05d " % sent).encode() + b"\x55\xAA" * 12   # ~32 B
        try:
            r.send(body)
            sent += 1
        except Exception as e:
            log.fail("send", err=str(e))
            break
        time.sleep_ms(gap_ms)

    log.info("summary", sent=sent)
    r.close()
    return log.done()


def _measure(lib, samples, recv_timeout_ms):
    log = TestLog("T4.parity.rx", lib)
    r = Radio(lib)
    try:
        r.init()
    except Exception as e:
        log.fail("init", err=str(e))
        log.done()
        return None

    rssi_list = []
    snr_list = []
    deadline_s = samples * (recv_timeout_ms / 1000) + 30
    deadline = time.ticks_add(time.ticks_ms(), int(deadline_s * 1000))

    while len(rssi_list) < samples and time.ticks_diff(deadline, time.ticks_ms()) > 0:
        try:
            rx = r.recv(timeout_ms=recv_timeout_ms)
        except Exception as e:
            log.fail("recv", err=str(e))
            break
        if rx is None:
            continue
        _, rssi, snr = rx
        rssi_list.append(rssi)
        snr_list.append(snr)
        log.info("sample", n=len(rssi_list), rssi=rssi, snr=snr)

    r.close()

    if rssi_list:
        avg_rssi = sum(rssi_list) / len(rssi_list)
        avg_snr  = sum(snr_list)  / len(snr_list)
        rssi_list.sort()
        snr_list.sort()
        med_rssi = rssi_list[len(rssi_list) // 2]
        med_snr  = snr_list[len(snr_list) // 2]
    else:
        avg_rssi = avg_snr = med_rssi = med_snr = 0.0

    log.info("stats",
             n=len(rssi_list),
             avg_rssi=avg_rssi, med_rssi=med_rssi,
             avg_snr=avg_snr, med_snr=med_snr)
    log.done()

    return {
        "n": len(rssi_list),
        "avg_rssi": avg_rssi, "med_rssi": med_rssi,
        "avg_snr":  avg_snr,  "med_snr":  med_snr,
    }


def dual_rx(samples_per_lib=20, recv_timeout_ms=3000):
    """Измерва RSSI/SNR с A, после с B, сравнява.

    ВАЖНО: lib A и lib B заедно не се събират в heap-а на VK_RA4M2.
    След lib A правим soft reset (machine.reset()) и повтаряме за lib B
    в нова Python сесия. Това означава dual_rx() трябва да се извиква
    два пъти ръчно — първият път с lib='A', вторият с lib='B'.
    """
    import sys
    import gc
    import os

    log = TestLog("T4.parity.compare", "A_vs_B")
    state_file = "/flash/.t4_state"

    # Прочитаме предишно записан резултат за lib A (ако има)
    prev_a = None
    try:
        with open(state_file) as f:
            line = f.read().strip()
        prev_a = eval(line)
        print(">>> Намерен предишен lib A резултат:", prev_a)
    except Exception:
        pass

    if prev_a is None:
        # Първо стартиране — измерваме с lib A и записваме
        print(">>> Измерване с lib A (lora-sx126x)...")
        a = _measure("A", samples_per_lib, recv_timeout_ms)
        if a is None:
            log.fail("lib_A_init_failed")
            return log.done()
        with open(state_file, "w") as f:
            f.write(repr(a))
        print()
        print(">>> Lib A резултат записан в", state_file)
        print(">>> Сега направи machine.reset() и пусни dual_rx() пак")
        print(">>> за да измерим с lib B и сравним.")
        log.info("first_run_done", a=a)
        log.done()
        return

    # Второ стартиране — измерваме с lib B
    a = prev_a
    print(">>> Измерване с lib B (micropySX126X)...")
    gc.collect()
    b = _measure("B", samples_per_lib, recv_timeout_ms)

    # Изтриваме state файла
    try:
        os.remove(state_file)
    except Exception:
        pass

    if a is None or b is None:
        log.fail("one_or_both_failed_init")
        return log.done()

    drssi = abs(a["avg_rssi"] - b["avg_rssi"])
    dsnr  = abs(a["avg_snr"]  - b["avg_snr"])

    log.info("comparison",
             a_avg_rssi=a["avg_rssi"], b_avg_rssi=b["avg_rssi"], drssi=drssi,
             a_avg_snr=a["avg_snr"],   b_avg_snr=b["avg_snr"],   dsnr=dsnr,
             a_n=a["n"], b_n=b["n"])

    log.check("T4.parity.rssi_within_2dB", drssi <= 2.0, drssi=drssi)
    log.check("T4.parity.snr_within_1dB",  dsnr  <= 1.0, dsnr=dsnr)
    log.check("T4.parity.both_received",   a["n"] > 0 and b["n"] > 0)

    return log.done()
