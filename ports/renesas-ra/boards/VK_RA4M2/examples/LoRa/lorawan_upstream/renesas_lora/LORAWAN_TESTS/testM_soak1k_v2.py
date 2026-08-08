"""Test M / HW-7 v2 — 200-uplink soak (shortened from 1000 per t010-rerun).
Authorised rewrite: hardcoded keys; mac.set_datarate(3) (absent) replaced
with mac.join(3) to pin MIB DR=3 SF9; nested mac.stats() dict layout.
"""
import lorawan, time, gc, binascii
from machine import Pin, SPI

DEV_EUI  = binascii.unhexlify("70B3D57ED0070003")
JOIN_EUI = binascii.unhexlify("0000000000000000")
APP_KEY  = binascii.unhexlify("202CB141A5842931F99C0C1DDFE70D68")

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_adr(False)

mac.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN at DR3 ===")
T0 = time.ticks_ms()
mac.join(3)              # pin MIB DR=3 via the join-frame datarate
deadline = time.ticks_add(T0, 30000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    print("JOIN FAILED")
    raise SystemExit(1)
print("  joined at +%dms  DR=%d" % (time.ticks_diff(time.ticks_ms(), T0), mac.get_datarate()))

N = 200
INTERVAL_MS = 60000
SNAPSHOT_EVERY = 25

ok_conf = 0
no_event = 0
stalls = 0
gc.collect()
mem_at_start = gc.mem_free()
stats_at_start = mac.stats()
print("baseline: gc.mem_free=%d" % mem_at_start)

t_last = time.ticks_ms()
T_loop = time.ticks_ms()
for i in range(N):
    target = time.ticks_add(T_loop, (i + 1) * INTERVAL_MS)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)

    elapsed = time.ticks_diff(time.ticks_ms(), t_last)
    if elapsed > INTERVAL_MS * 12 // 10:
        stalls += 1
        print("  ! stall at i=%d elapsed=%dms" % (i, elapsed))

    snap = len(events)
    st = mac.send(1, b"s%04d" % i, False)
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
        s = mac.stats()
        # nested layout per current firmware
        reject = s['spi']['sx126x_spi_busy_reject_count']
        mp_max = s['mac']['mac_process_max_us']
        bt = s['busy']['busy_timeout_count']
        print("  [%04d] ok=%d  no_evt=%d  stalls=%d  mem=%d  reject=%d  busy_to=%d  mp_max_us=%d"
              % (i+1, ok_conf, no_event, stalls, mem, reject, bt, mp_max))
        mac.nvm_store()
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
print("  reject_count end: %d" % stats_at_end['spi']['sx126x_spi_busy_reject_count'])
print("  busy_timeout_end: %d" % stats_at_end['busy']['busy_timeout_count'])
print()
print("PASS criteria:")
print("  ok_conf >= %d (>=99%%)  : %s" % (N * 99 // 100, "PASS" if ok_conf >= N * 99 // 100 else "FAIL"))
print("  stalls == 0           : %s" % ("PASS" if stalls == 0 else "FAIL"))
print("  abs(mem drift) <= 2kB : %s (%d B)" % ("PASS" if abs(mem_at_end - mem_at_start) <= 2048 else "FAIL", mem_at_end - mem_at_start))
print("  reject_count == 0     : %s" % ("PASS" if stats_at_end['spi']['sx126x_spi_busy_reject_count'] == 0 else "FAIL"))
mac.nvm_store()
print("VERDICT: %s" %
      ("PASS" if (ok_conf >= N * 99 // 100 and stalls == 0
                  and abs(mem_at_end - mem_at_start) <= 2048
                  and stats_at_end['spi']['sx126x_spi_busy_reject_count'] == 0) else "FAIL"))
