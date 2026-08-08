"""Test K v2 / HW-3 — server-initiated CONFIRMED downlinks; device ACKs.
Authorised rewrite: hardcoded keys, DR5 MIB default, INTERVAL=30 s,
CYCLES=10. dbg_last_ind() dropped per t005-clarify (master approval);
ack_uplinks verification moves to server-side journalctl cross-check.
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
mac.set_adr(False)

mac.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN ===")
T0 = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T0, 30000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    print("JOIN FAILED")
    raise SystemExit(1)
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T0))

# t005-rerun2: switch to Class C BEFORE the uplink loop so device has
# continuous RX2 listening — no RX1/RX2 scheduling deadline server-side.
st = mac.set_class('C')
print("  set_class('C') st=%d  class=%s" % (st, mac.get_class()))

print()
print(">>> Queue 10 confirmed downlinks now; this script sends 10 uplinks @30s <<<")
time.sleep(5)

CYCLES = 10
INTERVAL_MS = 30000
dl_received = 0
payloads = []

for i in range(CYCLES):
    while time.ticks_diff(time.ticks_ms(), T0) < (i + 1) * INTERVAL_MS:
        mac.process(); time.sleep_ms(20)

    snap = len(events)
    print("[%2d] uplink ..." % i)
    st = mac.send(1, b"k%02d" % i, False)
    if st != 0:
        print("     send() st=%d skip" % st); continue

    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)

    rx = mac.recv()
    if rx is not None:
        port, payload = rx
        dl_received += 1
        payloads.append((i, port, payload))
        print("     RX port=%d payload=%s" % (port, payload))

print()
print("=== HW-3 SUMMARY ===")
print("  cycles uplinked   : %d" % CYCLES)
print("  downlinks received: %d" % dl_received)
print("  payloads          :", payloads)
print()
print("PASS criterion: dl_received == 10 (server-side verifies ACK=true on next uplinks)")
print("VERDICT       : %s" % ("PASS" if dl_received == 10 else "FAIL"))
mac.nvm_store()
