import _test_common_c as tcc

m, events, received = tcc.setup_mac_class_c()
m.set_datarate(5)

joined, _, status = tcc.join_blocking(m)
if not joined:
    tcc.print_result('TC05_EXT_PASSIVE', False,
                     reason='join_failed', status=status)
    raise SystemExit

m.send(1, b'\x00', False)
tcc.class_c_pump(m, 5000)

m.set_class('C')
print('class_c: 3-min passive listen...')
tcc.class_c_pump(m, 180000)
m.set_class('A')

st = m.status()
no_crash = bool(st.get('stack_initialized')) and bool(st.get('joined'))
tcc.print_result('TC05_EXT_PASSIVE', no_crash,
                 received=len(received),
                 status_joined=st.get('joined'),
                 stack_init=st.get('stack_initialized'))
