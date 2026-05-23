import lorawan, time
import _test_common as tc

NAME = 'T05_DL_RECV_V2'
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

try:
    m = lorawan.Mac()
    mac_ref[0] = m
    m.set_event_callback(ev_cb)
    m.lorawan_init()

    if not m.is_joined():
        tc.print_result(NAME, False,
                        reason='not_joined_after_nvm_restore — master must pre-arm session via T07a/T01 then queue DL')
        raise SystemExit

    rc = m.send(1, b'\x00', False)
    if rc != 0:
        tc.print_result(NAME, False, reason='trigger_send_busy', send_rc=rc)
        raise SystemExit

    t0 = time.ticks_us()
    deadline = time.ticks_add(t0, 12000 * 1000)
    while time.ticks_diff(deadline, time.ticks_us()) > 0:
        m.process()

    if received:
        port, payload = received[0]
        tc.print_result(NAME, True, port=port, payload_len=len(payload),
                        payload_hex=payload.hex(), events=len(events),
                        received_count=len(received))
    else:
        ind = sum(1 for _, p in events
                  if tc.decode_event(p)[0] == 'mcps_indication')
        tc.print_result(NAME, False, reason='no_downlink',
                        mcps_indications=ind, events=len(events))
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
