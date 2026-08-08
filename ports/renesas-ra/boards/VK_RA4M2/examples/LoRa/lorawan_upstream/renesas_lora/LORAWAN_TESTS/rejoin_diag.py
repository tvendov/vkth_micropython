"""Phase 7b diagnosis — join + 2 sends, print MLME_JOIN / SendReJoinReq
counters after every step to see who is re-issuing the JoinRequest."""
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

ring = bounded_ring(128)
mac.set_event_callback(make_ev_cb(ring))

def _ghost_counters_since(t0):
    n_mlme = n_mcps_c = n_mcps_i = 0
    for _t, tag, _s in ring.since(t0):
        if tag == 'mlme_confirm': n_mlme += 1
        elif tag == 'mcps_confirm': n_mcps_c += 1
        elif tag == 'mcps_indication': n_mcps_i += 1
    return n_mlme, n_mcps_c, n_mcps_i

def report(tag, t0):
    m, cc, ci = _ghost_counters_since(t0)
    print("[%s] mlme_confirm_since=%d  mcps_confirm_since=%d  "
          "mcps_ind_since=%d  ring_len=%d  is_joined=%s"
          % (tag, m, cc, ci, len(ring), mac.is_joined()))

T_init = time.ticks_ms()
report("after init", T_init)
print("=== JOIN ===")
mac.join()
deadline = time.ticks_add(time.ticks_ms(), 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
print("joined:", mac.is_joined())
T_postjoin = time.ticks_ms()
report("after join", T_init)

ghosts = 0
for i in range(3):
    target_ms = time.ticks_add(time.ticks_ms(), 30000)
    while time.ticks_diff(target_ms, time.ticks_ms()) > 0:
        mac.process()
        time.sleep_ms(50)
    print()
    print("=== UPLINK %d (t+%dms after init) ===" % (i, time.ticks_ms()))
    report("before send #%d" % i, T_postjoin)
    t_tx = time.ticks_ms()
    mac.send(1, b"diag %d" % i, False)
    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    print("  events from send:", ring.since(t_tx))
    report("after send #%d" % i, T_postjoin)

print()
print("=== final wait 20s — watch for ghost JoinRequests ===")
T_idle = time.ticks_ms()
end = time.ticks_add(time.ticks_ms(), 20000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process()
    time.sleep_ms(100)
report("after 20s idle", T_idle)
ghost_mlme, _, _ = _ghost_counters_since(T_idle)
verdict("rejoin_diag", mac.is_joined() and ghost_mlme == 0,
        is_joined=mac.is_joined(), ghost_mlme_in_idle=ghost_mlme)
