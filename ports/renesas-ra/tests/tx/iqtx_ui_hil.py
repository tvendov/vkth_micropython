"""Retain the existing SDR UI; stop RX only during the TX resource test.

Run with run_board_hil.py --run-live, never with --run. PA must be disconnected.
The UI event loop remains enabled. A 100-ms LVGL heartbeat is test instrumentation,
not TX servicing. No display interaction or analog waveform quality is inferred.
"""
import machine,time,json,gc,sdr_single
IQTX=machine.IQTX
lv=sdr_single.lv
app=sdr_single._KEEP['app']
was_running=app.be.running
saved_params=json.loads(json.dumps(app.p))
dac,dac_q=app.be.dac,app.be.dac_q
iq=app.be.iq
heartbeat=[0]
timer=None
results=[]

def emit(event,**data):
    data['event']=event
    print('IQTX_UI '+json.dumps(data))

def beat(t): heartbeat[0]+=1

try:
    app.sdr_timer.pause()
    if app.save_timer: app.save_timer.pause()
    assert not app._inj_on, 'Tester injection is active'
    assert was_running and iq is not None, 'Active RX is required for the EBUSY handoff proof'
    rx_before=iq.status()
    busy_probe=None
    try:
        busy_probe=IQTX(mode=IQTX.CW)
    except OSError as exc:
        assert exc.args and exc.args[0]==16, 'Active RX raised the wrong error: '+repr(exc)
    else:
        try:
            busy_probe.deinit()
        finally:
            raise AssertionError('IQTX acquired resources while RX was active')
    assert app.be.running, 'RX stopped during the EBUSY probe'
    assert app.be.iq is iq, 'EBUSY probe replaced the IQADC owner'
    assert app.be.dac is dac and app.be.dac_q is dac_q, 'EBUSY probe replaced an RX DAC'
    rx_after=iq.status()
    assert rx_before['running'] and rx_after['running'], 'IQADC stopped during EBUSY probe'
    assert rx_after['blocks']>=rx_before['blocks'], 'IQADC block counter went backwards'
    assert rx_after['last_error']==rx_before['last_error']==0, 'IQADC error during EBUSY probe'
    emit('active_rx_busy',errno=16,rx_running=True,owners_unchanged=True,
         blocks_before=rx_before['blocks'],blocks_after=rx_after['blocks'])
    assert app.be.stop_rx(), 'RX resources not released'
    if dac_q is not None: dac_q.deinit()
    if dac is not None: dac.deinit()
    dac=dac_q=None
    gc.collect()
    emit('rx_released_ui_retained',heap=gc.mem_free())
    timer=lv.timer_create(beat,100,None)
    for mode in (IQTX.CW,IQTX.AM,IQTX.FM):
        gc.collect()
        free=gc.mem_free()
        tx=None
        try:
            tx=IQTX(mode=mode)
            prepared=tx.status()
            if mode==IQTX.CW:
                expected=(0,0,None)
                actual=(prepared['lut_bytes'],prepared['lut_allocation_bytes'],None)
            elif mode==IQTX.AM:
                expected=(8192,16383,7)
                actual=(prepared['lut_bytes'],prepared['lut_allocation_bytes'],
                        prepared['transfer_count'])
            else:
                expected=(1024,2047,0)  # voice FM uses bounded C ISR, not DOC
                actual=(prepared['lut_bytes'],prepared['lut_allocation_bytes'],
                        prepared['transfer_count'])
            assert actual==expected, 'Compact LUT contract: '+repr(prepared)
            tx.start()
            if mode==IQTX.CW: tx.key(True)
            before=heartbeat[0]
            sample_before=tx.status().get('dsp_samples',0)
            start=time.ticks_ms()
            polls=0
            while time.ticks_diff(time.ticks_ms(),start)<3000:
                s=tx.status()
                assert s['running'] and s['error']==0
                assert s['unexpected_callbacks']==0
                assert 0<=s['i_code']<=4095 and 0<=s['q_code']<=4095
                polls+=1
                time.sleep_ms(20)
            elapsed=time.ticks_diff(time.ticks_ms(),start)
            beats=heartbeat[0]-before
            assert beats>=10, 'LVGL heartbeat stalled'
            if mode==IQTX.FM:
                assert s['cpu_dsp'] and s['dsp_samples']!=sample_before, 'FM ADC/DSP stalled'
                assert s['dsp_deadline_misses']==0, 'FM C sample over budget'
                assert s['i_enabled'] and s['q_enabled'], 'FM DAC output disabled'
                tx.fm_configure(deviation_hz=4000,mic_gain=150,amplitude=400)
                applied=tx.status()
                assert (applied['deviation_hz'],applied['mic_gain'],applied['amplitude'])==(4000,150,400)
            tx.stop()
            s=tx.status()
            assert s['quiesced'] and s['i_code']==2048 and s['q_code']==2048
            result=dict(mode=mode,result='RUN_PASS',heap_before=free,
                        lut_bytes=expected[0],allocation_bytes=expected[1],
                        transfer_count=prepared['transfer_count'],polls=polls,
                        elapsed_ms=elapsed,ui_heartbeats=beats)
        finally:
            if tx is not None: tx.deinit()
            IQTX.release()
            tx=None
        results.append(result)
        emit('mode',**result)
    # Allocation failures must not strand TX ownership.
    probe=IQTX(mode=IQTX.CW)
    probe.deinit()
    emit('summary',results=results,owner_reacquire=True)
finally:
    IQTX.release()
    if timer is not None: timer.delete()
    if was_running: app.start_rx()
    app.p['rxauto']=saved_params['rxauto']
    # Drop this test's paused debounce only if persisted state already matches.
    # Future user changes can then schedule a normal new timer.
    persisted_match=sdr_single.load_params()==app.p
    if app.save_timer:
        if persisted_match:
            app.save_timer.delete()
            app.save_timer=None
        else:
            app.save_timer.resume()
    app.sdr_timer.resume()
    time.sleep_ms(1200)
    assert app.p==saved_params, 'SDR parameters changed'
    assert app.be.running==was_running, 'RX was not restored'
    assert app.be.err is None, repr(app.be.err)
    emit('restored',running=app.be.running,params_unchanged=app.p==saved_params,persisted_match=persisted_match,heap=gc.mem_free())
