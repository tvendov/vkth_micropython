import _test_common as tc

NAME = 'T02_OTAA_DR_SWEEP'
try:
    try:
        with open('/flash/dr.txt') as f:
            dr = int(f.read().strip())
    except Exception:
        dr = 5
    if dr < 0 or dr > 5:
        raise ValueError('EU868 OTAA DR must be 0..5, got %d' % dr)

    m, events = tc.setup_mac()
    joined, dt_us, status = tc.join_blocking(m, datarate=dr)
    tc.print_result('T02_OTAA_DR%d' % dr, joined, dr=dr,
                    elapsed_us=dt_us, join_status=status, events=len(events))
    if not joined:
        tc.dump_events(events)
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
