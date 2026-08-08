"""Test O / HW-9 — combined MAC command storm.

Queues multiple device-initiated MAC commands and triggers server-initiated
ones in a single window:

  device-initiated (carried in FOpts of the same uplink):
    - LinkCheckReq        (mac.link_check())
    - DeviceTimeReq       (mac.device_time_req())

  server-initiated (provoked):
    - LinkADRReq          : ADR was OFF; flip ADR ON + force DR0 + decent margin
                            so server immediately wants to push us back up.
    - DevStatusReq        : ChirpStack issues this periodically; with ADR on
                            it usually piggybacks in the next downlink.

After the burst, we measure:
  - mac.process() worst-case per-call latency
  - last_link_check() returns valid (margin, gw_count)
  - get_sys_time() advanced
  - mac.get_datarate() increased from 0"""
import lorawan, time
from machine import Pin, SPI
from _lorawan_test_helpers import verdict

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.init_defaults()
mac.set_adr(False)

deveui, joineui, appkey = mac.load_credentials()
mac.set_keys(deveui, joineui, appkey)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN at DR0 ===")
T0 = time.ticks_ms()
mac.join(0)
deadline = time.ticks_add(T0, 15000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    raise SystemExit("JOIN FAILED")
print("  joined at +%dms  DR=%d" % (time.ticks_diff(time.ticks_ms(), T0), mac.get_datarate()))

# Queue both device-side MAC commands, then enable ADR so server pushes back.
print()
print("queue LinkCheckReq + DeviceTimeReq, enable ADR")
print("  link_check()        st:", mac.link_check())
print("  device_time_req()   st:", mac.device_time_req())
mac.set_adr(True)
t_before_sys = mac.get_sys_time()
dr_before = mac.get_datarate()
print("  sys_time before:", t_before_sys, " DR before:", dr_before)

mac_proc_max = 0
N = 8
for i in range(N):
    target = time.ticks_add(time.ticks_ms(), 20000)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        t1 = time.ticks_ms()
        mac.process()
        d = time.ticks_diff(time.ticks_ms(), t1)
        if d > mac_proc_max: mac_proc_max = d
        time.sleep_ms(20)

    snap = len(events)
    print()
    print("[%d] uplink DR=%d ..." % (i, mac.get_datarate()))
    st = mac.send(1, b"O%d" % i, False)
    if st != 0:
        print("    send() st=%d" % st); continue

    poll = time.ticks_add(time.ticks_ms(), 10000)
    while time.ticks_diff(poll, time.ticks_ms()) > 0:
        t1 = time.ticks_ms()
        mac.process()
        d = time.ticks_diff(time.ticks_ms(), t1)
        if d > mac_proc_max: mac_proc_max = d
        time.sleep_ms(20)

    print("    events:", events[snap:][-8:] if len(events) > snap else "[]")
    lc = mac.last_link_check()
    if lc is not None:
        print("    last_link_check:", lc)

dr_after = mac.get_datarate()
t_after_sys = mac.get_sys_time()
lc_final = mac.last_link_check()
stats = mac.stats()

print()
print("=== HW-9 SUMMARY ===")
print("  DR before: %d  → after: %d" % (dr_before, dr_after))
print("  sys_time before: %s  → after: %s" % (t_before_sys, t_after_sys))
print("  last_link_check final:", lc_final)
print("  mac.process worst-case latency: %d ms" % mac_proc_max)
if isinstance(stats, dict):
    print("  stats.mac_process_max_us  :", stats.get('mac_process_max_us'))
    print("  stats.mcps_confirm_fail   :", stats.get('mcps_confirm_fail'))
    print("  stats.spi_busy_invariant_violations:", stats.get('spi_busy_invariant_violations'))
print()
print("PASS criteria:")
print("  dr_after > dr_before (LinkADRReq applied)")
print("  lc_final not None (LinkCheckAns received → margin + gateways read)")
print("  t_after_sys != t_before_sys (DeviceTimeAns applied)")
print("  mac.process worst-case < 200 ms (mac_storm bounded)")
mac.nvm_store()
verdict("testO_mac_storm",
        dr_after > dr_before and lc_final is not None
        and t_after_sys != t_before_sys and mac_proc_max < 200,
        dr_before=dr_before, dr_after=dr_after,
        mac_proc_max_ms=mac_proc_max)
