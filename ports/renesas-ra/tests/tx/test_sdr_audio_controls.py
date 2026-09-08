"""Execute actual AM/SSB UI methods; native controller/target are mocked."""
import types
import test_sdr_fm_controls as controls


def run():
    env = controls.namespace()
    for mode, keys, peak in (('AM', {'tx-MIC', 'tx-DEPTH', 'tx-TX'}, 1023),
                             ('USB', {'tx-MIC', 'tx-TX'}, 2047),
                             ('LSB', {'tx-MIC', 'tx-TX'}, 2047)):
        app, calls = controls.new_app(env)
        app._tx.audio_configure = lambda **kw: calls.append(kw)
        app._trx_state, app._tx_mode = 'TX', mode
        app._tx_file_label = 'R:USB'
        owner = app._tx
        app._sync_tx_controls()
        assert 'disabled' not in app.ui.get('vol-slider').states
        assert app.ui.get('vol-value').text == '40%'
        app.open_settings()
        assert set(app._set_widgets) == keys
        assert not app._gain_available('AF') and not app._apply_gain('AF', 100)
        before = dict(app.p)
        assert app._apply_gain('TX', 0) and app._apply_gain('MIC', 20)
        if mode == 'AM':
            # Execute the actual BACKEND + callback for depth.
            row = app.ui.w['scr-settings'].children[3]
            row.children[-1].events['short'](None)
        assert not calls and app.p == before
        assert app._trx_pending == 'TX_AUDIO' and app._trx_state == 'TX'
        app._trx_pending = None  # worker consumes only the request
        assert app._service_tx_audio()
        assert calls[-1] == dict(audio_gain=200, am_depth=51 if mode == 'AM' else 50, amplitude=0)
        assert app._tx is owner and app._tx_file_label == 'R:USB'
        assert app._tx_save_pending and app.save_timer is None
        assert app._apply_gain('TX', 100)
        app._trx_pending = None
        assert app._service_tx_audio() and calls[-1]['amplitude'] == peak
        # No pending mutation may cross an ownership/mode transaction.
        app._trx_pending = 'TX_CONFIG'
        assert not app._apply_gain('MIC', 100)
        app._trx_pending = None
        before = dict(app.p)
        def failure(**kw): raise OSError('checked stop failed')
        app._tx.audio_configure = failure
        app._queue_rx_cleanup = lambda error: setattr(app, '_cleanup_error', error)
        assert app._apply_gain('TX', 25)
        app._trx_pending = None
        assert not app._service_tx_audio()
        assert app.p == before and 'checked stop failed' in app._cleanup_error
        app.close_settings()
    # An AM-only selected slider must not survive as a dead control in SSB.
    app, calls = controls.new_app(env)
    app._tx.audio_configure = lambda **kw: calls.append(kw)
    app._trx_state, app._tx_mode = 'TX', 'AM'
    app._sync_tx_controls()
    app._active_gain = app._gain_candidate = 'DEPTH'
    app.open_settings()
    panel = app.ui.w['scr-settings']
    app._tx_mode = 'USB'
    app._sync_tx_controls()
    assert panel.deleted and app._active_gain == 'TX'
    print('PASS AM/USB/LSB HOME and BACKEND: actual callbacks, queued/coalesced native controls, '
          '0/100% level, preserved FILE owner, deferred save, failure rollback and context change')
    print('Host only. Analog amplitude, board timing and LVGL touch/layout NOT VERIFIED.')


if __name__ == '__main__':
    run()
