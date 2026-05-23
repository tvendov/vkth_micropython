import _test_common_c as tcc

m, events, received = tcc.setup_mac_class_c()
m.set_datarate(5)

joined, _, status = tcc.join_blocking(m)
if not joined:
    tcc.print_result('TC03_DL_BURST', False,
                     reason='join_failed', status=status)
    raise SystemExit

m.send(1, b'\x00', False)
tcc.class_c_pump(m, 5000)

# Handshake window: master inserts 3 queue items via psql here.
# (LoRaWAN session reset on join flushes queue, so master must arm
#  AFTER join + trigger uplink completes — see tc02_verdict.md.)
print('TC03_JOINED — awaiting master to arm 3 DL frames')
tcc.class_c_pump(m, 20000)
print('TC03 transitioning to Class C')

m.set_class('C')
print('class_c: pumping 45s for 3-burst DLs on ports 21/22/23...')
tcc.class_c_pump(m, 45000)
m.set_class('A')

ports_received = sorted({port for (port, _) in received})
expected_ports = [21, 22, 23]
got_all = all(p in ports_received for p in expected_ports)
preview = [(port, data.hex()) for (port, data) in received[:6]]
tcc.print_result('TC03_DL_BURST', got_all,
                 received=len(received), ports=ports_received,
                 preview=preview)
