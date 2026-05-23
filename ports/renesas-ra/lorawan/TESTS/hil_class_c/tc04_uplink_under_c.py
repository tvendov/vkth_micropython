import _test_common_c as tcc

m, events, received = tcc.setup_mac_class_c()
m.set_datarate(5)

joined, _, status = tcc.join_blocking(m)
if not joined:
    tcc.print_result('TC04_UPLINK_C', False,
                     reason='join_failed', status=status)
    raise SystemExit

m.set_class('C')
rc = m.send(1, b'\xAA', False)
tcc.class_c_pump(m, 8000)
m.set_class('A')

ok = (rc == 0)
tcc.print_result('TC04_UPLINK_C', ok,
                 send_rc=rc, received=len(received))
