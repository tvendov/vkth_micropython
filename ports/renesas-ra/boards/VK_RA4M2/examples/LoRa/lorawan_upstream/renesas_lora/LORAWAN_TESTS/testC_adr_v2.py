"""Test C v2 — ADR convergence.
Authorised rewrite of testC_adr.py to current firmware API (e6879fc27).
Enables ADR, starts at DR0 (SF12), sends 25 uplinks at 8 s spacing.
Server should issue LinkADRReq → device climbs DR0 → DR ≥ 4 over
~16-20 uplinks.
"""
import lorawan, time, binascii
from machine import Pin, SPI

DEV_EUI  = binascii.unhexlify("70B3D57ED0070003")
JOIN_EUI = binascii.unhexlify("0000000000000000")
APP_KEY  = binascii.unhexlify("202CB141A5842931F99C0C1DDFE70D68")

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.init_defaults()           # T-003 rerun: pin MIB DR=0 so dr_at_start=0
mac.set_adr(True)             # ENABLE ADR — server-driven DR negotiation

print("session: is_joined=%s ADR=%s" % (mac.is_joined(), mac.get_adr()))
if not mac.is_joined():
    mac.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)
    mac.join(0)                  # explicit DR=0 for join frame so MIB stays at 0
    deadline = time.ticks_add(time.ticks_ms(), 30000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    if not mac.is_joined():
        print("JOIN FAILED")
        raise SystemExit(1)
    print("joined fresh")

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("starting at DR0 (SF12) and letting ADR climb...")
T0 = time.ticks_ms()
ok = 0
adr_changes = 0
linkadr_mlme_count = 0
mcps_fail_count = 0
last_dr = mac.get_datarate()
print("[init] dr=%s" % last_dr)
for i in range(25):
    target = time.ticks_add(T0, i * 8000)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(50)
    snap = len(events)
    # Force DR0 only on first uplink; subsequent uplinks rely on ADR-driven DR.
    if i == 0:
        mac.send(1, b"adr%d" % i, False, 0)
    else:
        mac.send(1, b"adr%d" % i, False)
    end = time.ticks_add(time.ticks_ms(), 6000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    confirmed = False
    for _, e in events[snap:]:
        ev_name, ev_status = e[0], e[1]
        if ev_name == 'mcps_confirm':
            if ev_status == 0:
                confirmed = True
            else:
                mcps_fail_count += 1
        elif ev_name == 'mlme_confirm':
            # mlme_confirm fires when server sends LinkADRReq (or other MLME).
            linkadr_mlme_count += 1
    cur_dr = mac.get_datarate()
    changed = (cur_dr != last_dr)
    if changed: adr_changes += 1
    if confirmed: ok += 1
    print("[%2d] tx=%s dr=%s%s" %
          (i, "OK" if confirmed else "FAIL", cur_dr, "  ADR" if changed else ""))
    last_dr = cur_dr
print()
print("DONE: %d/25 confirmed, %d ADR changes, dr_start=0 dr_end=%s"
      % (ok, adr_changes, last_dr))
print("linkadr_mlme_count=%d mcps_fail_count=%d" %
      (linkadr_mlme_count, mcps_fail_count))
mac.nvm_store()
