"""Smoke — join + 2 uplinks to verify post-cleanup build."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_min_rx_symbols(24)

deveui, joineui, appkey = mac.load_credentials()
mac.set_keys(deveui, joineui, appkey)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
    print("  event:", ev)
mac.set_event_callback(ev_cb)

print("=== JOIN ===")
T = time.ticks_ms()
st = mac.join()
print("  join() st:", st)
deadline = time.ticks_add(T, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
print("  joined:", mac.is_joined(), "elapsed:", time.ticks_diff(time.ticks_ms(), T), "ms")
if not mac.is_joined():
    raise SystemExit("JOIN FAILED")

for i in range(2):
    time.sleep_ms(15000)
    payload = b"smoke %d" % i
    print("=== UPLINK %d ===" % i)
    st = mac.send(1, payload, False)
    print("  send() st:", st)
    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    saw_conf = False
    snap = len(events)
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
        for _, e in events[snap:]:
            if e[0] == 'mcps_confirm' and e[1] == 0: saw_conf = True
        if saw_conf: break
    print("  uplink %d: confirmed=%s" % (i, saw_conf))
print("DONE")
