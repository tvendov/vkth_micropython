import _test_common as tc

NAME = 'T03_UPLINK_UNCONF_V2'
try:
    m, events = tc.setup_mac()
    if not m.is_joined():
        joined, dt_us, status = tc.join_blocking(m, datarate=5)
        if not joined:
            tc.print_result(NAME, False, reason='join_failed', join_status=status)
            raise SystemExit
    events.clear()
    n_sent = 0
    n_queue_err = 0
    for i in range(5):
        payload = bytes([i, 0xA5])
        rc = m.send(1, payload, False)
        if rc == 0:
            n_sent += 1
        else:
            n_queue_err += 1
            print('send #%d rc=%d' % (i, rc))
        tc.pump(m, 8000)
    confirms = sum(1 for _, p in events
                   if tc.decode_event(p) == ('mcps_confirm', 'OK'))
    passed = (n_sent == 5 and n_queue_err == 0)
    tc.print_result(NAME, passed, sent=n_sent, queue_err=n_queue_err,
                    ok_confirms=confirms, events=len(events), spacing_ms=8000)
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
