import _test_common as tc

NAME = 'T09_ADR'
try:
    m, events = tc.setup_mac()
    m.set_adr(True)
    joined, dt_us, status = tc.join_blocking(m, datarate=5)
    if not joined:
        tc.print_result(NAME, False, reason='join_failed',
                        join_status=status, elapsed_us=dt_us)
        tc.dump_events(events)
        raise SystemExit

    dr_before = m.get_datarate()
    adr_before = m.get_adr()
    errors = 0
    for i in range(5):
        rc = m.send(1, b'\x01', False)
        if rc != 0:
            errors += 1
            print('send #%d rc=%d' % (i, rc))
        tc.pump(m, 3000)
    dr_after = m.get_datarate()

    tc.print_result(NAME, errors == 0, adr=m.get_adr(),
                    adr_before=adr_before, dr_before=dr_before,
                    dr_after=dr_after, send_errors=errors)
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
