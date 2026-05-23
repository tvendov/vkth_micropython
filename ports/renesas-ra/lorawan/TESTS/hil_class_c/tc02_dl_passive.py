import _test_common_c as tcc

m, events, received = tcc.setup_mac_class_c()
m.set_datarate(5)

joined, _, status = tcc.join_blocking(m)
if not joined:
    tcc.print_result('TC02_DL_PASSIVE', False,
                     reason='join_failed', status=status)
    raise SystemExit

m.send(1, b'\x00', False)
tcc.class_c_pump(m, 5000)

m.set_class('C')
print('class_c: pumping 30s for passive DL on port 20...')
tcc.class_c_pump(m, 30000)
m.set_class('A')

got_dl = any(port == 20 and data == b'\xDE\xAD\xBE\xEF'
             for (port, data) in received)
preview = [(port, data.hex()) for (port, data) in received[:3]]
tcc.print_result('TC02_DL_PASSIVE', got_dl,
                 received_count=len(received), preview=preview)
