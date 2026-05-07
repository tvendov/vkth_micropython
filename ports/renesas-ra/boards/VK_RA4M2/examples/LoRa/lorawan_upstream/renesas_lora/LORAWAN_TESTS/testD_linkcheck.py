"""Test D — LinkCheckReq/Ans MAC command roundtrip.
Queue LinkCheckReq → send uplink → wait for downlink with
LinkCheckAns → read margin + gateway count via mac.last_link_check()."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_min_rx_symbols(24)

p = mac.dbg_nvm_persist()
print("session: activation=%d (2=OTAA expected)" % p[7])
if p[7] != 2:
    deveui, joineui, appkey = mac.load_credentials()
    mac.set_keys(deveui, joineui, appkey)
    mac.join()
    deadline = time.ticks_add(time.ticks_ms(), 12000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    print("joined fresh")

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
    print("  event:", ev)
mac.set_event_callback(ev_cb)

print("queueing LinkCheckReq...")
st = mac.link_check()
print("  link_check() st:", st, "(0 = OK queued)")

print("sending uplink (LinkCheckReq piggyback in FOpts)...")
mac.send(1, b"linkchk", False)

end = time.ticks_add(time.ticks_ms(), 10000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)

result = mac.last_link_check()
print()
print("=== RESULT ===")
print("  last_link_check():", result)
if result is not None:
    margin, gw = result
    print("  margin = %d dB above demod sensitivity" % margin)
    print("  gateways heard = %d" % gw)
mac.nvm_store()
