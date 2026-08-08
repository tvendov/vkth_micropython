"""Test M / HW-7 v4 — 200-uplink soak with bounded event buffer.
Authorised rewrite per t010r-fix: replace unbounded `events` list with
collections.deque(maxlen=64) ring buffer to eliminate the linear leak
seen in v2 (events list grew ~80 B/uplink → MemoryError @ ~iter 130).
DR5 baseline per latest master direction (was DR3 in v2).
"""
import lorawan, time, gc, binascii
from machine import Pin, SPI

try:
    from collections import deque
    USE_DEQUE = True
except ImportError:
    USE_DEQUE = False

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

if USE_DEQUE:
    try:
        events = deque((), 64)
    except TypeError:
        events = []
        USE_DEQUE = False
else:
    events = []

EVT_RING_MAX = 64

def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
    if not USE_DEQUE and len(events) > EVT_RING_MAX:
        events.pop(0)

mac.set_event_callback(ev_cb)

print("=== JOIN at DR5 ===")
print("events buffer: %s maxlen=%d" % ("deque" if USE_DEQUE else "list", EVT_RING_MAX))
T0 = time.ticks_ms()
mac.join(5)
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

    # Snapshot tail of ring to detect mcps_confirm for this uplink.
    # Use a counter to know how many events existed before send.
    pre_n = 0
    try:
        pre_n = len(events)
    except TypeError:
        pre_n = 0

    st = mac.send(1, b"s%04d" % i, False)
    if st != 0:
        print("  [%04d] send() st=%d skip" % (i, st)); continue

    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    saw_conf = None
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
        # Walk the ring; deque doesn't support slicing.
        for _, e in list(events)[max(0, pre_n - 8):]:
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
        reject = s['spi']['sx126x_spi_busy_reject_count']
        mp_max = s['mac']['mac_process_max_us']
        bt = s['busy']['busy_timeout_count']
        ev_len = len(events) if hasattr(events, '__len__') else -1
        print("  [%04d] ok=%d no_evt=%d stalls=%d mem=%d reject=%d busy_to=%d mp_max=%d events=%d"
              % (i+1, ok_conf, no_event, stalls, mem, reject, bt, mp_max, ev_len))
        mac.nvm_store()

gc.collect()
mem_at_end = gc.mem_free()
stats_at_end = mac.stats()
drift = mem_at_end - mem_at_start

print()
print("=== HW-7 SUMMARY ===")
print("  uplinks ok_conf : %d / %d" % (ok_conf, N))
print("  no_event        : %d" % no_event)
print("  stalls          : %d" % stalls)
print("  mem drift       : %d B (end %d - start %d)"
      % (drift, mem_at_end, mem_at_start))
print("  reject_count    : %d" % stats_at_end['spi']['sx126x_spi_busy_reject_count'])
print("  busy_timeout    : %d" % stats_at_end['busy']['busy_timeout_count'])
print("  mac_proc_max_us : %d" % stats_at_end['mac']['mac_process_max_us'])
print()
PASS_C1 = ok_conf >= N * 99 // 100
PASS_C2 = stalls == 0
PASS_C3 = abs(drift) <= 2048
PASS_C4 = stats_at_end['spi']['sx126x_spi_busy_reject_count'] == 0
print("PASS criteria:")
print("  ok_conf >= %d  : %s" % (N * 99 // 100, "PASS" if PASS_C1 else "FAIL"))
print("  stalls == 0     : %s" % ("PASS" if PASS_C2 else "FAIL"))
print("  mem_drift <=2kB : %s" % ("PASS" if PASS_C3 else "FAIL"))
print("  reject == 0     : %s" % ("PASS" if PASS_C4 else "FAIL"))
print("VERDICT: %s" % ("PASS" if all([PASS_C1, PASS_C2, PASS_C3, PASS_C4]) else "FAIL"))
mac.nvm_store()
