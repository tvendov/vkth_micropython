"""Test O / HW-9 v2 — combined MAC command storm.
Authorised rewrite: hardcoded keys; mac.set_datarate() (missing) replaced
with init_defaults() which pins MIB_CHANNELS_DATARATE=DR_0; nested
mac.stats() dict layout instead of legacy flat keys.
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
mac.init_defaults()             # sets MIB DR=0, ADR=true, etc.
mac.set_adr(False)              # override: ADR off for now (turned on later)

mac.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
mac.set_event_callback(ev_cb)

print("=== JOIN at DR0 ===")
T0 = time.ticks_ms()
mac.join(0)                      # join at DR=0 explicitly
deadline = time.ticks_add(T0, 30000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined(): break
    time.sleep_ms(20)
if not mac.is_joined():
    print("JOIN FAILED")
    raise SystemExit(1)
print("  joined at +%dms  DR=%d" % (time.ticks_diff(time.ticks_ms(), T0), mac.get_datarate()))

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
print("  DR before: %d  -> after: %d" % (dr_before, dr_after))
print("  sys_time before: %s  -> after: %s" % (t_before_sys, t_after_sys))
print("  last_link_check final:", lc_final)
print("  mac.process worst-case (Python-measured): %d ms" % mac_proc_max)
# Nested mac.stats() layout (current firmware):
print("  stats.mac.mac_process_max_us :", stats['mac']['mac_process_max_us'])
print("  stats.busy.busy_timeout_count:", stats['busy']['busy_timeout_count'])
print("  stats.spi.sx126x_wake_count  :", stats['spi']['sx126x_wake_count'])
print()
print("PASS criteria (printed for reference; final verdict from RESP):")
print("  dr_after > dr_before          (LinkADRReq applied)")
print("  lc_final not None             (LinkCheckAns received)")
print("  t_after_sys != t_before_sys   (DeviceTimeAns applied)")
print("  mac.process worst-case < 200 ms")
mac.nvm_store()
print("VERDICT_DR    : %s" % ("PASS" if dr_after > dr_before else "FAIL"))
print("VERDICT_LC    : %s" % ("PASS" if lc_final is not None else "FAIL"))
print("VERDICT_TIME  : %s" % ("PASS" if t_after_sys != t_before_sys else "FAIL"))
print("VERDICT_LAT   : %s" % ("PASS" if mac_proc_max < 200 else "FAIL"))
