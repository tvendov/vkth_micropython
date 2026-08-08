"""Test 5 — multi-channel hopping (EU868 868.1/868.3/868.5).
6 uplinks, 10s apart. ChirpStack rx_info.frequency reveals which
channel was used for each TX — should rotate across all 3 sub-bands."""
import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()

ring = bounded_ring(64)
mac.set_event_callback(make_ev_cb(ring))

if not mac.is_joined():
    mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)
    mac.join()
    deadline = time.ticks_add(time.ticks_ms(), 12000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    print("joined")

T0 = time.ticks_ms()
ok = 0
for i in range(6):
    target = time.ticks_add(T0, i * 10000)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(50)
    t_tx = time.ticks_ms()
    mac.send(1, b"chan%d" % i, False)
    end = time.ticks_add(time.ticks_ms(), 6000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    confirmed = any(tag == 'mcps_confirm' and status == 0
                    for _t, tag, status in ring.since(t_tx))
    print("[%d] confirmed=%s" % (i, confirmed))
    if confirmed: ok += 1
print("DONE: %d/6 uplinks confirmed" % ok)
mac.nvm_store()
verdict("test5_channels", ok >= 5, confirmed=ok, total=6)
