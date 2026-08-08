"""Test 1 — confirmed uplink + queued downlink reception.

User queues a downlink via ChirpStack REST API:
  curl -X POST http://192.168.2.140:8080/api/devices/<deveui>/queue \
       -H 'Authorization: Bearer <api_key>' \
       -H 'Content-Type: application/json' \
       -d '{"queueItem":{"fPort":10,"data":"SGVsbG8="}}'

Board sends a CONFIRMED uplink — server:
  1. delivers the queued downlink in RX1 (with Ack flag set)
  2. board reads `mac.recv()` to get the payload
  3. board's mcps_confirm fires with status=0 (Ack received)

Usage: queue the downlink first, then `import test1_confirmed`.
The script waits 5s then transmits."""
import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()

mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)

ring = bounded_ring(64)
mac.set_event_callback(make_ev_cb(ring))

print("=== JOIN ===")
T = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    verdict("test1_confirmed", False, reason='join_failed')
    raise SystemExit("JOIN FAILED")
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T))

print()
print("=== Waiting 5s for server-side queue setup... ===")
end = time.ticks_add(time.ticks_ms(), 5000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(50)

print()
print("=== CONFIRMED UPLINK (port=1, payload=b'\\x55') ===")
t_tx = time.ticks_ms()
st = mac.send(1, b"\x55", True)   # confirmed=True
print("  send() st:", st)

# Poll up to 8s for mcps_confirm + mcps_indication.
poll_until = time.ticks_add(time.ticks_ms(), 8000)
while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)

# Drain mac.recv() — pops the downlink payload if one arrived.
rx = mac.recv()
print()
print("=== RESULT ===")
recs = ring.since(t_tx)
print("  events from send:", recs)
print("  mac.recv() ->", rx)
if rx is not None:
    port, payload = rx
    print("  downlink port=%d  payload=%s  hex=%s"
          % (port, payload, payload.hex()))
confirm_ok = any(tag == 'mcps_confirm' and status == 0
                 for _t, tag, status in recs)
got_ind = any(tag == 'mcps_indication' for _t, tag, _s in recs)
print("  mcps_confirm status=0: %s   mcps_indication seen: %s"
      "   (confirm_ok proves server ACKed; indication shows downlink delivery)"
      % (confirm_ok, got_ind))
verdict("test1_confirmed", confirm_ok,
        confirm_ok=confirm_ok, indication=got_ind, payload=rx is not None)
