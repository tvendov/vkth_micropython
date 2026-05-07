"""Phase 7b diagnosis — join + 2 sends, print MLME_JOIN / SendReJoinReq
counters after every step to see who is re-issuing the JoinRequest."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_min_rx_symbols(24)

deveui, joineui, appkey = mac.load_credentials()
mac.set_keys(deveui, joineui, appkey)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

def report(tag):
    c = mac.dbg_join_counters()
    print("[%s] mlme_join_req=%d  mlme_other_req=%d  send_rejoin=%d  last_now=%d"
          % (tag, c[0], c[1], c[2], c[3]))

report("after init")
print("=== JOIN ===")
mac.join()
deadline = time.ticks_add(time.ticks_ms(), 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
print("joined:", mac.is_joined())
report("after join")

for i in range(3):
    target_ms = time.ticks_add(time.ticks_ms(), 30000)
    while time.ticks_diff(target_ms, time.ticks_ms()) > 0:
        mac.process()
        time.sleep_ms(50)
    snap = len(events)
    print()
    print("=== UPLINK %d (t+%dms after init) ===" % (i, time.ticks_ms()))
    report("before send #%d" % i)
    mac.send(1, b"diag %d" % i, False)
    poll_until = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    print("  events from send:", events[snap:])
    report("after send #%d" % i)

print()
print("=== final wait 20s — watch for ghost JoinRequests ===")
end = time.ticks_add(time.ticks_ms(), 20000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process()
    time.sleep_ms(100)
report("after 20s idle")
