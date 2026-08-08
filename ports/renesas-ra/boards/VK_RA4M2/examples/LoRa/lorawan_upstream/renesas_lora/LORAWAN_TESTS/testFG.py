"""Tests F + G — variable payload sizes + DeviceTimeReq.

F: 4 uplinks at sizes 1, 11, 32, 51 bytes (15s spacing).
G: queue DeviceTimeReq, send uplink, read back SysTime."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_adr(False)

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
    print("  event:", ev)
mac.set_event_callback(ev_cb)

# ---------- Test F: payload size sweep ----------
print("=== Test F: payload sweep ===")
ok = 0
for sz in (1, 11, 32, 51):
    payload = bytes(range(sz))
    snap = len(events)
    print("[F] tx %d bytes" % sz)
    st = mac.send(1, payload, False)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    confirmed = any(e[0] == 'mcps_confirm' and e[1] == 0 for _, e in events[snap:])
    if confirmed: ok += 1
    print("[F] %dB: %s" % (sz, "OK" if confirmed else "FAIL"))
    time.sleep(7)
print("F: %d/4 sizes confirmed" % ok)

# ---------- Test G: DeviceTimeReq ----------
print()
print("=== Test G: DeviceTimeReq ===")
t_before = mac.get_sys_time()
print("sys_time before:", t_before)
st = mac.device_time_req()
print("device_time_req queued, st:", st)
snap = len(events)
mac.send(1, b"timeq", False)
end = time.ticks_add(time.ticks_ms(), 10000)
while time.ticks_diff(end, time.ticks_ms()) > 0:
    mac.process(); time.sleep_ms(20)
t_after = mac.get_sys_time()
print("sys_time after :", t_after)
print("events:", events[snap:])
mac.nvm_store()
