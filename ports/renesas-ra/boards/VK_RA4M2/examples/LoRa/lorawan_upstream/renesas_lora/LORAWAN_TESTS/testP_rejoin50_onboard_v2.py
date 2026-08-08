"""Test P (on-board) v2 / HW-10 — single cold-reset OTAA join + halt.
Authorised rewrite: hardcoded keys (load_credentials returns None after
factory_reset); dropped dbg_join_counters() (absent from firmware).
Designed to run as /main.py; the PC driver loops 50× with JLink resets.
"""
import lorawan, time, binascii
from machine import Pin, SPI

DEV_EUI  = binascii.unhexlify("70B3D57ED0070003")
JOIN_EUI = binascii.unhexlify("0000000000000000")
APP_KEY  = binascii.unhexlify("202CB141A5842931F99C0C1DDFE70D68")

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("DEV_NONCE_PROBE: boot eui=70b3d57ed0070003")

T0 = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T0, 15000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)

joined = mac.is_joined()
elapsed = time.ticks_diff(time.ticks_ms(), T0)
print("DEV_NONCE_PROBE: joined=%s elapsed_ms=%d" % (joined, elapsed))

if joined:
    snap = len(events)
    mac.send(1, b"P50", False)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    print("DEV_NONCE_PROBE: uplink_done events_tail=%s" % events[snap:][-3:])

print("DEV_NONCE_PROBE: halt")
while True:
    mac.process()
    time.sleep_ms(200)
