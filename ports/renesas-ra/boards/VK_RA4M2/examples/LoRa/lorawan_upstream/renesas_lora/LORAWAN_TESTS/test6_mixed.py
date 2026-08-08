"""Test 6 — mixed confirmed/unconfirmed traffic, 6 uplinks/1 min.
Alternates confirmed (Ack expected) and unconfirmed. Validates that
LoRaMac transitions cleanly between MCPS_CONFIRMED and MCPS_UNCONFIRMED
within the same boot/session, no state corruption."""
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
    print("joined fresh")
else:
    print("session restored from flash")

T0 = time.ticks_ms()
ok_tx = ack_count = 0
for i in range(6):
    target = time.ticks_add(T0, i * 10000)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(50)
    t_tx = time.ticks_ms()
    confirmed_req = (i % 2 == 0)   # alternate
    mac.send(1, b"mix%d" % i, confirmed_req)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    recs = ring.since(t_tx)
    tx_ok = any(tag == 'mcps_confirm' and status == 0 for _t, tag, status in recs)
    got_ind = any(tag == 'mcps_indication' for _t, tag, _s in recs)
    if tx_ok: ok_tx += 1
    if confirmed_req and got_ind: ack_count += 1
    print("[%d] %s tx_ok=%s ind=%s" %
          (i, "CONF" if confirmed_req else "UNC ", tx_ok, got_ind))
print("DONE: %d/6 TX, %d/3 confirmed-Acks" % (ok_tx, ack_count))
mac.nvm_store()
verdict("test6_mixed", ok_tx >= 5, tx_ok=ok_tx, acks=ack_count)
