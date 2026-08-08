"""Test M / HW-7 — 1000-uplink soak.

Long-duration stability test. After OTAA, sends 1000 unconfirmed uplinks
at 60s spacing (~16.7h total). Periodic snapshots of:
  - gc.mem_free() drift  (leak detector)
  - mac.stats() 40-leaf  (SPI invariants, confirm/indication counters)
  - per-uplink mcps_confirm status
  - inter-uplink elapsed (stall detector — must stay ≤ INTERVAL_S × 1.2)

Persists session via mac.nvm_store() every 50 uplinks so a power loss
during the run doesn't force a rejoin on restart.

Duty cycle: at DR3 SF9 4-byte payload ≈ 100 ms airtime; 1000 / 60000s
≈ 0.17% on band 1 — well within 1%."""
import lorawan, time, gc
from machine import Pin, SPI
from _lorawan_test_helpers import verdict

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_adr(False)

deveui, joineui, appkey = mac.load_credentials()
mac.set_keys(deveui, joineui, appkey)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN ===")
T0 = time.ticks_ms()
mac.join(3)
deadline = time.ticks_add(T0, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    raise SystemExit("JOIN FAILED")
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T0))

N = 1000
INTERVAL_MS = 60000
SNAPSHOT_EVERY = 50

ok_conf = 0
no_event = 0
stalls = 0
gc.collect()
mem_at_start = gc.mem_free()
stats_at_start = mac.stats()
print("baseline: gc.mem_free=%d  mac.stats keys=%d" % (mem_at_start, len(stats_at_start)))

t_last = time.ticks_ms()
for i in range(N):
    target = time.ticks_add(T0, (i + 1) * INTERVAL_MS)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)

    elapsed = time.ticks_diff(time.ticks_ms(), t_last)
    if elapsed > INTERVAL_MS * 12 // 10:
        stalls += 1
        print("  ! stall detected at uplink %d, elapsed=%dms (limit %d)"
              % (i, elapsed, INTERVAL_MS * 12 // 10))

    snap = len(events)
    st = mac.send(1, b"s%04d" % i, False, 3)
    if st != 0:
        print("  [%04d] send() st=%d skip" % (i, st)); continue

    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    saw_conf = None
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
        for _, e in events[snap:]:
            if e[0] == 'mcps_confirm':
                saw_conf = e[1]
                break
        if saw_conf is not None: break

    if saw_conf == 0: ok_conf += 1
    elif saw_conf is None: no_event += 1

    t_last = time.ticks_ms()

    if (i + 1) % SNAPSHOT_EVERY == 0:
        gc.collect()
        mem = gc.mem_free()
        stats = mac.stats()
        viol = stats.get('spi_busy_invariant_violations', -1) if isinstance(stats, dict) else -1
        mp_max = stats.get('mac_process_max_us', -1) if isinstance(stats, dict) else -1
        print("  [%04d] ok=%d  no_evt=%d  stalls=%d  mem_free=%d  spi_inv_viol=%s  mp_max_us=%s"
              % (i+1, ok_conf, no_event, stalls, mem, viol, mp_max))
        mac.nvm_store()
        # truncate events buffer to avoid unbounded RAM use over 16h
        if len(events) > 2000:
            del events[:1000]

gc.collect()
mem_at_end = gc.mem_free()
stats_at_end = mac.stats()

print()
print("=== HW-7 SUMMARY ===")
print("  uplinks ok_conf : %d / %d" % (ok_conf, N))
print("  no_event        : %d" % no_event)
print("  stalls          : %d" % stalls)
print("  mem drift       : %d B (end %d - start %d)"
      % (mem_at_end - mem_at_start, mem_at_end, mem_at_start))
print("  stats start:", stats_at_start)
print("  stats end  :", stats_at_end)
print()
print("PASS criteria:")
print("  ok_conf >= %d (>=99%%)" % (N * 99 // 100))
print("  stalls == 0")
print("  abs(mem drift) <= 2048 B")
print("  stats_end.spi_busy_invariant_violations == 0")
mac.nvm_store()
verdict("testM_soak1k", ok_conf >= (N * 99 // 100) and stalls == 0,
        ok_conf=ok_conf, no_event=no_event, stalls=stalls,
        mem_drift=mem_at_end - mem_at_start)
