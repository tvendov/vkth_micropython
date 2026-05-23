import _test_common as tc

NAME = 'T09_ADR_V3'
try:
    m, events = tc.setup_mac()

    if not m.is_joined():
        joined, dt_us, status = tc.join_blocking(m, datarate=5)
        if not joined:
            tc.print_result(NAME, False, reason='join_failed', join_status=status)
            raise SystemExit

    m.set_datarate(5)
    m.set_adr(True)

    events.clear()
    dr_before = m.get_datarate()
    adr_before = m.get_adr()
    errors = 0
    N = 5
    for i in range(N):
        rc = m.send(1, b'\x01', False)
        if rc != 0:
            errors += 1
            print('send #%d rc=%d' % (i, rc))
        tc.pump(m, 10000)

    dr_after = m.get_datarate()
    mlme_indications = sum(1 for _, p in events
                           if tc.decode_event(p)[0] == 'mlme_indication')
    mac_errors = sum(1 for _, p in events
                     if tc.decode_event(p)[0] == 'mac_error')
    confirms = sum(1 for _, p in events
                   if tc.decode_event(p) == ('mcps_confirm', 'OK'))

    passed = (errors == 0 and m.get_adr() == True)
    tc.print_result(NAME, passed,
                    adr=m.get_adr(), adr_before=adr_before,
                    dr_before=dr_before, dr_after=dr_after,
                    send_errors=errors, sent=N,
                    spacing_ms=10000,
                    ok_confirms=confirms,
                    mlme_indications=mlme_indications,
                    mac_errors=mac_errors,
                    events=len(events))
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
