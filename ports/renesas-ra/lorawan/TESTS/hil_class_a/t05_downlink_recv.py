import _test_common as tc

NAME = 'T05_DL_RECV'
try:
    m, events = tc.setup_mac()
    joined, dt_us, status = tc.join_blocking(m, datarate=5)
    if not joined:
        tc.print_result(NAME, False, reason='join_failed',
                        join_status=status, elapsed_us=dt_us)
        tc.dump_events(events)
        raise SystemExit

    rc = m.send(1, b'\x00', False)
    if rc != 0:
        tc.print_result(NAME, False, send_rc=rc)
        raise SystemExit
    tc.pump(m, 8000)

    data = m.recv()
    got_dl = data is not None
    if got_dl:
        port, payload = data
        tc.print_result(NAME, True, port=port, payload_len=len(payload),
                        payload_hex=payload.hex())
    else:
        tc.print_result(NAME, False, reason='no_downlink', events=len(events))
        ind = sum(1 for _, p in events
                  if tc.decode_event(p)[0] == 'mcps_indication')
        print('mcps_indications=%d' % ind)
        tc.dump_events(events)
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
