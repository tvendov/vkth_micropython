import _test_common as tc
import lorawan

NAME = 'T07B_NVM_RESUME'
try:
    events = []

    def ev_cb(packed, *_):
        import time
        events.append((time.ticks_us(), packed))

    m = lorawan.Mac()
    m.set_event_callback(ev_cb)
    m.lorawan_init()
    m.init_defaults()
    m.set_keys(tc.DEV_EUI, tc.JOIN_EUI, tc.APP_KEY)
    m.nvm_restore()

    st = m.status()
    already_joined = m.is_joined()

    rc = None
    if already_joined:
        rc = m.send(1, b'\xCC', False)
        tc.pump(m, 5000)

    tc.print_result(NAME, already_joined, status=st, send_rc=rc,
                    events=len(events))
    if not already_joined:
        print('nvm_restore did not restore joined state — check NVM contents')
        tc.dump_events(events)
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
