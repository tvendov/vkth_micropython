"""Actual RX monitor UI methods on host doubles; source contracts are not HIL."""
import ast
from pathlib import Path
from types import SimpleNamespace
from test_sdr_tx_switch import TREE


class Label:
    def __init__(self): self.text, self.writes = '', 0
    def get_text(self): return self.text
    def set_text(self, value): self.text, self.writes = value, self.writes + 1
    def set_style_text_color(self, *args): pass


cls = next(n for n in TREE.body if isinstance(n, ast.ClassDef) and n.name == 'SdrApp')
names = {'_paint_tone_monitor', 'open_tone_monitor_menu'}
methods = [n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name in names]
assert {n.name for n in methods} == names
env = dict(lv=SimpleNamespace(color_hex=lambda c: c), GREEN=1, GRAY2=2)
exec(compile(ast.fix_missing_locations(ast.Module(body=[ast.ClassDef(
    name='App', bases=[], keywords=[], body=methods, decorator_list=[])],
    type_ignores=[])), '<actual UI methods>', 'exec'), env)
app = env['App']()
app._settings_tx = False
app._set_widgets = {'tone_monitor': (Label(), Label())}
app.be = SimpleNamespace(iq=None, err=None)
app._paint_tone_monitor()
assert app._set_widgets['tone_monitor'][0].text == 'N/A'


class IQ:
    def __init__(self): self.st, self.fail = (0, False, 0, 0, False), False
    def tone_monitor(self, value=None):
        if value is not None:
            if self.fail: raise ValueError('rejected')
            self.st = value, False, 0, 0, bool(value)
        return self.st


app.be.iq = IQ()
menus = []
app.open_pick_menu = lambda *args: menus.append(args)
app.open_tone_monitor_menu()
assert menus[-1][1] == ((0, 'OFF'), (1000, '100 Hz'), (7000, '700 Hz'),
                       (10000, '1000 Hz'), (17500, '1750 Hz'))
menus[-1][3](10000)
fb, sb = app._set_widgets['tone_monitor']
assert (fb.text, sb.text) == ('1000.0 Hz', 'WAIT')
app.be.iq.st = 10000, True, 990, 2, True
app._paint_tone_monitor()
assert sb.text == 'TONE'
writes = fb.writes + sb.writes
app.be.iq.st = 10000, True, 910, 10, True
app._paint_tone_monitor()
assert fb.writes + sb.writes == writes  # no purity/window redraw storm
app.be.iq.fail = True
menus[-1][3](17500)
assert fb.text == '1000.0 Hz' and 'rejected' in app.be.err
app.be.iq.st = 10000, False, 0, 0, False
app._paint_tone_monitor()
assert sb.text == 'PATH/STOP'
app._settings_tx = True
app.open_tone_monitor_menu()
assert len(menus) == 1
app.be.iq = None
app._paint_tone_monitor()
assert sb.text == 'N/A'

root = Path(__file__).resolve().parents[2]
source = (root / 'ra/ra_iq_adc.c').read_text()
stage = source[source.index('static inline uint16_t ra_iq_audio_stage'):]
assert stage.index('ra_tone_detect') < stage.index('uint8_t scope') < stage.index('ra_iq_audio_filter(audio)')
assert 's_tone_frequency && s_status.running' in source
assert 'mode != RA_IQ_DEMOD_OFF && mode != RA_IQ_DEMOD_PASS' in source
assert 'frequency_dhz, 250U, 8U' in source
assert 's_tone_frequency = s_tone_reset_pending = s_tone_run_block = 0U;' in source
print('PASS actual monitor UI: OFF/select/readback/TONE/WAIT/PATH/missing API/TX guard; change-only paint')
print('PASS source contracts: pre-AF passive hook; RX-only; new-owner OFF; not hardware proof')
