# T1 — Smoke / sanity тестове.
# Една платка е достатъчна. Не изисква peer.
#
# Стартиране:
#   import t1_smoke
#   t1_smoke.run("A")    # тества lora-sx126x
#   t1_smoke.run("B")    # тества micropySX126X
#   t1_smoke.run_both()  # последователно A, после B

import gc
import time
from _log import TestLog
from _radio import Radio


def run(lib="A"):
    log = TestLog("T1.smoke", lib)

    # T1.1: Чист import + конструиране на адаптера
    gc.collect()
    mem_before = gc.mem_free()
    log.info("mem_before_init", mem_free=mem_before)

    try:
        r = Radio(lib)
        log.check("T1.1.constructor", True)
    except Exception as e:
        log.fail("constructor exception", err=str(e))
        return log.done()

    # T1.2: init / begin
    try:
        t0 = time.ticks_ms()
        r.init()
        dt_ms = time.ticks_diff(time.ticks_ms(), t0)
        log.check("T1.2.init", True, init_ms=dt_ms)
    except Exception as e:
        log.fail("init exception", err=str(e))
        return log.done()

    gc.collect()
    mem_after = gc.mem_free()
    used = mem_before - mem_after
    log.info("mem_after_init", mem_free=mem_after, used_bytes=used)

    # T1.3: TX без peer (gateway може да не отговори, само проверяваме че не crash-ва)
    try:
        r.send(b"smoke")
        log.check("T1.3.send_no_peer", True)
    except Exception as e:
        log.fail("send exception", err=str(e))

    # T1.4: RX с timeout (никой не предава → очакваме None)
    try:
        rx = r.recv(timeout_ms=1500)
        # Допустимо и двете: None (нищо чуто) или валиден пакет (от случаен трафик).
        log.check("T1.4.recv_timeout", True, rx=("none" if rx is None else "got"))
    except Exception as e:
        log.fail("recv exception", err=str(e))

    # T1.5: close / sleep
    try:
        r.close()
        log.check("T1.5.close", True)
    except Exception as e:
        log.fail("close exception", err=str(e))

    return log.done()


def run_both():
    rc_a = run("A")
    print()
    rc_b = run("B")
    return rc_a or rc_b


if __name__ == "__main__":
    run_both()
