"""Test B — multi-channel hopping verification.
30 uplinks at 10s spacing (~5 min). ADR off to keep DR fixed.
ChirpStack-side capture extracts txInfo.frequency per uplink to
verify even rotation across EU868 868.1/868.3/868.5 MHz."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_min_rx_symbols(24)
mac.set_adr(False)

p = mac.dbg_nvm_persist()
print("session: activation=%d (2=OTAA expected) init_loaded=%d bytes" %
      (p[7], p[1]))
if p[7] != 2:
    deveui, joineui, appkey = mac.load_credentials()
    mac.set_keys(deveui, joineui, appkey)
    mac.join()
    deadline = time.ticks_add(time.ticks_ms(), 12000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    print("joined fresh")

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

T0 = time.ticks_ms()
ok = 0
for i in range(30):
    target = time.ticks_add(T0, i * 10000)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(50)
    snap = len(events)
    mac.send(1, bytes([i & 0xFF]), False)
    end = time.ticks_add(time.ticks_ms(), 6000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    confirmed = any(e[0] == 'mcps_confirm' and e[1] == 0 for _, e in events[snap:])
    if confirmed: ok += 1
    if i % 5 == 4:
        print("[%2d] confirmed=%d/%d" % (i, ok, i + 1))
print("DONE: %d/30 confirmed" % ok)
mac.nvm_store()
