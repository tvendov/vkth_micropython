"""Final persistence validator — assumes flash already has a saved
session from a previous Stage 1. Just init, restore-from-flash via
nvm_board_init, send one uplink, print activation state."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()

p = mac.dbg_nvm_persist()
print("init_load_ok=%d  init_bytes=%d  mib_fail=%d  activation=%d (2=OTAA)  "
      "mlme_join=%d  send_rejoin=%d" %
      (p[0], p[1], p[4], p[7], p[8], p[9]))

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
    print("  event:", ev)
mac.set_event_callback(ev_cb)

print("sending uplink...")
mac.send(1, b"posrst", False)
end = time.ticks_add(time.ticks_ms(), 8000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)
print("done.")
p2 = mac.dbg_nvm_persist()
print("after-send: mlme_join=%d  send_rejoin=%d  (both should be 0 if no ghost JR)"
      % (p2[8], p2[9]))
mac.nvm_store()
