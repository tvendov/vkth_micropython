"""Test 6 — mixed confirmed/unconfirmed traffic, 6 uplinks/1 min.
Alternates confirmed (Ack expected) and unconfirmed. Validates that
LoRaMac transitions cleanly between MCPS_CONFIRMED and MCPS_UNCONFIRMED
within the same boot/session, no state corruption."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_min_rx_symbols(24)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

if mac.dbg_nvm_persist()[7] != 2:
    deveui, joineui, appkey = mac.load_credentials()
    mac.set_keys(deveui, joineui, appkey)
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
    snap = len(events)
    confirmed_req = (i % 2 == 0)   # alternate
    mac.send(1, b"mix%d" % i, confirmed_req)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    tx_ok = any(e[0] == 'mcps_confirm' and e[1] == 0 for _, e in events[snap:])
    got_ind = any(e[0] == 'mcps_indication' for _, e in events[snap:])
    if tx_ok: ok_tx += 1
    if confirmed_req and got_ind: ack_count += 1
    print("[%d] %s tx_ok=%s ind=%s" %
          (i, "CONF" if confirmed_req else "UNC ", tx_ok, got_ind))
print("DONE: %d/6 TX, %d/3 confirmed-Acks" % (ok_tx, ack_count))
mac.nvm_store()
