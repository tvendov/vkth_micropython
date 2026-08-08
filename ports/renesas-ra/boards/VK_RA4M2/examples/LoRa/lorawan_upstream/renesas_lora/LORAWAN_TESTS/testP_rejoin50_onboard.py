"""Test P (on-board) / HW-10 — single cold-reset OTAA join + halt.

This script is meant to run on /main.py and execute ONE join + 1 uplink,
then idle. The PC-side driver loops: hardware-reset board → wait for ready
→ verify join happened on server → reset again ... ×50.

Prints a clear DEV_NONCE_PROBE line so the PC driver can grep for it in
journalctl on the ChirpStack server (look for `dev_nonce_validated`).

Important: nvm_factory_reset() on every boot — we want a fresh OTAA each
cycle to exercise dev_nonce increment + server's replay-protection logic.

LoRaWAN 1.0.4 §6.2.4: dev_nonce MUST be strictly monotonic across the
device's lifetime; the server rejects any DevNonce <= last seen."""
import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()

mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)

ring = bounded_ring(64)
mac.set_event_callback(make_ev_cb(ring))

print("DEV_NONCE_PROBE: boot %s eui=%s" %
      (time.time(), bytes(credentials.DEV_EUI).hex()))

T0 = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T0, 15000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)

joined = mac.is_joined()
elapsed = time.ticks_diff(time.ticks_ms(), T0)
mlme_count = sum(1 for _t, tag, _s in ring.since(T0) if tag == 'mlme_confirm')
s = mac.stats()
counters = (mlme_count,
            s['isr']['hard_isr_dio1_count'],
            s['mac']['mac_process_count'],
            s['nvm']['nvm_save_count'])
print("DEV_NONCE_PROBE: joined=%s elapsed_ms=%d counters=%s "
      "(mlme_confirms,hard_isr_dio1,mac_process,nvm_save)"
      % (joined, elapsed, counters))

if joined:
    t_tx = time.ticks_ms()
    mac.send(1, b"P50", False)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    print("DEV_NONCE_PROBE: uplink_done events_tail=%s" % ring.since(t_tx)[-3:])

verdict("testP_rejoin50_onboard_legacy", joined, elapsed_ms=elapsed,
        mlme_confirms=mlme_count)
print("DEV_NONCE_PROBE: halt")
while True:
    mac.process()
    time.sleep_ms(200)
