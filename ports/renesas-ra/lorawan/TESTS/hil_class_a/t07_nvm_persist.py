import _test_common as tc

NAME = 'T07A_NVM_STORE'
try:
    m, events = tc.setup_mac()
    joined, dt_us, status = tc.join_blocking(m, datarate=5)
    if not joined:
        tc.print_result(NAME, False, reason='join_failed',
                        join_status=status, elapsed_us=dt_us)
        tc.dump_events(events)
        raise SystemExit

    rc1 = m.send(1, b'\xAA', False); tc.pump(m, 2000)
    rc2 = m.send(1, b'\xBB', False); tc.pump(m, 2000)

    m.nvm_store()
    st = m.status()
    tc.print_result(NAME, True, send_rc=[rc1, rc2],
                    is_joined=m.is_joined(), status=st)
    print('Now reset board and run t07b_nvm_resume.py')
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
