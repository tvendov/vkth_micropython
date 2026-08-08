"""Test L / HW-4 — EU868 max-FRMPayload matrix per DR.

Sends 3 uplinks each at DR0 / DR3 / DR5, every uplink at the spec maximum
FRMPayload size for its DR (EU868 §2.1.6, LoRaWAN Regional Parameters):

  DR0 SF12BW125 → max FRMPayload 51 B  (M=59, FHDR overhead 8)
  DR3 SF9BW125  → max FRMPayload 115 B (M=123)
  DR5 SF7BW125  → max FRMPayload 222 B (M=230)

Payload is a fixed pattern (bytes(range(N))) so the server-side dump can
verify no truncation. ADR is OFF (we pin the DR).

Duty cycle: 3 × (DR0 airtime 1.5s + DR3 ~330ms + DR5 ~80ms) over 9*30s spacing
is well within 1% on band 1."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_adr(False)

deveui, joineui, appkey = mac.load_credentials()
mac.set_keys(deveui, joineui, appkey)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN ===")
T0 = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T0, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    raise SystemExit("JOIN FAILED")
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T0))

# (dr, max_frm_payload_bytes)
DRS = ((0, 51), (3, 115), (5, 222))
PER_DR = 3
INTERVAL_S = 30

ok = 0
fail = 0
results = []

for dr, sz in DRS:
    print()
    print("=== DR=%d  payload=%d B ===" % (dr, sz))

    for j in range(PER_DR):
        payload = bytes(i & 0xff for i in range(sz))
        snap = len(events)
        t_tx = time.ticks_ms()
        st = mac.send(1, payload, False, dr)
        if st != 0:
            print("  [%d/%d] send() st=%d FAIL" % (j+1, PER_DR, st))
            results.append((dr, sz, j, False, st, "send_st_nonzero"))
            fail += 1
            time.sleep(INTERVAL_S)
            continue

        # Wait for confirm — unconfirmed uplinks still fire mcps_confirm.
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
              % (j+1, PER_DR, sz, conf_status, rtt, "OK" if confirmed else "FAIL"))
        results.append((dr, sz, j, confirmed, conf_status, "rtt=%d" % rtt))
        if confirmed: ok += 1
        else: fail += 1

        time.sleep(INTERVAL_S)

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
