import _test_common as tc

NAME = 'T06_LINK_CHECK'
try:
    m, events = tc.setup_mac()
    joined, dt_us, status = tc.join_blocking(m, datarate=5)
    if not joined:
        tc.print_result(NAME, False, reason='join_failed',
                        join_status=status, elapsed_us=dt_us)
        tc.dump_events(events)
        raise SystemExit

    m.link_check()
    rc = m.send(1, b'\x00', True)
    if rc != 0:
        tc.print_result(NAME, False, send_rc=rc)
        raise SystemExit
    tc.pump(m, 10000)

    result = m.last_link_check()
    if result is None:
        tc.print_result(NAME, False, reason='no_link_check_ans',
                        events=len(events))
        tc.dump_events(events)
    else:
        margin, gateways = result
        ok = (margin >= 0 and gateways >= 1)
        tc.print_result(NAME, ok, margin=margin, gateways=gateways)
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
