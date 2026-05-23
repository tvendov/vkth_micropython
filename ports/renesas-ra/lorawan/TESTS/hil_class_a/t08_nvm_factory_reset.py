import _test_common as tc
import lorawan

NAME = 'T08_NVM_FACTORY_RESET'
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
    m.nvm_factory_reset()

    if m.is_joined():
        tc.print_result(NAME, False, reason='still_joined_after_reset')
        raise SystemExit

    joined, dt_us, status = tc.join_blocking(m, datarate=5)
    tc.print_result(NAME, joined, elapsed_us=dt_us,
                    join_status=status, events=len(events))
    if not joined:
        tc.dump_events(events)
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
