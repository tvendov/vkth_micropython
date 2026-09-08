"""Actual status painters/poller: host LVGL doubles, no device or RF claim."""
import types
import test_sdr_fm_controls as c


def run():
    c.METHODS.update({'paint_status', '_poll_tx_status'})
    env = c.namespace()
    env.update(fmt_count=lambda value: str(value), GRAY2=0x888888)
    app, calls = c.new_app(env)
    app._st = None
    app._ovr_red = app._und_red = app._clip_red = True
    for name in ('sdr-ovr', 'sdr-und', 'sdr-clip'):
        app.ui.get(name).color = 0xe53935
    # Direct RX consumer did not set _st; clearing must still clear its colours.
    app.paint_status(None)
    assert not any((app._ovr_red, app._und_red, app._clip_red))
    assert all(app.ui.get(n).color == env['BORDER']
               for n in ('sdr-ovr', 'sdr-und', 'sdr-clip'))

    status = dict(owned=True, running=True, outputs_enabled=True, error=0,
                  dsp_samples=10, af_frames=1, adc_rails=0, file_underruns=0,
                  dsp_deadline_misses=0, dsp_clips=0)
    app._tx = types.SimpleNamespace(status=lambda: dict(status))
    app._refresh_tx_settings = lambda st: None
    app._trx_state = 'TX'
    for mode in ('AM', 'USB', 'LSB', 'FM', 'CW'):
        app._tx_mode = mode
        app._tx_diag = None
        app._tx_last_samples = None
        app._ovr_red = app._und_red = app._clip_red = True
        assert app._poll_tx_status()
        assert not any((app._ovr_red, app._und_red, app._clip_red)), mode
        assert app.ui.get('sdr-blk').text == 'AF 1'
        for field, flag in (('adc_rails', '_ovr_red'),
                            ('file_underruns', '_und_red'),
                            ('dsp_deadline_misses', '_und_red'),
                            ('dsp_clips', '_clip_red')):
            status[field] += 1
            app._paint_tx_status(status)
            assert getattr(app, flag), (mode, field)
            # Identical next snapshot must clear red, not early-return forever.
            app._paint_tx_status(status)
            assert not getattr(app, flag), (mode, field)
            assert status[field] == 1  # native cumulative counter not cleared
            status[field] = 0
            app._paint_tx_status(status)
            assert not getattr(app, flag)  # owner reset is not a new fault
    print('PASS TX warnings: AM/USB/LSB/FM/CW poll, RX stale colours,')
    print('independent event deltas, identical-snapshot clear, cumulative history retained.')
    print('Host source methods only; no target rendering, analogue or RF proof.')


if __name__ == '__main__':
    run()
