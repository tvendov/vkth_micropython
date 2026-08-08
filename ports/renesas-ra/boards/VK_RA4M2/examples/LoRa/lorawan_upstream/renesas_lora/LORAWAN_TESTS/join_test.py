import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

ring = bounded_ring(128)

mac = lorawan.Mac(region=lorawan.EU868)
# Wipe persisted NVM (MAC params include MaxRxError/MinRxSymbols
# from earlier sessions — we set them too high and that made
# RxWindow2Delay collapse below the TX-done offset).
mac.nvm_factory_reset()
mac.lorawan_init()
# Bump min_rx_symbols from the default 6 to 12 — at SF7 the default
# gives only ~6.14 ms RX window which is shorter than the LoRaWAN
# downlink preamble (8 symbols × 1.024 ms = 8.2 ms). 12 symbols
# = 12.3 ms at SF7 leaves margin for clock drift to lock onto
# the preamble. Keep MaxRxError at default 10 ms so WindowOffset
# stays small (we burned a session earlier with 500 ms).

print("DevEUI :", bytes(credentials.DEV_EUI).hex())
print("JoinEUI:", bytes(credentials.JOIN_EUI).hex())
print("AppKey :", bytes(credentials.APP_KEY).hex())

mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)
mac.set_event_callback(make_ev_cb(ring))

# Sanity-check chip is alive
print("radio status:", hex(mac.radio_get_status()), "busy:", mac.radio_busy())


def _print_stats(label):
    s = mac.stats()
    print("--- mac.stats() @ %s ---" % label)
    for group in ('mac', 'spi', 'busy', 'isr', 'nvm'):
        g = s.get(group, {})
        print("  [%s]" % group)
        for k in sorted(g.keys()):
            print("    %-40s %s" % (k, g[k]))


_print_stats("before join")
print("=== JOIN (instrumented) ===")
T0 = time.ticks_ms()
req_st = mac.join()
print("  join() request status:", req_st)
deadline = time.ticks_add(T0, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined():
        break
    time.sleep_ms(20)
elapsed = time.ticks_diff(time.ticks_ms(), T0)
joined = mac.is_joined()
print("  joined:", joined, "elapsed:", elapsed, "ms")
print("  events during join:", ring.since(T0))

if joined:
    print()
    print("=== UPLINK after join ===")
    t_tx = time.ticks_ms()
    payload = b"VK_RA4M2 hello"
    fport = 1
    try:
        st = mac.send(fport, payload, False)   # confirmed=False
        print("  send() request status:", st)
    except Exception as e:
        print("  send() raised:", e)
    deadline2 = time.ticks_add(time.ticks_ms(), 10000)
    while time.ticks_diff(deadline2, time.ticks_ms()) > 0:
        mac.process()
        time.sleep_ms(20)
    print("  events after uplink:", ring.since(t_tx))

_print_stats("after join+uplink")

mlme_count = sum(1 for _t, tag, _s in ring.since(T0) if tag == 'mlme_confirm')
verdict("join_test", joined, elapsed_ms=elapsed, mlme_confirms=mlme_count)
