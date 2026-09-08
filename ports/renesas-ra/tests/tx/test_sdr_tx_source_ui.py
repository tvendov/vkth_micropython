"""Actual selector/loop/navigation methods with host LVGL doubles, not hardware."""
import ast
import types
import test_sdr_fm_controls as c


def run():
    c.METHODS.update({'open_tx_source_menu', 'toggle_tx_file_loop',
                      'open_pick_menu', 'close_pick_menu'})
    env = c.namespace()
    c.Widget.FLAG.FLOATING = 'floating'
    c.Widget.set_pos = lambda self, x, y: None
    c.Widget.center = lambda self: None
    env.update(PANEL=0xffffff, BTN_RX=1, CYAN_IN=2, DARK_TXT=0, GRAY=0)
    current = [None]
    env['lv'].screen_active = lambda: current[0]
    env['lv'].screen_load = lambda scr: current.__setitem__(0, scr)
    app, calls = c.new_app(env)
    app._trx_state, app._tx_mode = 'TX', 'FM'
    app._iq_file_profiles = (('R:USB', 'USB', '/usb48', '/usb24'),
                             ('R:FM', 'FM', '/fm48', '/fm24'))
    app._tx_source_settings = None
    app._tx_file_on = True
    requests = []
    app._request_tx_config = lambda **kw: requests.append(kw)
    app.open_settings()
    backend = app.ui.w['scr-settings']
    assert current[0] is backend
    app._set_widgets['tx-source-button'].events['click'](None)
    overlay, callbacks = env['_KEEP']['pick_menu']
    assert overlay in backend.children  # not the invisible HOME screen
    callbacks[1](None)  # actual R:USB callback, closes picker before queuing
    assert overlay.deleted and 'pick_menu' not in env['_KEEP']
    assert current[0] is backend and not backend.deleted
    assert requests == [{'source': ('R:USB', 'USB', '/usb48', 48000, 0, 0)}]
    assert app._tx_mode == 'FM'  # source never silently changes TX modulation
    app._tx_file_label = 'R:USB'
    app._refresh_tx_settings()
    assert app._set_widgets['tx-source'].text == 'SOURCE: R:USB  >'
    app._trx_pending = 'TX_CONFIG'
    app._refresh_tx_settings()
    assert 'disabled' in app._set_widgets['tx-source-button'].states
    assert not app.toggle_tx_file_loop()
    app._trx_pending = None

    # Execute the real file policy method; changing LOOP must not touch file I/O.
    cls = next(n for n in c.TREE.body if isinstance(n, ast.ClassDef) and n.name == '_IqFileSource')
    method = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == 'set_loop')
    ns = {}
    exec(compile(ast.fix_missing_locations(ast.Module(body=[method], type_ignores=[])), '<set_loop>', 'exec'), ns)
    source = types.SimpleNamespace(_feeding=True, _payload_bytes=16384, _bufs=(bytes(8192),), _loop=True)
    source.set_loop = types.MethodType(ns['set_loop'], source)
    app._iq_file = source
    owner = app._tx
    app._set_widgets['tx-loop-button'].events['click'](None)
    assert not app._iq_file_loop and not source._loop
    assert app._set_widgets['tx-loop'].text == 'ONCE' and app._tx is owner
    assert app.toggle_tx_file_loop() and source._loop
    assert not source.set_loop(False) and source._loop  # unchanged RX contract
    source._feeding = False  # EOF marker has already been committed
    assert not app.toggle_tx_file_loop() and app._iq_file_loop
    source._feeding = True
    source._payload_bytes = 4096
    assert app.toggle_tx_file_loop() and not source._loop
    assert not app.toggle_tx_file_loop() and not app._iq_file_loop

    app.open_tx_source_menu()
    overlay = env['_KEEP']['pick_menu'][0]
    app.close_settings()
    assert overlay.deleted and backend.deleted
    assert 'pick_menu' not in env['_KEEP'] and 'settings' not in env['_KEEP']
    assert not app._modal
    app._trx_state = 'RX'
    app._sync_tx_controls()
    assert app.ui.get('agc-title').text == 'AGC'
    print('PASS TX source UI: visible parent, actual callbacks, independent mode, labels,')
    print('pending guard, live loop/no restart, RX/EOF/short-file guards, parent cleanup, RX title.')
    print('Host doubles only; physical touch, rendering, DAC and RF not measured.')


if __name__ == '__main__':
    run()
