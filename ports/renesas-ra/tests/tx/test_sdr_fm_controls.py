"""Execute extracted SDR FM UI/persistence methods with host-only mocks.

No LVGL, dataflash hardware, COM or J-Link. Actual Python source is extracted;
ADC/DAC behavior is covered separately by the portable C tests and future HIL.
"""
import ast
import gc
import json
import sys
import types

from test_sdr_tx_switch import SOURCE, TREE, UI_SOURCE


CONSTANTS = {
    'DEFAULTS', 'DEF_VFOS', 'TARGETS', 'F_MIN', 'F_MAX', 'MODES', 'STEPS',
    'MODE_BW', 'BW_CHOICES', 'AGC_MODES', 'AGC_TARGET_MIN', 'AGC_TARGET_MAX',
    'TX_AF_MODES', 'TX_AF_CHOICES',
    'CAL_PPM_MIN', 'CAL_PPM_MAX', 'MAGIC', 'DF_BLOCK', 'DF_LIMIT',
    'DF_HEADER_BYTES', 'DF_PAYLOAD_LIMIT', 'SAVE_DELAY_MS',
    'TRX_RX', 'TRX_RX_OFF', 'TRX_TX', 'TRX_TO_TX', 'TRX_TO_RX', 'TRX_FAULT',
}
METHODS = {
    '_audio_controls', '_tx_gain_keys', '_apply_audio_gain', '_service_tx_audio',
    '_tx_filter_controls', '_request_tx_filter', '_service_tx_filter', 'open_tx_filter_menu',
    '_home_bandwidth_text',
    '_fm_controls', '_sync_tx_controls', '_paint_tx_status', '_apply_fm_gain',
    '_gain_spec', '_gain_available', '_paint_gain_pin', '_apply_gain',
    '_refresh_gains', '_bind_active_slider', 'touch_params', 'set_volume', 'update_vol',
    '_refresh_tx_settings', '_build_tx_settings', 'open_settings', 'close_settings',
    '_drop_settings_screen', '_build_route', '_refresh_route', 'open_route_menu',
    '_drop_route_screen', 'close_route_menu',
}


class Widget:
    FLAG = types.SimpleNamespace(SCROLLABLE='scroll', HIDDEN='hidden')

    def __init__(self, parent=None):
        self.text = ''
        self.value = 0
        self.states = set()
        self.children = []
        self.events = {}
        self.deleted = False
        if parent is not None:
            parent.children.append(self)

    def set_text(self, text): self.text = text
    def get_text(self): return self.text
    def add_state(self, state): self.states.add(state)
    def remove_state(self, state): self.states.discard(state)
    def set_value(self, value, animate): self.value = value
    def set_range(self, low, high): self.range = (low, high)
    def set_style_text_color(self, color, selector): self.color = color
    def set_size(self, w, h): self.size = (w, h)
    def set_width(self, w): self.width = w
    def add_flag(self, flag): pass
    def remove_flag(self, flag): pass
    def add_event_cb(self, cb, event, arg): self.events[event] = cb
    def delete(self):
        self.deleted = True
        for child in self.children: child.delete()
    def __getattr__(self, name):
        if name.startswith(('set_style_', 'set_flex_')):
            return lambda *args: None
        raise AttributeError(name)


class Timer:
    def __init__(self, callback): self.callback = callback
    def set_repeat_count(self, count): self.count = count
    def set_period(self, period): self.period = period
    def reset(self): pass


def namespace():
    nodes = []
    for node in TREE.body:
        if isinstance(node, ast.Assign):
            names = {n.id for t in node.targets for n in ast.walk(t) if isinstance(n, ast.Name)}
            if names & CONSTANTS:
                nodes.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name in ('_fresh_params', 'load_params', 'save_params'):
            nodes.append(node)
    cls = next(n for n in TREE.body if isinstance(n, ast.ClassDef) and n.name == 'SdrApp')
    methods = [n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name in METHODS]
    assert {n.name for n in methods} == METHODS
    nodes.append(ast.ClassDef(name='App', bases=[], keywords=[], body=methods, decorator_list=[]))
    def label(parent, text, *args):
        result = Widget(parent)
        result.set_text(text)
        return result
    def button(parent, w, h, *args, **kw):
        result = Widget(parent)
        result.set_size(w, h)
        return result
    env = {'_KEEP': {}, 'WHITE': 0xffffff, 'BORDER': 0x444444,
           'GRAY': 0x888888, 'CYAN_RX': 0x00aaaa, 'PANEL2': 0xffffff, 'BG_RX': 0,
           'gc': gc, '_base': lambda w: w, '_flex': lambda w, *a: w,
           'fmt_bw': lambda hz: '%gk' % (hz / 1000),
           '_lbl': label, '_btn': button, 'font': lambda n: n,
           'lv': types.SimpleNamespace(color_hex=lambda c: c,
                obj=Widget, label=Widget, button=Widget, screen_load=lambda scr: None,
                OPA=types.SimpleNamespace(COVER=255),
                FLEX_FLOW=types.SimpleNamespace(ROW=1, COLUMN=2),
                FLEX_ALIGN=types.SimpleNamespace(START=1, END=2, CENTER=3, SPACE_BETWEEN=4),
                EVENT=types.SimpleNamespace(CLICKED='click', SHORT_CLICKED='short', LONG_PRESSED_REPEAT='repeat'),
                STATE=types.SimpleNamespace(DISABLED='disabled', CHECKED='checked'),
                timer_create=lambda cb, period, arg: Timer(cb))}
    exec(compile(ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[])), str(UI_SOURCE), 'exec'), env)
    return env


def new_app(env):
    env['_KEEP'].clear()
    app = env['App']()
    app.p = env['_fresh_params']()
    app._trx_state = 'RX'
    app._tx_mode = None
    app._tx_file_label = None
    app._iq_file_loop = True
    app._gain_context_fm = False
    app._gain_context_tx = False
    app._settings_tx = False
    app._tx_status = {'amplitude': 800}
    app._tx_settings_cache = None
    app._trx_pending = None
    app._tx_audio_request = None
    app._tx_filter_request = None
    app._tx_rx_snapshot = None
    app._active_gain = 'AF'
    app._rx_active_gain = 'AF'
    app._gain_candidate = 'AF'
    app._gain_widgets = {}
    app._tx_save_pending = False
    app._tx_diag = None
    app._trx_error = None
    app._squelch = 0
    app.save_timer = None
    widgets = {}
    app.ui = types.SimpleNamespace(w=widgets, get=lambda name: widgets.setdefault(name, Widget()))
    app._set_widgets = {}
    app._set_lbls = {}
    app._set_live_cache = [None] * 6
    app._end_verify_scroll_gate = lambda value: None
    app._set_modal = lambda value: setattr(app, '_modal', value)
    app._consume_status = lambda: None
    app._refresh_settings = lambda: None
    app._closed = 0
    def close():
        app._closed += 1
        env['_KEEP'].pop('gains_panel', None)
    app.close_gains_panel = close
    app.update_rx = app._sync_tx_controls
    calls = []
    app.be = types.SimpleNamespace(set_volume=lambda value: calls.append(('RX', value)))
    app._tx = types.SimpleNamespace(fm_configure=lambda **kw: calls.append(kw))
    app._tx_source_settings = None
    return app, calls


def test_controls(env):
    app, calls = new_app(env)
    app._trx_state, app._tx_mode = 'TX', 'FM'
    app._sync_tx_controls()
    assert app._active_gain == 'TX'
    assert app.ui.get('vol-label').text == 'PWR'
    assert app.ui.get('vol-value').text == '40%'
    assert 'disabled' not in app.ui.get('agc-pill').states
    assert app.ui.get('agc-value').text == 'MIC'
    for key in ('AF', 'AGC', 'SQL'):
        assert not app._gain_available(key)
        app._apply_gain(key, 99)
    assert calls == []
    for key, value, expected in (
        ('DEV', 50, {'deviation_hz': 5000, 'mic_gain': 100, 'amplitude': 819}),
        ('MIC', 35, {'deviation_hz': 5000, 'mic_gain': 350, 'amplitude': 819}),
        ('TX', 100, {'deviation_hz': 5000, 'mic_gain': 350, 'amplitude': 2047}),
        ('TX', 0, {'deviation_hz': 5000, 'mic_gain': 350, 'amplitude': 0}),
    ):
        assert app._apply_gain(key, value)
        assert calls[-1] == expected
    assert app.p['v'] == 72 and app._tx_save_pending and app.save_timer is None
    before = dict(app.p)
    def reject(**kw): raise ValueError('mock invalid control')
    app._tx.fm_configure = reject
    assert not app._apply_gain('DEV', 25)
    assert app.p == before and 'mock invalid control' in app._trx_error
    app._trx_state, app._tx_mode = 'RX', None
    app._sync_tx_controls()
    assert app._active_gain == 'AF' and app.ui.get('vol-value').text == '72%'
    assert not app._tx_save_pending and app.save_timer is not None
    assert 'disabled' not in app.ui.get('agc-pill').states
    # No silently writable RX volume in other TX modes either.
    app._trx_state, app._tx_mode = 'TX', 'USB'
    app._sync_tx_controls()
    assert 'disabled' in app.ui.get('vol-slider').states
    assert not app._apply_gain('AF', 100) and app.p['v'] == 72
    print('PASS FM UI: separate controls, 0/100% TX level, rollback, RX restore and non-FM guards')


def test_flash_deferred(env):
    app, calls = new_app(env)
    original = env['save_params']
    saved = []
    env['save_params'] = lambda p: saved.append(dict(p))
    try:
        app.touch_params()
        pending = app.save_timer
        app._trx_state, app._tx_mode = 'TX', 'FM'
        pending.callback(pending)  # an old RX save expires after entering TX
        assert saved == [] and app._tx_save_pending and app.save_timer is None
        app._trx_state = 'RX'
        app._sync_tx_controls()
        app.save_timer.callback(app.save_timer)
        assert len(saved) == 1 and not app._tx_save_pending
    finally:
        env['save_params'] = original
    print('PASS FM persistence scheduling: no flash save in TX; deferred save after RX')


def test_persistence(env):
    memory = bytearray(b'\xff' * 512)
    erased = []
    def write(offset, data): memory[offset:offset + len(data)] = data
    def erase(block):
        erased.append(block)
        memory[block * 64:(block + 1) * 64] = b'\xff' * 64
    df = types.SimpleNamespace(read=lambda off, n: memory[off:off+n], write=write, erase_block=erase)
    previous = sys.modules.get('dataflash')
    sys.modules['dataflash'] = df
    try:
        p = env['_fresh_params']()
        p.update(txdev=5000, txmic=1600, txlevel=100, txdepth=100)
        env['save_params'](p)
        loaded = env['load_params']()
        assert [loaded[k] for k in ('txdev', 'txmic', 'txlevel')] == [5000, 1600, 100]
        assert loaded['txdepth'] == 100
        length = memory[4] | memory[5] << 8
        assert length <= 506 and max(erased) < 8
        # Worst-length legal field values still fit the existing record reserve.
        p.update(f=40000000, cal=-200.0, again=64.0, iqa=1.5, iqp=-0.5,
                 vfos=[[40000000, 'USB'] for _ in range(3)], a='SLOW')
        env['save_params'](p)
        assert (memory[4] | memory[5] << 8) <= 506
        # Backward compatibility: old records without any TX fields.
        old = json.loads(memory[6:6 + (memory[4] | memory[5] << 8)])
        for k in ('txdev', 'txmic', 'txlevel', 'txdepth'): old.pop(k)
        payload = json.dumps(old).encode()
        write(0, b'SDR1' + bytes((len(payload) & 255, len(payload) >> 8)) + payload)
        loaded = env['load_params']()
        assert [loaded[k] for k in ('txdev', 'txmic', 'txlevel')] == [2500, 100, 40]
        assert loaded['txdepth'] == 50
    finally:
        if previous is None: sys.modules.pop('dataflash', None)
        else: sys.modules['dataflash'] = previous
    print('PASS FM dataflash record: roundtrip, old-record defaults and 512-byte bound')


def test_backend_context(env):
    app, calls = new_app(env)
    rx_builds = []
    def rx_build():
        rx_builds.append(True)
        return Widget()
    app._build_settings = rx_build
    app._active_gain = app._gain_candidate = 'SQL'
    app._trx_state = 'TO_TX'
    app._sync_tx_controls()
    assert app._active_gain == 'SQL'
    app._trx_state, app._tx_mode = 'TX', 'FM'
    app._sync_tx_controls()
    assert app._rx_active_gain == 'SQL' and app._active_gain == 'TX'
    assert app.ui.get('agc-title').text == 'SOURCE'
    app.open_route_menu()  # actual SDR -> route path, now permitted during TX
    route = app.ui.w['scr-route']
    assert app._route_widgets['backend'].text == 'BACKEND TX  >'
    assert 'disabled' in app._route_widgets['rt-button0'].states
    assert 'disabled' in app._route_widgets['cal-buttons'][0].states
    app._route_widgets['backend-button'].events['click'](None)
    panel = app.ui.w['scr-settings']
    assert route.deleted and app.ui.w['scr-route'] is None
    assert app._settings_tx and not rx_builds and 'settings' in env['_KEEP']
    assert set(app._set_widgets) == {'tx-MIC', 'tx-DEV', 'tx-TX', 'tx-source',
                                    'tx-source-button', 'tx-loop', 'tx-loop-button'}
    assert app._set_widgets['tx-DEV'].text == '+/-2.5k'
    # Actual row callback -> actual Python FM setter -> mocked native boundary.
    dev_row = panel.children[3]
    dev_row.children[-1].events['short'](None)
    assert app.p['txdev'] == 2600 and calls[-1]['deviation_hz'] == 2600
    assert app._set_widgets['tx-DEV'].text == '+/-2.6k'
    dev_row.children[-1].events['repeat'](None)
    assert app.p['txdev'] == 2700
    app._refresh_tx_settings({'dsp_budget_cycles': 1000, 'dsp_last_cycles': 200,
                             'dsp_max_cycles': 300, 'audio_peak': 99,
                             'i_enabled': True, 'q_enabled': False})
    assert 'DSP 20% max 30%' in app._set_lbls['tx-live'].text
    assert 'I ON / Q OFF' in app._set_lbls['tx-live'].text
    # Repeated open is harmless; BACK destroys only the UI, not the TX owner.
    owner = app._tx
    app.open_settings()
    assert app.ui.w['scr-settings'] is panel
    panel.children[0].children[-1].events['click'](None)
    assert panel.deleted and 'settings' not in env['_KEEP']
    assert app._trx_state == 'TX' and app._tx is owner
    # Other modes also get TX, not RX VERIFY. Unsupported live control is disabled.
    app._tx_mode = 'USB'
    app._tx_rx_snapshot = {}
    app._sync_tx_controls()
    assert app.ui.get('vol-label').text == 'PWR'
    assert app.ui.get('vol-value').text == '39%'
    assert 'disabled' in app.ui.get('vol-slider').states
    app.open_settings()
    assert app._settings_tx and not rx_builds
    assert set(app._set_widgets) == {'tx-TX', 'tx-source', 'tx-source-button',
                                    'tx-loop', 'tx-loop-button', 'tx-filter', 'tx-filter-button'}
    tx_row = app.ui.w['scr-settings'].children[2]
    assert 'disabled' in tx_row.children[-1].states
    app._trx_state = 'TO_RX'
    app._sync_tx_controls()
    assert 'settings' not in env['_KEEP'] and app._active_gain == 'SQL'
    app._trx_state, app._tx_mode = 'RX', None
    app._sync_tx_controls()
    assert app._active_gain == 'SQL'  # no AF fallback through the transition
    app.open_route_menu()
    assert app._route_widgets['backend'].text == 'BACKEND RX  >'
    assert not app._route_widgets['rt-button0'].states
    app._route_widgets['backend-button'].events['click'](None)
    assert rx_builds == [True] and not app._settings_tx
    app.close_settings()
    app._trx_pending = 'TX'
    app.open_route_menu()
    app.open_settings()
    assert 'route_menu' not in env['_KEEP'] and 'settings' not in env['_KEEP']
    app._trx_pending = None
    app._trx_state, app._tx_mode = 'TX', 'FM'
    app.open_route_menu()
    partial = Widget()
    def fail_build():
        app._set_scr_partial = partial
        raise MemoryError('injected build failure')
    app._build_tx_settings = fail_build
    app._route_widgets['backend-button'].events['click'](None)
    assert partial.deleted and app.ui.w['scr-settings'] is None
    assert not app._modal and 'settings' not in env['_KEEP']
    assert app._tx is owner and app._trx_state == 'TX'
    print('PASS BACKEND: contextual route, FM callbacks, read-only other TX, BACK keeps TX, RX restore, failure cleanup')


def main():
    env = namespace()
    test_controls(env)
    test_flash_deferred(env)
    test_persistence(env)
    test_backend_context(env)
    assert 'keys = self._tx_gain_keys() if self._gain_panel_tx' in SOURCE
    print('Host-only actual Python methods; LVGL layout/touch, device and RF NOT VERIFIED.')


if __name__ == '__main__':
    main()
