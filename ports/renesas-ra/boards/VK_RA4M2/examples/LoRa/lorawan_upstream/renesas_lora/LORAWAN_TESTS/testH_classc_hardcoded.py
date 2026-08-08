"""Test H — Class C with hardcoded keys (no Data Flash provision needed).

Steps:
  1. Init + OTAA join with hardcoded keys
  2. Switch to Class C
  3. Send 1 uplink to advertise activation
  4. Loop 60s polling for downlink
  5. Catch downlink via mcps_indication and mac.recv()"""
import lorawan, time
from machine import Pin, SPI

DEVEUI  = bytes.fromhex("70B3D57ED0070003")
JOINEUI = bytes.fromhex("0000000000000000")
APPKEY  = bytes.fromhex("202CB141A5842931F99C0C1DDFE70D68")

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_keys(DEVEUI, JOINEUI, APPKEY)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN ===")
T = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T, 30000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined():
        break
    time.sleep_ms(20)

if not mac.is_joined():
    print("JOIN FAILED. events:", events)
    raise SystemExit
print("joined at +%dms" % time.ticks_diff(time.ticks_ms(), T))

print()
print("=== Switch to Class C ===")
print("class before:", mac.get_class())
st = mac.set_class('C')
print("set_class('C') st:", st)
print("class after:", mac.get_class())

print()
print("=== Uplink to advertise activation ===")
events_pre = len(events)
mac.send(1, b"classc", False)
end = time.ticks_add(time.ticks_ms(), 8000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)
print("post-uplink events:", events[events_pre:])

# Sync sentinel for master psql arming. After activation uplink ack,
# device session is live on the server. Slave prints this line; master
# detects it, arms 5 DLs, RESPs "t-v3b-armed". Pause gives master ~30s
# to complete the psql arm before listen window starts.
print(">>> ACTIVATION_DONE_PSQL_ARM_NOW <<<")
sync_end = time.ticks_add(time.ticks_ms(), 30000)
while time.ticks_diff(sync_end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(50)

print()
print(">>> queue a downlink in ChirpStack NOW (next 60s) <<<")
T0 = time.ticks_ms()
last_count = len(events)
while time.ticks_diff(time.ticks_ms(), T0) < 115000:
    mac.process(); time.sleep_ms(50)
    if len(events) > last_count:
        print("  +%dms" % time.ticks_diff(time.ticks_ms(), T0), events[last_count:])
        last_count = len(events)
        rx = mac.recv()
        if rx is not None:
            port, payload = rx
            print("  *** GOT DOWNLINK port=%d payload=%s ***" % (port, payload))

print("DONE. mac.recv() last:", mac.recv())
mac.set_class('A')
