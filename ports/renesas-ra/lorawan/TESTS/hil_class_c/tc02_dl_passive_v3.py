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
    print('[FAIL] TC02_V3 not_joined_after_nvm_restore — run TC02 v1 once first to populate NVM, then JLink reset and try again')
    raise SystemExit

m.set_class('C')

m.send(1, b'\x01', False)

t0 = time.ticks_us()
deadline = time.ticks_add(t0, 30000 * 1000)
while time.ticks_diff(deadline, time.ticks_us()) > 0:
    m.process()

m.set_class('A')

got_dl = any(port == 20 and data == b'\xDE\xAD\xBE\xEF'
             for (port, data) in received)
preview = [(port, data.hex()) for (port, data) in received[:3]]
print('[%s] TC02_V3 received_count=%d preview=%s events=%d' %
      ('PASS' if got_dl else 'FAIL', len(received), preview, len(events)))
