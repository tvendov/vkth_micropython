import _test_common as tc

NAME = 'T01_OTAA_SF7'
try:
    m, events = tc.setup_mac()
    joined, dt_us, status = tc.join_blocking(m, datarate=5)
    tc.print_result(NAME, joined, dr=5, elapsed_us=dt_us,
                    join_status=status, events=len(events))
    if not joined:
        tc.dump_events(events)
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
