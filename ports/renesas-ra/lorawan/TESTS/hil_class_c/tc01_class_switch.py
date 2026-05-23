import _test_common_c as tcc

m, events, received = tcc.setup_mac_class_c()
m.set_datarate(5)

joined, _, status = tcc.join_blocking(m)
if not joined:
    tcc.print_result('TC01_CLASS_SWITCH', False,
                     reason='join_failed', status=status)
    raise SystemExit

class_a = m.get_class()
rc_c = m.set_class('C')
class_c = m.get_class()
rc_a = m.set_class('A')
class_back = m.get_class()

ok = (class_a == 'A' and class_c == 'C' and class_back == 'A'
      and rc_c == 0 and rc_a == 0)
tcc.print_result('TC01_CLASS_SWITCH', ok,
                 a=class_a, c=class_c, back=class_back,
                 rc_set_c=rc_c, rc_set_a=rc_a)
