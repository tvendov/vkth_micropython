"""Test 2 — DR sweep SF7 → SF12.

Sends one uplink at each DR from 5 (SF7) down to 0 (SF12). Validates
RX timing across all spreading factors. SF12 has 32.77 ms/symbol so
RX1 with min_rx_symbols=24 = 786 ms — far exceeds the 8-symbol
preamble (262 ms).

EU868 duty cycle: 1% on 868.x. SF12 14-byte airtime ≈ 1.5 s, so 60 s
spacing keeps duty under 3%, well within budget.

ADR must be OFF for this test or the network server may override our
chosen DR after a few uplinks."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_min_rx_symbols(24)
mac.set_adr(False)             # pin DR; otherwise server may step it down

deveui, joineui, appkey = mac.load_credentials()
mac.set_keys(deveui, joineui, appkey)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
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

# Sweep DR5 (SF7) → DR0 (SF12)
results = []
for dr in (5, 4, 3, 2, 1, 0):
    sf = 12 - dr if dr > 0 else 12   # DR5=SF7 .. DR0=SF12
    print()
    print("=== UPLINK @ DR%d (SF%d BW125) ===" % (dr, sf))
    snap = len(events)
    t0 = time.ticks_ms()
    st = mac.send(1, bytes([dr]), False, dr)
    print("  send() st:", st)
    poll_until = time.ticks_add(time.ticks_ms(), 12000)   # SF12 needs longer
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    elapsed = time.ticks_diff(time.ticks_ms(), t0)
    confirmed = any(e[0] == 'mcps_confirm' and e[1] == 0 for _, e in events[snap:])
    indication = any(e[0] == 'mcps_indication' for _, e in events[snap:])
    results.append((dr, sf, confirmed, indication, elapsed))
    print("  events:", events[snap:])
    print("  -> confirmed=%s indication=%s elapsed=%dms" %
          (confirmed, indication, elapsed))
    # 60s spacing keeps EU868 duty cycle safe across all DR
    if dr > 0:
        target = time.ticks_add(t0, 60000)
        while time.ticks_diff(target, time.ticks_ms()) > 0:
            mac.process(); time.sleep_ms(50)

print()
print("=== SUMMARY ===")
for dr, sf, conf, ind, ms in results:
    print("  DR%d SF%-2d  confirmed=%s indication=%s  %dms"
          % (dr, sf, conf, ind, ms))
