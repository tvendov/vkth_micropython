"""Soak test — 10 uplinks at 30s interval after a single OTAA join.
Verifies: per-uplink TX/RX cycle stability, RX1 downlink reception,
mcps_indication arrival, no scheduler stalls."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()

deveui, joineui, appkey = mac.load_credentials()
mac.set_keys(deveui, joineui, appkey)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN ===")
T = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    print("JOIN FAILED — abort"); raise SystemExit
print("  joined OK at +%dms" % time.ticks_diff(time.ticks_ms(), T))

N = 10
INTERVAL_MS = 30000
ok_tx = ok_rx = 0
for i in range(N):
    while time.ticks_diff(time.ticks_ms(), T) < (i + 1) * INTERVAL_MS:
        mac.process(); time.sleep_ms(20)
    payload = b"hello %d" % i
    saw_conf = saw_ind = False
    snap = len(events)
    print("[%2d] TX %s" % (i, payload), end="  ")
    st = mac.send(1, payload, False)
    if st != 0:
        print("send() st=%d" % st); continue
    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
        for _, e in events[snap:]:
            if e[0] == 'mcps_confirm' and e[1] == 0: saw_conf = True
            if e[0] == 'mcps_indication': saw_ind = True
        if saw_conf and saw_ind: break
    if saw_conf: ok_tx += 1
    if saw_ind:  ok_rx += 1
    print("conf=%s ind=%s" % (saw_conf, saw_ind))

print()
print("SUMMARY: %d/%d uplinks confirmed, %d/%d downlinks received"
      % (ok_tx, N, ok_rx, N))
