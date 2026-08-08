"""Test B — multi-channel hopping verification.
30 uplinks at 10s spacing (~5 min). ADR off to keep DR fixed.
ChirpStack-side capture extracts txInfo.frequency per uplink to
verify even rotation across EU868 868.1/868.3/868.5 MHz."""
import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_adr(False)

ctxs = mac.nvm_diag()
total = sum(ctxs) if ctxs else 0
print("session: is_joined=%s nvm_ctx_total=%d" % (mac.is_joined(), total))
if not mac.is_joined():
    mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)
    mac.join()
    deadline = time.ticks_add(time.ticks_ms(), 12000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    print("joined fresh")

ring = bounded_ring(64)
mac.set_event_callback(make_ev_cb(ring))

T0 = time.ticks_ms()
ok = 0
for i in range(30):
    target = time.ticks_add(T0, i * 10000)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(50)
    t_tx = time.ticks_ms()
    mac.send(1, bytes([i & 0xFF]), False)
    end = time.ticks_add(time.ticks_ms(), 6000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    confirmed = any(tag == 'mcps_confirm' and status == 0
                    for _t, tag, status in ring.since(t_tx))
    if confirmed: ok += 1
    if i % 5 == 4:
        print("[%2d] confirmed=%d/%d" % (i, ok, i + 1))
print("DONE: %d/30 confirmed" % ok)
mac.nvm_store()
verdict("testB_channels", ok >= 27, confirmed=ok, total=30)
