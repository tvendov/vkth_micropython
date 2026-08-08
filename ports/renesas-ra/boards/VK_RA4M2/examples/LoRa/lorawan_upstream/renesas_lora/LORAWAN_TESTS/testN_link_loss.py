"""Test N / HW-8 — antenna detach / link-loss recovery.

Operator-driven test. Script runs a steady-state uplink train at 30s
spacing and prints a checkpoint every cycle. Operator interaction:

  T+0    : start uplinks
  T+60s  : operator UNSCREWS antenna (or wraps in foil — kill RF coupling)
  T+90s  : operator RESTORES antenna
  T+180s : script ends, prints recovery summary

Across the 30s outage window we expect ≤ 1 uplink to reach the server.
Within 60s after restore, ≥ 1 uplink must reach the server again, with
sequential f_cnt_up (no rejoin).

Device-side proofs we check:
  - mlme_confirm event count post-join (rejoin events) — must be 0.
  - mac.process() per-call latency stays bounded (we sample max).
  - 0 panic / 0 SystemExit / REPL alive at end."""
import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_adr(False)

mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)

ring = bounded_ring(128)
mac.set_event_callback(make_ev_cb(ring))

print("=== JOIN ===")
T0 = time.ticks_ms()
mac.join(3)
deadline = time.ticks_add(T0, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    verdict("testN_link_loss", False, reason='join_failed')
    raise SystemExit("JOIN FAILED")
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T0))

T_steady = time.ticks_ms()
mlme_before = sum(1 for _t, tag, _s in ring.since(0) if tag == 'mlme_confirm')
print("  mlme_confirm events at start (post-join window):", mlme_before)

INTERVAL_MS = 30000
CYCLES = 6        # 6 × 30s = 180s total
PHASE = [
    "pre-detach  ",
    "pre-detach  ",
    "*** DETACH NOW *** outage",
    "outage      ",
    "*** RESTORE NOW *** recovery",
    "recovery    ",
]

mac_proc_max = 0
ok_conf = []

T_start = time.ticks_ms()
for i in range(CYCLES):
    while time.ticks_diff(time.ticks_ms(), T_start) < (i + 1) * INTERVAL_MS:
        t1 = time.ticks_ms()
        mac.process()
        d = time.ticks_diff(time.ticks_ms(), t1)
        if d > mac_proc_max: mac_proc_max = d
        time.sleep_ms(20)

    t_tx = time.ticks_ms()
    print("[%d] phase=%s  tx ..." % (i, PHASE[i]))
    st = mac.send(1, b"L%d" % i, False, 3)
    if st != 0:
        print("    send() st=%d" % st)
        ok_conf.append(False); continue

    poll = time.ticks_add(time.ticks_ms(), 8000)
    got_conf = False
    while time.ticks_diff(poll, time.ticks_ms()) > 0:
        t1 = time.ticks_ms()
        mac.process()
        d = time.ticks_diff(time.ticks_ms(), t1)
        if d > mac_proc_max: mac_proc_max = d
        time.sleep_ms(20)
        for _t, tag, status in ring.since(t_tx):
            if tag == 'mcps_confirm':
                got_conf = (status == 0)
                break
        if got_conf: break
    ok_conf.append(got_conf)
    print("    confirm=%s" % got_conf)

mlme_after = sum(1 for _t, tag, _s in ring.since(T_steady) if tag == 'mlme_confirm')
rejoin_attempts = mlme_after  # any post-steady mlme_confirm is a rejoin event
print()
print("=== HW-8 SUMMARY ===")
print("  per-cycle conf:", ok_conf)
print("  mlme_confirm events post-join (rejoin attempts):", rejoin_attempts)
print("  mac.process max latency: %d ms" % mac_proc_max)
print()
print("Server-side cross-check (run separately):")
print("  - count of uplinks for dev_eui in outage window (cycles 2-3) must be <= 1")
print("  - count of uplinks in recovery window (cycles 4-5) must be >= 1")
print("  - f_cnt_up must be sequential across all received uplinks")
print()
print("PASS criteria:")
print("  rejoin attempts == 0")
print("  mac.process max latency < 100 ms")
print("  REPL alive (i.e. this print line executed)")
mac.nvm_store()
verdict("testN_link_loss",
        rejoin_attempts == 0 and mac_proc_max < 100,
        rejoin_attempts=rejoin_attempts, mac_proc_max_ms=mac_proc_max,
        cycles_confirmed=sum(1 for c in ok_conf if c))
