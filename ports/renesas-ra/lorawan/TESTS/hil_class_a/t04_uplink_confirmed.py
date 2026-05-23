import _test_common as tc

NAME = 'T04_UPLINK_CONF'
try:
    m, events = tc.setup_mac()
    joined, dt_us, status = tc.join_blocking(m, datarate=5)
    if not joined:
        tc.print_result(NAME, False, reason='join_failed',
                        join_status=status, elapsed_us=dt_us)
        tc.dump_events(events)
        raise SystemExit

    events.clear()
    rc = m.send(1, bytes([0xC0, 0xFF]), True)
    if rc != 0:
        tc.print_result(NAME, False, send_rc=rc)
        raise SystemExit
    tc.pump(m, 8000)

    ack_ok = False
    for _, packed in events:
        tag, stat = tc.decode_event(packed)
        if tag == 'mcps_confirm' and stat == 'OK':
            ack_ok = True
            break
    tc.print_result(NAME, ack_ok, send_rc=rc, events=len(events))
    if not ack_ok:
        tc.dump_events(events)
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
