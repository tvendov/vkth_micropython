"""Test H — Class C continuous RX2 listening.

Steps:
  1. Init + switch to Class C
  2. Send 1 uplink so server recognizes the activation
  3. Loop 60s polling — chip stays in continuous RX2 between processes
  4. User queues a downlink via REST while we're idle
  5. We catch it via mcps_indication and mac.recv()"""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_min_rx_symbols(24)

print("class before:", mac.get_class())
st = mac.set_class('C')
print("set_class('C') st:", st)
print("class after :", mac.get_class())

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("uplink to advertise activation...")
mac.send(1, b"classc", False)
end = time.ticks_add(time.ticks_ms(), 8000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)
print("post-uplink events:", events[-3:])

print()
print(">>> queue a downlink in ChirpStack NOW (next 60s) <<<")
T0 = time.ticks_ms()
last_count = len(events)
while time.ticks_diff(time.ticks_ms(), T0) < 60000:
    mac.process(); time.sleep_ms(50)
    if len(events) > last_count:
        print("  +%dms" % time.ticks_diff(time.ticks_ms(), T0), events[last_count:])
        last_count = len(events)
        rx = mac.recv()
        if rx is not None:
            port, payload = rx
            print("  *** GOT DOWNLINK port=%d payload=%s ***" % (port, payload))

print("DONE. mac.recv() last:", mac.recv())
mac.set_class('A')   # restore Class A for normal save
mac.nvm_store()
