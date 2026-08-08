"""Test J / HW-2 — ConfirmedDataUp volume test (20 round-trips).

Sends 20 CONFIRMED uplinks at 30s spacing. Server must ACK each one
(mcps_confirm status=0, indication with ack=1 on next downlink window).
Counts: ack_received, conf_status_nonzero, timeouts.

EU868 duty cycle margin at 30s × 20:
  DR3 SF9 11-byte payload ≈ 124 ms airtime; 20×124 ms / 600 s = 0.4% on band 1,
  well within 1% per sub-band budget.

Expected runtime: ~10 minutes."""
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

N = 20
INTERVAL_MS = 30000
ack_ok = 0
conf_fail = 0
no_event = 0
ind_seen = 0
per_uplink = []

# Spacing baseline: T0 + (i+1) * INTERVAL_MS for uplink i. Drives strict cadence.
for i in range(N):
    while time.ticks_diff(time.ticks_ms(), T0) < (i + 1) * INTERVAL_MS:
        mac.process(); time.sleep_ms(20)

    payload = b"cf %02d" % i
    snap = len(events)
    t_tx = time.ticks_ms()
    st = mac.send(1, payload, True)
    if st != 0:
        print("[%2d] send() st=%d — skip" % (i, st))
        per_uplink.append((i, "send_fail", st, -1))
        continue

    # Poll up to 8s for mcps_confirm. RX2 latency is bounded by stack at ~6s.
    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    saw_conf_status = None
    saw_ind = False
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
        for _, e in events[snap:]:
            if e[0] == 'mcps_confirm' and saw_conf_status is None:
                saw_conf_status = e[1]
            if e[0] == 'mcps_indication':
                saw_ind = True
        if saw_conf_status is not None and saw_ind: break

    rtt_ms = time.ticks_diff(time.ticks_ms(), t_tx)
    if saw_conf_status == 0:
        ack_ok += 1
        if saw_ind: ind_seen += 1
        tag = "ACK"
    elif saw_conf_status is None:
        no_event += 1
        tag = "TIMEOUT"
    else:
        conf_fail += 1
        tag = "CONF_FAIL(%d)" % saw_conf_status

    per_uplink.append((i, tag, saw_conf_status, rtt_ms))
    print("[%2d] %-15s status=%s ind=%s rtt=%dms"
          % (i, tag, saw_conf_status, saw_ind, rtt_ms))

print()
print("=== HW-2 SUMMARY ===")
print("  ACKs received      : %d/%d" % (ack_ok, N))
print("  conf_status_nonzero: %d/%d" % (conf_fail, N))
print("  timeouts (no event): %d/%d" % (no_event, N))
print("  indications        : %d/%d" % (ind_seen, N))
print()
print("PASS criterion: ack_ok == %d AND conf_fail == 0 AND no_event == 0" % N)
print("VERDICT       : %s" % ("PASS" if (ack_ok == N and conf_fail == 0 and no_event == 0) else "FAIL"))
mac.nvm_store()
