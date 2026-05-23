import lorawan, time

events = []
received = []
mac_ref = [None]

def ev_cb(packed, *_):
    events.append((time.ticks_us(), packed))
    tag = packed & 0xff
    if tag == 1 and mac_ref[0] is not None:
        try:
            pkt = mac_ref[0].recv()
            if pkt is not None:
                received.append(pkt)
        except Exception:
            pass

m = lorawan.Mac()
mac_ref[0] = m
m.set_event_callback(ev_cb)
m.lorawan_init()

if not m.is_joined():
    print('[FAIL] TC03_V3 not_joined_after_nvm_restore')
    raise SystemExit

m.set_class('C')
m.send(1, b'\x01', False)

t0 = time.ticks_us()
deadline = time.ticks_add(t0, 60000 * 1000)
while time.ticks_diff(deadline, time.ticks_us()) > 0:
    m.process()

m.set_class('A')

got_21 = any(port == 21 and data == b'\xaa' for (port, data) in received)
got_22 = any(port == 22 and data == b'\xbb' for (port, data) in received)
got_23 = any(port == 23 and data == b'\xcc' for (port, data) in received)
all_ok = got_21 and got_22 and got_23

preview = [(port, data.hex()) for (port, data) in received]
print('[%s] TC03_V3 received_count=%d got_21=%s got_22=%s got_23=%s preview=%s events=%d' %
      ('PASS' if all_ok else 'FAIL', len(received), got_21, got_22, got_23, preview, len(events)))
