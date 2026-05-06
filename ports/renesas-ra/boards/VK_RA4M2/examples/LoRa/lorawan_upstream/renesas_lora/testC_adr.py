"""Test C — ADR convergence.
Enables ADR, starts at DR0 (SF12), sends 25 uplinks at 8s spacing.
Server has strong signal (+21 dB margin per LinkCheckAns) so it
should issue LinkADRReq → board climbs DR0 → ... → DR5 (SF7) over
~16-20 uplinks.

Watch:
  - DR climbs in the [dr@ MIB] column over time
  - mlme_confirm fires when ADR LinkADRReq processed
  - Server-side log: 'pending mac-command block ... LinkADRReq'"""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_min_rx_symbols(24)
mac.set_adr(True)             # ENABLE ADR — server-driven DR negotiation

p = mac.dbg_nvm_persist()
print("session: activation=%d (2=OTAA expected)  ADR=%s" %
      (p[7], mac.get_adr()))
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
mac.set_event_callback(ev_cb)

print("starting at DR0 (SF12) and letting ADR climb...")
T0 = time.ticks_ms()
ok = 0
adr_changes = 0
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
    confirmed = any(e[0] == 'mcps_confirm' and e[1] == 0 for _, e in events[snap:])
    cur_dr = mac.get_datarate()
    changed = (cur_dr != last_dr)
    if changed: adr_changes += 1
    if confirmed: ok += 1
    print("[%2d] tx=%s dr=%s%s" %
          (i, "OK" if confirmed else "FAIL", cur_dr, "  ★ADR" if changed else ""))
    last_dr = cur_dr
print()
print("DONE: %d/25 confirmed, %d ADR changes, final DR=%s"
      % (ok, adr_changes, last_dr))
mac.nvm_store()
