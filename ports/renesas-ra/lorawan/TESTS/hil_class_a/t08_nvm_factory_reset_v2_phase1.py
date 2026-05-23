import lorawan, time
import _test_common as tc

NAME = 'T08_FACTORY_RESET_V2_PHASE1'
try:
    m = lorawan.Mac()
    m.lorawan_init()

    joined_before = m.is_joined()
    print('phase1 pre-reset is_joined=%s' % joined_before)

    if not joined_before:
        joined, _, status = tc.join_blocking(m, datarate=5)
        if not joined:
            print('[FAIL] %s pre-condition: cannot join, status=%s' % (NAME, status))
            raise SystemExit
        joined_before = True
        print('phase1 fresh-joined ok')

    m.nvm_factory_reset()
    joined_after = m.is_joined()

    print('phase1 post-factory-reset is_joined=%s (in-memory stale flag — expected True)' % joined_after)
    print('[INFO] %s phase1 complete — operator/slave must JLink reset before phase2' % NAME)
except SystemExit:
    pass
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
