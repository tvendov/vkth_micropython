"""Test I — Class B beacon sync + ping-slot downlink.

REQUIRES: gateway must broadcast LoRaWAN beacons every 128 s on
869.525 MHz SF12BW125 (gateway needs GPS lock + beacon-broadcast
enabled in chirpstack-gateway-bridge config).

Flow:
  1. Class A init, send 1 uplink for activation
  2. Set ping-slot periodicity (default 0 = every 32 s)
  3. Beacon acquisition — listen up to 7 min for first beacon
  4. On MLME_BEACON event, switch to Class B
  5. Wait for downlink on ping slot
  6. Test PASS = mcps_indication with payload during ping slot"""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
print("class:", mac.get_class())

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

# Trigger one uplink so server registers Class A activation
print("uplink for registration...")
mac.send(1, b"classb", False)
end = time.ticks_add(time.ticks_ms(), 8000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)

# Configure ping-slot periodicity (0 = 32 s, default)
st = mac.set_ping_slot_periodicity(0)
print("set_ping_slot_periodicity(0) st:", st)

# Start beacon acquisition
print("beacon_acquisition() — listening up to 7 min for first beacon")
st = mac.beacon_acquisition()
print("  st:", st)

T0 = time.ticks_ms()
beacon_seen = False
last_count = len(events)
while time.ticks_diff(time.ticks_ms(), T0) < 420000:   # 7 min
    mac.process(); time.sleep_ms(50)
    if len(events) > last_count:
        for _, ev in events[last_count:]:
            print("  +%ds" % (time.ticks_diff(time.ticks_ms(), T0) // 1000),
                  ev)
            if ev[0] == 'mlme_indication' and not beacon_seen:
                beacon_seen = True
                # Switch to Class B once beacon is locked
                st_b = mac.set_class('B')
                print("  set_class('B') st:", st_b, "class:", mac.get_class())
        last_count = len(events)

print()
print("DONE: beacon_seen=%s, final class=%s" % (beacon_seen, mac.get_class()))
mac.set_class('A')
mac.nvm_store()
