"""Actual HOME methods + native-owner doubles. No board, LVGL render or RF proof."""
import ast
import copy
import sys
import types

import test_sdr_tx_switch as switch
import test_sdr_fm_controls as controls


METHODS = {
    '_home_bandwidth_text', '_refresh_home_context', '_update_home_summary',
    '_set_mode_bar', 'open_home_choices', 'set_mode', 'open_agc_menu',
    'open_tx_source_menu', 'open_step_menu', 'open_filter_menu',
    'open_step_controls', '_open_bottom_choices', 'tune', 'fine',
    'open_entry', 'close_entry', '_entry_hz', 'toggle_spectrum_view',
    'paint_spectrum',
}


class Widget(controls.Widget):
    FLAG = types.SimpleNamespace(HIDDEN='hidden')

    def __init__(self):
        super().__init__()
        self.flags = set()
        self.label = controls.Widget()

    def get_child(self, index):
        assert index == 0
        return self.label

    def add_flag(self, flag): self.flags.add(flag)
    def remove_flag(self, flag): self.flags.discard(flag)
    def has_flag(self, flag): return flag in self.flags


def install_ui(app):
    env = controls.namespace()
    env.update(BTN_RX=0, GREEN=1, DARK_TXT=0, GRAY2=0, fmt_bw=lambda v: '%gk' % (v / 1000))
    env['lv'].obj = Widget
    cls = next(n for n in switch.TREE.body if isinstance(n, ast.ClassDef) and n.name == 'SdrApp')
    methods = [copy.deepcopy(n) for n in cls.body if isinstance(n, ast.FunctionDef) and n.name in METHODS]
    assert {n.name for n in methods} == METHODS
    tree = ast.Module(body=[ast.ClassDef(name='UI', bases=[], keywords=[], body=methods, decorator_list=[])], type_ignores=[])
    exec(compile(ast.fix_missing_locations(tree), str(switch.UI_SOURCE), 'exec'), env)
    for name in METHODS:
        setattr(app, name, types.MethodType(getattr(env['UI'], name), app))
    items = app.ui.items
    app.ui.get = lambda name: items.setdefault(name, Widget())
    app.p['vfos'] = [[app.p['f'], app.p['m']], [7100000, 'LSB'], [30000000, 'FM']]
    app.p['bw'] = dict(env['MODE_BW'])
    app._mode_expanded = 0
    app.cur_bw = lambda: app.p['bw'][app.p['m']]
    app.update_entry_digits = app.update_entry_bands = lambda: None
    app.touch_params = lambda: None
    app._set_modal = lambda value: setattr(app, '_modal', value)
    app.open_pick_menu = lambda title, items, current, pick: setattr(app, 'picker', (title, items, current, pick))
    app._iq_file_profiles = (
        ('R:AM', 'AM', '/flash/am48.sdriq', '/flash/am24.sdriq'),
        ('R:USB', 'USB', '/flash/usb48.sdriq', '/flash/usb24.sdriq'),
        ('R:LSB', 'LSB', '/flash/lsb48.sdriq', '/flash/lsb24.sdriq'),
        ('R:FM', 'FM', '/flash/fm48.sdriq', '/flash/fm24.sdriq'),
        ('R:CW', 'CW', '/flash/cw48.sdriq', '/flash/cw24.sdriq'))
    app._iq_file_loop = True
    # Compile the real nested modulation callback, not a second implementation.
    wire = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == '_wire')
    callback = copy.deepcopy(next(n for n in ast.walk(wire) if isinstance(n, ast.FunctionDef) and n.name == 'mode_cb'))
    factory = ast.parse('def callback_factory(self, m, i):\n    pass').body[0]
    factory.body = [callback, ast.Return(value=ast.Name(id='mode_cb', ctx=ast.Load()))]
    exec(compile(ast.fix_missing_locations(ast.Module(body=[factory], type_ignores=[])), str(switch.UI_SOURCE), 'exec'), env)
    return env


def fixture(mode='AM'):
    app, log = switch.new_app(mode)
    env = install_ui(app)
    class Tx:
        AM, FM, CW, USB, LSB = switch.TX_MODES
        owned = False
        fail_release = False
        fail_start = False
        fail_construct = False

        def __init__(self, **kwargs):
            assert not Tx.owned and not app.be.running
            Tx.owned = True
            log.append(('construct', kwargs))
            if Tx.fail_construct:
                raise MemoryError('mock LUT allocation')
            self.kwargs = kwargs
            self.running = False

        def status(self):
            return dict(owned=True, running=self.running, quiesced=not self.running,
                        outputs_enabled=True, error=0, i_code=2048, q_code=2048, keyed=False)

        def start(self):
            if Tx.fail_start:
                raise RuntimeError('mock start refusal')
            self.running = True
            log.append('tx_start')

        def stop(self):
            self.running = False
            log.append('tx_stop')

        def deinit(self): log.append('tx_deinit')

        @staticmethod
        def release():
            log.append('release')
            if Tx.fail_release:
                raise RuntimeError('mock release refusal')
            Tx.owned = False

        def file_service(self):
            log.append('feed')
            return 128

    class File:
        error, eof = None, False
        fail = False
        _attached, _feeding, _file = False, False, None

        def start(self, tx, point, rate, loop, path):
            assert not tx.running and loop is app._iq_file_loop
            if self.fail:
                raise OSError('missing FILE')
            self._attached, self._feeding, self._file = True, True, object()
            log.append(('prefill', point, rate, loop, path))

        def stop(self):
            self._attached, self._feeding, self._file = False, False, None
            log.append('file_stop')

        def _refill(self, index): log.append(('refill', index))
        def poll(self): return (0, 1)

    app._iq_file = File()
    sys.modules['machine'] = types.SimpleNamespace(IQTX=Tx)
    app.toggle_rx()
    app._service_trx_pending()
    assert app._trx_state == 'TX' and Tx.owned
    return app, log, env, Tx


def test_home_mode_frequency_and_restore():
    app, log, env, unused = fixture()
    original = copy.deepcopy(app.p)
    original_lo = app._lo_hz
    app.open_home_choices()
    assert app._mode_expanded == 5
    assert app.ui.get('home-summary').text == 'AM | FIX | 1 kHz'
    assert app.ui.get('brand-title').text == 'SDR TRANSMITTER'
    assert app.ui.get('vfo-alt-0').has_flag('hidden')
    assert 'disabled' in app.ui.get('vfo-a').states
    callback = env['callback_factory'](app, 'AM', 0)
    callback(None)  # selected HOME mode chip opens modulation choices
    assert app._mode_expanded == 1
    assert app.ui.get('btn-CW').has_flag('hidden')
    for name in ('AM', 'USB', 'LSB', 'FM'):
        assert not app.ui.get('btn-' + name).has_flag('hidden')
    before = list(log)
    env['callback_factory'](app, 'USB', 2)(None)
    assert app._trx_pending == 'TX_CONFIG' and app._trx_state == 'TX'
    assert log == before and app.p['m'] == 'AM'  # touch does no hardware work
    assert app.ui.get('rx-button').label.text == 'WAIT'
    assert not app._request_tx_config(mode='LSB')  # no overlapping requests
    app._service_trx_pending()
    assert app._tx_mode == app.p['m'] == 'USB'
    assert log[len(before):] == ['tx_stop', 'tx_deinit', 'release',
        ('clock', 1, 3500000), ('construct', {'mode': 'USB', 'audio_gain': 100,
        'am_depth': 50, 'amplitude': 819}), 'tx_start']
    for method, delta in ((app.tune, 1000), (app.fine, -100)):
        old = app.p['f']
        assert method(delta)
        assert app.p['f'] == old
        app._service_trx_pending()
        assert app.p['f'] == old + delta and ('clock', 1, old + delta) in log
    app.open_entry()
    app.entry = '04000000'
    app.close_entry(True)
    app._service_trx_pending()
    assert app.p['f'] == 4000000 and ('clock', 1, 4000000) in log
    app.open_step_menu()
    assert app._mode_expanded == 4
    env['callback_factory'](app, 'AM', 0)(None)
    assert app.p['s'] == env['STEPS'][0][0]
    app.open_filter_menu()
    assert app.picker[0] == 'TX FILTER: FIXED'
    assert app.p['bw'] == original['bw'] and app.p['vfos'] == original['vfos']
    assert app._lo_hz == original_lo
    app.toggle_rx()
    app._service_trx_pending()
    assert app._trx_state == 'RX' and app.p == original
    assert app.ui.get('brand-title').text == 'SDR RECEIVER'
    assert not app.ui.get('vfo-alt-0').has_flag('hidden')
    assert 'disabled' not in app.ui.get('vfo-a').states
    assert 'FIX' not in app.ui.get('home-summary').text
    assert log[-2:] == [('clock', 1, original_lo * 4), 'start_rx']
    print('PASS HOME callbacks: mode/step/filter/keypad/arrows, deferred x1 clock, independent RX restore')


def test_source_and_mode_are_independent():
    app, log, env, unused = fixture('USB')
    app.open_agc_menu()
    title, items, current, pick = app.picker
    assert title == 'TX AUDIO SOURCE' and current is None
    assert [name for _, name in items] == ['MIC', 'R:AM', 'R:USB', 'R:LSB', 'R:FM']
    pick(items[1][0])
    app._service_trx_pending()
    assert app._tx.kwargs == {'mode': 'USB', 'file_mode': 'AM', 'file_tune': 0,
                             'file_gain': 100, 'audio_gain': 100, 'am_depth': 50, 'amplitude': 819}
    assert app.ui.get('scope-view').text == 'AF R:AM'
    source = app._tx_source_settings
    for mode in ('LSB', 'FM', 'AM'):
        assert app.set_mode(mode)
        before = len(log)
        app._service_tx_file()  # keep feeding during pending UI->worker interval
        assert 'feed' in log[before:]
        app._service_trx_pending()
        assert app._tx.kwargs['mode'] == mode and app._tx.kwargs['file_mode'] == 'AM'
        assert app._tx_source_settings == source and app._iq_file_loop
        construct = max(i for i, event in enumerate(log) if isinstance(event, tuple) and event[0] == 'construct')
        recent = log[construct:]
        prefill = next(i for i, event in enumerate(recent) if isinstance(event, tuple) and event[0] == 'prefill')
        assert prefill < recent.index('frame_before_start') < recent.index('tx_start')
    app.open_tx_source_menu()
    app.picker[3](items[-1][0])
    app._service_trx_pending()
    assert app._tx.kwargs['file_mode'] == 'FM' and app._tx.kwargs['file_tune'] == -6000
    assert app._tx.kwargs['mode'] == 'AM'
    assert app._request_tx_config(source=None)
    app._service_trx_pending()
    assert not app._tx_file_on and app._tx.kwargs == {
        'mode': 'AM', 'audio_gain': 100, 'am_depth': 50, 'amplitude': 409}
    app.open_tx_source_menu()
    app.picker[3](items[2][0])  # R:USB decoder feeding the AM modulator
    app._service_trx_pending()
    assert app._tx.kwargs['file_mode'] == 'USB' and app._tx.kwargs['mode'] == 'AM'
    assert app._tx.kwargs['file_gain'] == 75  # before decoder limiter, not TX LEVEL
    assert app._request_tx_config(source=None)
    app._service_trx_pending()
    assert app.ui.get('scope-view').text == 'AF MIC'
    print('PASS MIC/real FILE selector, independent decoder/RF modes, FM offset, LOOP, prefill and AF label')


def test_failures_are_closed():
    for failure in ('clock', 'file', 'release', 'construct', 'start'):
        app, log, env, tx = fixture()
        before = len(log)
        source = ('R:AM', 'AM', '/missing.sdriq', 48000, 0, 0)
        assert app._request_tx_config(mode='USB', hz=3501000, source=source)
        if failure == 'clock': app._clock_failures = [False, True]
        if failure == 'file': app._iq_file.fail = True
        if failure == 'release': tx.fail_release = True
        if failure == 'construct': tx.fail_construct = True
        if failure == 'start': tx.fail_start = True
        app._service_trx_pending()
        assert app._trx_state != 'TX' and app._trx_error
        assert 'tx_start' not in log[before:]
        if failure == 'release':
            assert app._trx_pending == 'RX' and tx.owned
            assert all(not isinstance(e, tuple) or e[0] not in ('construct', 'clock') for e in log[before:])
            tx.fail_release = False
            app._service_trx_pending()
        assert not tx.owned and app._trx_state == 'RX'
        assert app.p['m'] == 'AM' and app.p['f'] == 3500000
        assert app.ui.get('smeter-value').text.startswith('ERR: ')
    app, log, env, tx = fixture()
    before = list(log)
    assert not app._request_tx_config(mode='CW')
    assert not app._request_tx_config(hz=499999)
    assert not app._request_tx_config(hz=40000001)
    assert log == before and app._trx_state == 'TX' and tx.owned
    print('PASS clock/FILE/release/allocation/start failures, invalid choices: no fallback MIC or overlapping owner')


def main():
    assert 'self.w["spectral-bin-%d" % i]' not in switch.SOURCE
    assert 'self.bins.append' not in switch.SOURCE
    app, _, _, _ = fixture('AM')
    app._spec_native = True
    app._spec_lcd = types.SimpleNamespace(spectrum_update=lambda *args: (_ for _ in ()).throw(
        AssertionError('startup must not publish fabricated bins')))
    app.paint_spectrum()
    print('PASS no legacy spectrum children or startup demo publication')
    previous = sys.modules.get('machine')
    try:
        test_home_mode_frequency_and_restore()
        test_source_and_mode_are_independent()
        test_failures_are_closed()
    finally:
        if previous is None: sys.modules.pop('machine', None)
        else: sys.modules['machine'] = previous
    print('HOST ONLY: actual Python methods; touch/render, native device behavior and RF NOT VERIFIED.')


if __name__ == '__main__':
    main()
