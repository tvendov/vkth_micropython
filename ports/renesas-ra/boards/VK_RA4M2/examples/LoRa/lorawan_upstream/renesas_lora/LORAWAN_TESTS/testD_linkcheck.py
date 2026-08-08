"""Test D — LinkCheckReq/Ans MAC command roundtrip.
Queue LinkCheckReq → send uplink → wait for downlink with
LinkCheckAns → read margin + gateway count via mac.last_link_check()."""
import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()

ctxs = mac.nvm_diag()
total = sum(ctxs) if ctxs else 0
print("session: is_joined=%s nvm_ctx_total=%d" % (mac.is_joined(), total))
if not mac.is_joined():
    mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)
    mac.join()
    deadline = time.ticks_add(time.ticks_ms(), 12000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    print("joined fresh")

ring = bounded_ring(64)
mac.set_event_callback(make_ev_cb(ring))

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
passed = False
if result is not None:
    margin, gw = result
    print("  margin = %d dB above demod sensitivity" % margin)
    print("  gateways heard = %d" % gw)
    passed = (margin > 0 and gw >= 1)
mac.nvm_store()
if result is None:
    verdict("testD_linkcheck", False, reason='no_linkcheckans')
else:
    verdict("testD_linkcheck", passed,
            margin=result[0], gateways=result[1])
