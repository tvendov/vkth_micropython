"""Test K / HW-3 — server-initiated CONFIRMED downlinks; device ACKs.

Server queues 10 confirmed=true downlinks; for each, the device's next
uplink must carry the ACK bit. The downlink itself opens an RX window on
the device's uplink (Class A) — so we send periodic uplinks at 60s spacing
to give the server windows to deliver into.

How to queue (run on PC, see TEST_EXECUTION_PLAN.md §0.4):
  INSERT INTO device_queue_item (id, dev_eui, f_port, confirmed, data, created_at)
  VALUES (gen_random_uuid(), decode('70b3d57ed0070003','hex'), 10, true,
          decode('48656c6c6f','hex'), NOW());

Run pattern:
  - PC operator queues 1 confirmed downlink every ~60s (10 total).
  - This script just uplinks every 60s for 12 cycles and counts
    mcps_indication events from the event-callback ring.

Expected: 10/10 indications with ack-on-next-uplink seen by server log."""
import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_adr(False)

mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)

ring = bounded_ring(128)
mac.set_event_callback(make_ev_cb(ring))

print("=== JOIN ===")
T0 = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T0, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    verdict("testK_confirmed_dl_legacy", False, reason='join_failed')
    raise SystemExit("JOIN FAILED")
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T0))

print()
print(">>> Queue 10 CONFIRMED downlinks server-side now; this script <<<")
print(">>> sends 12 uplinks at 60s cadence to open RX windows.        <<<")
time.sleep(5)

CYCLES = 12
INTERVAL_MS = 60000
dl_received = 0      # downlink payloads delivered to device
ind_uplinks = 0      # mcps_indication events observed (downlink delivery proof)
payloads = []

for i in range(CYCLES):
    while time.ticks_diff(time.ticks_ms(), T0) < (i + 1) * INTERVAL_MS:
        mac.process(); time.sleep_ms(20)

    t_tx = time.ticks_ms()
    print("[%2d] uplink ..." % i)
    st = mac.send(1, b"k%02d" % i, False)
    if st != 0:
        print("     send() st=%d skip" % st); continue

    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)

    rx = mac.recv()
    if rx is not None:
        port, payload = rx
        dl_received += 1
        payloads.append((i, port, payload))
        print("     RX port=%d payload=%s" % (port, payload))

    if any(tag == 'mcps_indication' for _t, tag, _s in ring.since(t_tx)):
        ind_uplinks += 1
        print("     mcps_indication observed (server confirmed downlink delivery)")

print()
print("=== HW-3 SUMMARY ===")
print("  cycles uplinked      : %d" % CYCLES)
print("  downlinks received   : %d" % dl_received)
print("  mcps_indication count: %d" % ind_uplinks)
print("  payload sample       :", payloads[:5])
print()
print("PASS criterion: dl_received >= 10 AND every confirmed downlink ACKed on next uplink")
print("                (cross-verify with server log: 10x downlink_id with subsequent ack=true uplink)")
mac.nvm_store()
verdict("testK_confirmed_dl_legacy", dl_received >= 10,
        dl_received=dl_received, indications=ind_uplinks, cycles=CYCLES)
