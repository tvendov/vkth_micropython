import _test_common as tc

NAME = 'T10_SOAK'
N_FRAMES = 6
INTERVAL_MS = 30000

try:
    m, events = tc.setup_mac()
    joined, dt_us, status = tc.join_blocking(m, datarate=5)
    if not joined:
        tc.print_result(NAME, False, reason='join_failed',
                        join_status=status, elapsed_us=dt_us)
        tc.dump_events(events)
        raise SystemExit

    events.clear()
    sent = 0
    queue_err = 0
    exc_count = 0
    for i in range(N_FRAMES):
        try:
            rc = m.send(1, bytes([i]), False)
            if rc == 0:
                sent += 1
            else:
                queue_err += 1
                print('send #%d rc=%d' % (i, rc))
        except Exception as e:
            exc_count += 1
            print('send #%d exc=%r' % (i, e))
        tc.pump(m, INTERVAL_MS)

    mac_errs = sum(1 for _, p in events
                   if tc.decode_event(p)[0] == 'mac_error')
    rejoins = sum(1 for _, p in events
                  if tc.decode_event(p)[0] == 'mlme_confirm')
    still_joined = m.is_joined()
    passed = (sent == N_FRAMES and queue_err == 0 and exc_count == 0
              and mac_errs == 0 and still_joined)
    tc.print_result(NAME, passed, sent=sent, queue_err=queue_err,
                    exc=exc_count, mac_errors=mac_errs,
                    mlme_confirms=rejoins, joined_after=still_joined,
                    events=len(events))
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
