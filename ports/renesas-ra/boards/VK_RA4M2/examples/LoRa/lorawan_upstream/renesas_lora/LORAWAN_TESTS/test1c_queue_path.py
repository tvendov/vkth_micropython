"""Test 1c — clean isolated queue → downlink path.

Flow:
  1. Join.
  2. First uplink CONFIRMED  → expect Ack-only response (queue empty).
  3. Pause 30s — USER queues a downlink via REST API.
  4. Second uplink UNCONFIRMED → server piggybacks queued payload.
  5. mac.recv() returns (port, payload).

Run: queue MUST be empty at start. After uplink #1 the script prints
"queue NOW" — at that moment user runs the curl to add the payload.
30s timer keeps RX windows open via mac.process()."""
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

# --- First uplink: confirmed, expect Ack-only ---
print()
print("=== UPLINK #1 (confirmed, queue empty) ===")
snap = len(events)
mac.send(1, b"\x55", True)
end = time.ticks_add(time.ticks_ms(), 8000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)
print("  events:", events[snap:])
print("  recv:", mac.recv())

# --- 30s pause for user to queue payload ---
print()
print(">>> queue NOW (curl POST .../queue payload=SGVsbG8=) <<<")
print(">>> 30s window starts ...")
end = time.ticks_add(time.ticks_ms(), 30000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(50)

# --- Second uplink: unconfirmed, expect queued payload in RX1 ---
print()
print("=== UPLINK #2 (unconfirmed, expect queued payload) ===")
snap = len(events)
mac.send(1, b"\x66", False)
end = time.ticks_add(time.ticks_ms(), 8000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)
print("  events:", events[snap:])
rx = mac.recv()
print("  recv:", rx)
if rx is not None:
    port, payload = rx
    print("  -> port=%d payload=%s hex=%s" % (port, payload, payload.hex()))
