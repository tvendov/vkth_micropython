# T3.pingpong — двупосочен тест с измерване на round-trip latency.
# Една платка работи в режим "master" (изпраща ping, чака pong),
# другата в "slave" (чака ping, отговаря с pong).
#
# Стартиране на DUT-A (master):
#   import t3_pingpong
#   t3_pingpong.master(lib="A", count=50)
#
# Стартиране на DUT-B (slave):
#   import t3_pingpong
#   t3_pingpong.slave(lib="A")           # или lib="B"

import time
from _log import TestLog
from _radio import Radio
import _config as cfg


def master(lib="A", count=50, ping_gap_ms=None, pong_timeout_ms=2000):
    log = TestLog("T3.pingpong.master", lib)
    if ping_gap_ms is None:
        ping_gap_ms = cfg.MIN_TX_GAP_MS

    r = Radio(lib)
    try:
        r.init()
    except Exception as e:
        log.fail("init", err=str(e))
        return log.done()

    successes = 0
    rtts = []

    for i in range(count):
        ping = ("PING %05d" % i).encode()
        t0 = time.ticks_ms()
        try:
            r.send(ping)
        except Exception as e:
            log.fail("send_ping", i=i, err=str(e))
            break

        # Чакаме pong
        try:
            rx = r.recv(timeout_ms=pong_timeout_ms)
        except Exception as e:
            log.fail("recv_pong", i=i, err=str(e))
            break
        rtt = time.ticks_diff(time.ticks_ms(), t0)

        if rx is None:
            log.info("pong_timeout", i=i, rtt_ms=rtt)
        else:
            payload, rssi, snr = rx
            expected = ("PONG %05d" % i).encode()
            if payload == expected:
                successes += 1
                rtts.append(rtt)
                log.info("rtt", i=i, rtt_ms=rtt, rssi=rssi, snr=snr)
            else:
                log.info("pong_mismatch", i=i, got=payload, expected=expected)

        if i < count - 1:
            time.sleep_ms(ping_gap_ms)

    if rtts:
        rtts.sort()
        log.info("rtt_stats",
                 n=len(rtts),
                 min_ms=rtts[0],
                 max_ms=rtts[-1],
                 median_ms=rtts[len(rtts)//2],
                 avg_ms=sum(rtts)//len(rtts))

    log.check("T3.pingpong.master.success_rate",
              successes >= int(count * 0.95),
              successes=successes, expected=count)

    r.close()
    return log.done()


def slave(lib="A", duration_s=600):
    log = TestLog("T3.pingpong.slave", lib)
    r = Radio(lib)
    try:
        r.init()
    except Exception as e:
        log.fail("init", err=str(e))
        return log.done()

    answered = 0
    deadline = time.ticks_add(time.ticks_ms(), duration_s * 1000)

    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        try:
            rx = r.recv(timeout_ms=5000)
        except Exception as e:
            log.fail("recv", err=str(e))
            break
        if rx is None:
            continue

        payload, rssi, snr = rx
        if payload.startswith(b"PING ") and len(payload) >= 10:
            idx = payload[5:10]
            pong = b"PONG " + idx
            try:
                r.send(pong)
                answered += 1
                log.info("pong", idx=idx.decode(), rssi=rssi, snr=snr)
            except Exception as e:
                log.fail("send_pong", err=str(e))
                break

    log.info("summary", answered=answered)
    log.check("T3.pingpong.slave.responded", answered > 0, answered=answered)
    r.close()
    return log.done()


if __name__ == "__main__":
    # По подразбиране стартираме като slave (по-безопасно — не TX-ва без peer)
    slave("A", duration_s=120)
