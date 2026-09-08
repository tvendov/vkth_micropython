"""Run actual app GEN methods against host doubles, never the board."""
import sys
import types
import test_sdr_tx_switch as sw
import test_sdr_fm_controls as ui


def owner():
    previous = sys.modules.get('machine')
    try:
        for mode in ('AM', 'USB', 'LSB', 'FM'):
            app, log = sw.new_app(mode)
            class Tx:
                AM, FM, USB, LSB = 1, 2, 3, 4
                GEN_API_VERSION = 1
                def __init__(self, **kw):
                    assert 'file_mode' not in kw and 'file_tune' not in kw
                    assert kw['gen_frequency_dhz'] == 10000
                    assert kw['gen_level'] == 50 and kw['gen_wave'] == 0
                    log.append('construct_gen')
                    self.running = False
                def status(self):
                    return dict(owned=True, running=self.running, quiesced=not self.running,
                                outputs_enabled=True, error=0, keyed=False, gen_source=True)
                def start(self):
                    self.running = True
                    log.append('start_gen')
                def stop(self): self.running = False
                def deinit(self): pass
                @staticmethod
                def release(): pass
            sys.modules['machine'] = types.SimpleNamespace(IQTX=Tx)
            app._trx_state = 'TO_TX'
            # No file.start/refill methods exist: any accidental access fails.
            assert app._start_tx_owner(mode, app.p['f'], 'GEN')
            assert app._trx_state == 'TX' and app._tx_mode == mode
            assert app._tx_source_settings == 'GEN' and app._tx_file_label == 'GEN'
            assert not app._tx_file_on and app.ui.get('scope-view').text == 'AF GEN'
            app._service_tx_file()
            assert log.index(('clock', 1, 3500000)) < log.index('construct_gen')
            assert log.index('frame_before_start') < log.index('start_gen')
        print('PASS GEN owner: AM/USB/LSB/FM; x1 synth before start; no FILE fallback; AF GEN')
    finally:
        if previous is None: sys.modules.pop('machine', None)
        else: sys.modules['machine'] = previous


def controls():
    ui.METHODS.update({'open_tx_source_menu', 'open_tx_gen_menu'})
    env = ui.namespace()
    app, calls = ui.new_app(env)
    app._tx_class = types.SimpleNamespace(GEN_API_VERSION=1)
    app._trx_state, app._tx_mode = 'TX', 'USB'
    app._iq_file_profiles = ()
    choices = []
    app.open_pick_menu = lambda *args: choices.append(args)
    requests = []
    app._request_tx_config = lambda **kw: requests.append(kw)
    app.open_tx_source_menu()
    assert choices[-1][1] == ((None, 'MIC'), ('GEN', 'GEN'))
    choices[-1][3]('GEN')
    assert requests == [{'source': 'GEN'}]
    app._tx_source_settings = 'GEN'
    app._tx.gen_configure = lambda **kw: calls.append(kw)
    app.touch_params = lambda: calls.append('save_deferred')
    app._refresh_tx_settings = lambda: None
    for field, value, key in [('genfreq', 17500, 'frequency_dhz'), ('genlevel', 0, 'level'), ('genwave', 2, 'wave')]:
        app.open_tx_gen_menu(field)
        assert len(choices[-1][1]) <= 5
        choices[-1][3](value)
        assert app.p[field] == value and calls[-2:] == [{key: value}, 'save_deferred']
    def reject(**kw): raise ValueError('rejected')
    app._tx.gen_configure = reject
    app.update_rx = lambda: None
    app.open_tx_gen_menu('genlevel')
    choices[-1][3](100)
    assert app.p['genlevel'] == 0 and 'rejected' in app._trx_error
    before = len(choices)
    app._trx_pending = 'TX_CONFIG'
    app.open_tx_gen_menu()
    assert len(choices) == before
    print('PASS GEN UI: source, frequency/level/wave, bounded picker, native failure rollback, pending guard')


if __name__ == '__main__':
    owner()
    controls()
