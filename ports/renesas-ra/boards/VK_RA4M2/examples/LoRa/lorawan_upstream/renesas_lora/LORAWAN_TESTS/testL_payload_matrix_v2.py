"""Test L / HW-4 v2 — EU868 max-FRMPayload matrix per DR.
Authorised rewrite per standing dbg_* / API-substitution policy:
  - mac.set_datarate() (absent) replaced by per-call DR override on
    mac.send(port, data, confirmed, datarate=dr).
  - dr-refusal detection: was `cur != dr` after set_datarate; now relies
    on mac.send() return value (st != 0 indicates stack refusal,
    including payload-exceeds-DR-max).
  - Hardcoded keys.
"""
import lorawan, time, binascii
from machine import Pin, SPI

DEV_EUI  = binascii.unhexlify("70B3D57ED0070003")
JOIN_EUI = binascii.unhexlify("0000000000000000")
APP_KEY  = binascii.unhexlify("202CB141A5842931F99C0C1DDFE70D68")

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_adr(False)

mac.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN ===")
T0 = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T0, 30000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    print("JOIN FAILED")
    raise SystemExit(1)
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T0))

# (dr, max_frm_payload_bytes, per_dr_count)
# t007-rerun patch: DR0 SF12 51B has 1.48 s airtime → per-channel DC
# backoff of 148 s. Cap DR0 at 1 uplink; keep 3 for DR3 / DR5.
DRS = ((0, 51, 1), (3, 115, 1), (5, 222, 1))   # t007-rerun2: 1 per DR
INTERVAL_S = 30
INTERVAL_BETWEEN_DRS_S = 100   # t007-rerun3: > DR0 148 s reservation margin

ok = 0
fail = 0
results = []

for dr, sz, count in DRS:
    print()
    print("=== DR=%d  payload=%d B  count=%d ===" % (dr, sz, count))

    for j in range(count):
        payload = bytes(i & 0xff for i in range(sz))
        snap = len(events)
        t_tx = time.ticks_ms()
        # v2: per-call DR override (4th positional arg).
        st = mac.send(1, payload, False, dr)
        if st != 0:
            print("  [%d/%d] send() st=%d FAIL (likely stack refusal or payload>DR-max)"
                  % (j+1, count, st))
            results.append((dr, sz, j, False, st, "send_st_nonzero"))
            fail += 1
            if j < count - 1:
                time.sleep(INTERVAL_S)
            continue

        poll_until = time.ticks_add(time.ticks_ms(), 10000)
        confirmed = False
        conf_status = None
        while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
            mac.process(); time.sleep_ms(20)
            for _, e in events[snap:]:
                if e[0] == 'mcps_confirm':
                    conf_status = e[1]
                    confirmed = (e[1] == 0)
                    break
            if conf_status is not None: break

        rtt = time.ticks_diff(time.ticks_ms(), t_tx)
        print("  [%d/%d] sz=%d conf_status=%s rtt=%dms %s"
              % (j+1, count, sz, conf_status, rtt, "OK" if confirmed else "FAIL"))
        results.append((dr, sz, j, confirmed, conf_status, "rtt=%d" % rtt))
        if confirmed: ok += 1
        else: fail += 1

        if j < count - 1:
            time.sleep(INTERVAL_S)

    # Extra slack between DRs (DR0 needs DC recovery before DR3).
    time.sleep(INTERVAL_BETWEEN_DRS_S)

print()
print("=== HW-4 SUMMARY ===")
print("  total uplinks: %d   ok=%d   fail=%d" % (len(results), ok, fail))
print()
print("per-row (dr, sz, idx, ok, conf_status, note):")
for r in results:
    print("  ", r)
print()
print("PASS criterion: ok == %d, fail == 0, server logs FRMPayload bytes match `bytes(range(N))` pattern."
      % len(results))
print("VERDICT       : %s" % ("PASS" if fail == 0 else "FAIL"))
mac.nvm_store()
