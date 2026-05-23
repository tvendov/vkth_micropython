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
    print('[FAIL] TC05_V3 not_joined_after_nvm_restore')
    raise SystemExit

m.set_class('C')

def pump_ms(ms):
    t0 = time.ticks_us()
    deadline = time.ticks_add(t0, ms * 1000)
    while time.ticks_diff(deadline, time.ticks_us()) > 0:
        m.process()

N = 10
for i in range(N):
    try:
        m.send(1, bytes([i + 1]), False)
    except Exception as e:
        print('send #%d exc: %s' % (i + 1, e))
    pump_ms(15000)

m.set_class('A')

ports_recv = sorted({port for (port, data) in received})
preview = [(p, d.hex()) for (p, d) in received]

passed = len(received) >= 8
print('[%s] TC05_V3 received_count=%d unique_ports=%s preview=%s events=%d uplinks=%d' %
      ('PASS' if passed else 'FAIL', len(received), ports_recv, preview, len(events), N))
