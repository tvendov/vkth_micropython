import lorawan
import _test_common as tc

NAME = 'T08_FACTORY_RESET_V2_PHASE2'
try:
    m = lorawan.Mac()
    m.lorawan_init()

    joined_after_cold_boot = m.is_joined()
    passed = (joined_after_cold_boot == False)

    tc.print_result(NAME, passed,
                    is_joined_after_cold_boot=joined_after_cold_boot,
                    expected=False,
                    note='factory_reset wiped NVM; cold boot must have no session')
except Exception as e:
    print('[FAIL] %s exc=%r' % (NAME, e))
