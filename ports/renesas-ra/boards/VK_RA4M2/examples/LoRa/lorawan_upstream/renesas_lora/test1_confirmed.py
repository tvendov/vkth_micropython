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
mac.join()
deadline = time.ticks_add(T, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    raise SystemExit("JOIN FAILED")
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T))

print()
print("=== Waiting 5s for server-side queue setup... ===")
end = time.ticks_add(time.ticks_ms(), 5000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(50)

print()
print("=== CONFIRMED UPLINK (port=1, payload=b'\\x55') ===")
snap = len(events)
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
print("  events from send:", events[snap:])
print("  mac.recv() ->", rx)
if rx is not None:
    port, payload = rx
    print("  downlink port=%d  payload=%s  hex=%s"
          % (port, payload, payload.hex()))
ind = mac.dbg_last_ind()
print()
print("  dbg_last_ind: rxdata=%d port=%d bufsize=%d status=%d ack=%d mcps_kind=%d" %
      (ind[0], ind[1], ind[2], ind[3], ind[4], ind[5]))
print("    (rxdata=1 → payload available; ack=1 → server confirmed our uplink)")
