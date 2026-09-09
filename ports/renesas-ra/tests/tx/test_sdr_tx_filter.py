"""Actual UI callbacks/persistence with doubles. No LVGL rendering or board I/O."""
import copy
import sys
import types

import test_sdr_fm_controls as controls
import test_sdr_tx_home as home


def run():
    env = controls.namespace()
    memory = bytearray(b'\xff' * 512)
    old = sys.modules.get('dataflash')
    sys.modules['dataflash'] = types.SimpleNamespace(
        read=lambda off, n: memory[off:off+n],
        write=lambda off, b: memory.__setitem__(slice(off, off+len(b)), b),
        erase_block=lambda b: memory.__setitem__(slice(b*64, (b+1)*64), b'\xff'*64))
    try:
        p = env['_fresh_params']()
        p['bw'].update(AM=9000, USB=2100, LSB=2700)
        # Keep valid per-mode RX values while selecting unrelated TX corners.
        p['bw'] = {m: env['BW_CHOICES'][m][-1] for m in env['MODES']}
        p['txbw'].update(AM=6000, USB=1800, LSB=2700)
        p['v'], p['txlevel'] = 72, 25
        env['save_params'](p)
        loaded = env['load_params']()
        assert loaded['bw'] == p['bw'] and loaded['txbw'] == p['txbw']
        assert loaded['v'] == 72 and loaded['txlevel'] == 25
        worst = copy.deepcopy(p)
        worst.update(f=40000000, cal=-200.0, again=64.0, iqa=1.5, iqp=-0.5,
            vfos=[[40000000, 'USB'] for _ in range(3)], a='SLOW', txdev=5000,
            txmic=1600, txlevel=100, txdepth=100, genfreq=30000, genlevel=100, genwave=2)
        env['save_params'](worst)
        payload_size = memory[4] | memory[5] << 8
        assert payload_size <= env['DF_PAYLOAD_LIMIT']
        print('TX/RX largest tested settings payload: %d / %d B' % (payload_size, env['DF_PAYLOAD_LIMIT']))
        env['save_params'](p)
        # Old records migrate to BASE without borrowing any RX bandwidth.
        import json
        record = json.loads(memory[6:6+(memory[4] | memory[5] << 8)])
        record.pop('TB')
        def put(obj):
            payload = json.dumps(obj).encode()
            memory[:6+len(payload)] = env['MAGIC'] + len(payload).to_bytes(2, 'little') + payload
        put(record)
        assert env['load_params']()['txbw'] == dict.fromkeys(env['TX_AF_MODES'], 0)
        record['TB'] = [9000, '2400', 2700]
        put(record)
        assert env['load_params']()['txbw'] == dict(AM=0, USB=0, LSB=2700)
    finally:
        if old is None: sys.modules.pop('dataflash', None)
        else: sys.modules['dataflash'] = old

    for mode in env['TX_AF_MODES']:
        for source in (None, 'R:USB', 'GEN'):
            app, calls = controls.new_app(env)
            app._trx_state, app._tx_mode = 'TX', mode
            app._tx_rx_snapshot = {}
            app._tx_file_label = source
            app._tx_source_settings = 'GEN' if source == 'GEN' else None
            app._tx.AUDIO_FILTER_API_VERSION = 1
            app._tx.audio_configure = lambda **kw: calls.append(kw)
            app._refresh_home_context = lambda: None
            app.open_pick_menu = lambda *a: setattr(app, 'picker', a)
            app.open_settings()
            assert app._set_widgets['tx-filter-button'].size == (464, 26)
            app._set_widgets['tx-filter-button'].events['click'](None)
            pick = app.picker[-1]
            before = copy.deepcopy(app.p)
            assert pick(2400 if mode != 'AM' else 4500)
            assert pick(0)  # coalesce a real BASE request, not 'no request'
            assert app.p == before and calls == []
            app._trx_pending = None
            assert app._service_tx_filter() and calls == [dict(audio_cutoff=0)]
            assert app._tx_file_label == source and app._tx_save_pending and app.save_timer is None
            assert app._request_tx_filter(1800 if mode != 'AM' else 6000)
            app._trx_pending = None
            assert app._service_tx_filter()
            assert app.p['bw'] == before['bw'] and app.p['v'] == before['v']
            assert app._home_bandwidth_text() != 'BASE'
            assert not app._request_tx_filter(9000)
            app._trx_pending = 'TX_CONFIG'
            assert not app._request_tx_filter(0)
            app._trx_pending = None
            saved = copy.deepcopy(app.p)
            def fail(**kw): raise ValueError('native refusal')
            app._tx.audio_configure = fail
            app._queue_rx_cleanup = lambda text: setattr(app, 'failure', text)
            assert app._request_tx_filter(0)
            app._trx_pending = None
            assert not app._service_tx_filter() and app.p == saved and 'native refusal' in app.failure
            app.close_settings()

    # HOME horizontal row uses the same setter, not RX set_bandwidth or TX restart.
    app, log, ui_env, Tx = home.fixture('AM')
    Tx.AUDIO_FILTER_API_VERSION = 1
    Tx.audio_configure = lambda self, **kw: log.append(('af', kw))
    app._refresh_tx_settings = lambda: None
    app._tx_save_pending = False
    app.p['txbw'] = dict.fromkeys(env['TX_AF_MODES'], 0)
    original_rx = copy.deepcopy(app.p['bw'])
    owner = app._tx
    app.open_filter_menu()
    assert app._mode_expanded == 6
    assert app.ui.get('btn-AM').label.text == 'BASE'
    before = list(log)
    ui_env['callback_factory'](app, 'CW', 4)(None)
    assert log == before and app._trx_pending == 'TX_FILTER'
    app._service_trx_pending()
    assert app._tx is owner and log[len(before):] == [('af', dict(audio_cutoff=6000))]
    assert app.ui.get('filter-value').text == '6k' and app.p['bw'] == original_rx
    # The HOME summary's existing state 5 still opens the modulation chooser.
    app.open_home_choices()
    assert app._mode_expanded == 5
    ui_env['callback_factory'](app, 'AM', 0)(None)
    assert app._mode_expanded == 1
    # A subsequent TX mode/source transaction passes the saved profile to IQTX.
    app.p['txbw']['USB'] = 2400
    assert app._request_tx_config(mode='USB')
    app._service_trx_pending()
    assert app._tx.kwargs['audio_cutoff'] == 2400 and app.p['txbw']['AM'] == 6000
    app.toggle_rx()
    app._service_trx_pending()
    assert app._trx_state == 'RX' and app.p['bw'] == original_rx
    assert app.p['txbw'] == dict(AM=6000, USB=2400, LSB=0)
    print('PASS TX AF HOME/BACKEND/persistence: modes/sources, BASE migration, atomic requests, RX independence')


if __name__ == '__main__':
    run()
