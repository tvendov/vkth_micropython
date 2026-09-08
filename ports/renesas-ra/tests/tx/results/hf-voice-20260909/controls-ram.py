params={'bw': {'CW': 500, 'AM': 9000, 'USB': 3000, 'LSB': 1800, 'FM': 4000}, 'iqe': 0, 'm': 'AM', 'cal': 161.7, 'genwave': 0, 'again': 2.2, 'atgt': 0.5, 'iqa': 1.0, 'txlevel': 40, 'genfreq': 10000, 'a': 'OFF', 'v': 0, 'beon': 1, 'iqp': 0.0, 's': 100, 'txmic': 100, 'rxauto': 1, 'vfos': [[3500290, 'AM'], [3500000, 'AM'], [7597178, 'USB']], 'genlevel': 50, 'act': 1, 'txdepth': 50, 'rt': [7, 1, 3], 'f': 3500000, 'txdev': 2500}
import sdr_single as s,time,gc
s.save_params=lambda p:None
_saved_loader=s.load_params
s.load_params=lambda:params
try:
    _app=s.start()
finally:
    s.load_params=_saved_loader
a=_app
assert a.ui.get('brand-title').get_text()=='SDR TRANSCEIVER'
def ui_action(fn):
    result=[False,None]
    def dispatch(t):
        t.pause()
        try:fn()
        except Exception as e:result[1]=repr(e)
        result[0]=True
    timer=s.lv.timer_create(dispatch,1,None)
    begin=time.ticks_ms()
    while not result[0] and time.ticks_diff(time.ticks_ms(),begin)<3000:
        time.sleep_ms(10)
    timer.delete()
    assert result[0] and result[1] is None,result
def click(widget):
    ui_action(lambda:widget.send_event(s.lv.EVENT.CLICKED,None))
def settle():
    begin=time.ticks_ms()
    while a._trx_pending is not None and time.ticks_diff(time.ticks_ms(),begin)<5000:
        time.sleep_ms(20)
    assert a._trx_pending is None and a._trx_state=='TX',(a._trx_state,a._trx_error)
    assert a._trx_error is None,a._trx_error
def mode(name):
    if a._tx_mode!=name:
        click(a.ui.get('step-display'))
        click(a.ui.get('btn-'+a._tx_mode))
        click(a.ui.get('btn-'+name))
        settle()
    assert a._tx_mode==name
def backend():
    click(a.ui.get('brand-row'))
    click(a._route_widgets['backend-button'])
    assert s.lv.screen_active()==a.ui.get('scr-settings')
def pick_source(name):
    panel=s._KEEP['pick_menu'][0].get_child(0)
    selected=None
    for k in range(1,panel.get_child_count()):
        b=panel.get_child(k)
        if b.get_child(0).get_text()==name:selected=b
    assert selected is not None,name
    click(selected)
    settle()
    assert (a._tx_file_label or 'MIC')==name,(name,a._tx_file_label,a._trx_error)
for k in range(120):
    if a._tx_guard_error() is None:break
    time.sleep_ms(50)
assert a._tx_guard_error() is None,(a._tx_guard_error(),a.be.err)
if a.save_timer:a.save_timer.pause()
click(a.ui.get('rx-button'))
settle()
assert a.ui.get('agc-title').get_text()=='SOURCE'
assert a.ui.get('brand-title').get_text()=='SDR TRANSCEIVER'
rf=a.p['f']

def healthy(source):
    assert a._trx_state=='TX' and a._trx_pending is None,(a._trx_state,a._trx_error)
    st=a._tx.status()
    assert st['running'] and st['outputs_enabled'] and st['error']==0,st
    assert not st['gen_source'] and st['file_source']==(source!='MIC'),st
    assert (st['mic_pin'] is not None)==(source=='MIC'),st
    assert st['unexpected_callbacks']==0 and st['dsp_deadline_misses']==0,st
    return st
def source(name):
    if (a._tx_file_label or 'MIC')!=name:
        click(a.ui.get('agc-pill'))
        pick_source(name)
    time.sleep_ms(1200)
    healthy(name)
    assert a.ui.get('scope-view').get_text()=='AF '+name
def clock_check(hz):
    clk=s.TARGETS[a.p['rt'][a.p['act']]][1]
    ratio=s.PLL/(hz*(1.0-s.XTAL_PPM*1e-6))
    div=int(ratio)
    expected=bytes(a._synth._ms_params(div,int((ratio-div)*s._C),s._C))
    got=a._synth.i2c.readfrom_mem(a._synth.addr,42+8*clk,8)
    assert expected==got,('clock',hz,expected,got)
def gain(key,value):
    ui_action(lambda:a._apply_gain(key,value))
    settle()
def back():
    click(a.ui.get('scr-settings').get_child(0).get_child(1))
    assert s.lv.screen_active()==a.ui.get('scr-receiver') and not a._modal
def restore_rx():
    click(a.ui.get('rx-button'))
    begin=time.ticks_ms()
    while a._trx_state!='RX' or a._trx_pending is not None or a._tx_guard_error() is not None:
        assert time.ticks_diff(time.ticks_ms(),begin)<8000,(a._trx_state,a._trx_error,a.be.err)
        time.sleep_ms(25)
    assert a.be.running and a._tx is None and not a._tx_file_on
    assert not a._iq_file._attached
    clock_check(a._normal_lo_hz(a.p['f'])*4)
    n=a.be.iq.status()['blocks']
    time.sleep_ms(400)
    assert a.be.iq.status()['blocks']>n

for name in ('AM','USB','LSB'):
    mode(name)
    for src in ('MIC','R:'+name):
        source(src)
        owner=a._tx
        st=healthy(src)
        clock_check(3500000)
        lo=[4095,4095,4095];hi=[0,0,0]
        for k in range(48):
            sample=owner.status()
            for idx,key in enumerate(('last_adc','i_code','q_code')):
                v=sample[key];lo[idx]=min(lo[idx],v);hi[idx]=max(hi[idx],v)
            time.sleep_ms(7)
        print('HF_INPUT',name,src,'PIN',st['mic_pin'],'RAW',lo[0],hi[0],
              'I',lo[1],hi[1],'Q',lo[2],hi[2],'START_CLIP',st['dsp_clips'])
        if name=='AM':assert lo[2]==hi[2]==2048
        if src!='MIC':assert hi[0]>lo[0],('FILE AF static',src,lo,hi)
        del sample
        backend()
        assert a._settings_tx and 'tx-TX' in a._set_widgets
        assert ('tx-DEPTH' in a._set_widgets)==(name=='AM')
        if src!='MIC':
            assert a._iq_file_loop and a._iq_file._loop
            click(a._set_widgets['tx-loop-button'])
            assert not a._iq_file_loop
            click(a._set_widgets['tx-loop-button'])
            assert a._iq_file_loop and a._tx is owner
        back()
        for level in (0,20,40):
            gain('TX',level)
            time.sleep_ms(350)
            out=healthy(src)
            peak=1023 if name=='AM' else 2047
            assert out['amplitude']==(peak*level+50)//100 and a._tx is owner,out
            if level==0:assert out['i_code']==out['q_code']==2048,out
        gain('MIC',8)
        time.sleep_ms(250)
        assert healthy(src)['audio_gain']==80
        gain('MIC',10)
        if name=='AM':
            gain('DEPTH',75)
            time.sleep_ms(250)
            assert healthy(src)['am_depth']==75
            gain('DEPTH',50)
        # OFF disables capture/drawing, not the source or DAC modulation.
        ui_action(a.toggle_scope_view)
        assert a._scope_view==2
        time.sleep_ms(250)
        off=healthy(src)
        time.sleep_ms(500)
        off2=healthy(src)
        assert off2['dsp_samples']>off['dsp_samples'] and off2['af_frames']==off['af_frames']
        ui_action(a.toggle_scope_view)
        assert a._scope_view==0
        time.sleep_ms(1200)
        first=healthy(src)
        time.sleep_ms(5000)
        last=healthy(src)
        assert a._tx is owner and last['af_frames']>first['af_frames']
        assert last['dsp_samples']>first['dsp_samples']
        delta=last['file_underruns']-first['file_underruns']
        print('HF_CONTROL',name,src,'AF',last['af_frames']-first['af_frames'],
              'SAMPLES',last['dsp_samples']-first['dsp_samples'],
              'UND_TRANSITION',first['file_underruns'],'UND_STEADY',delta,
              'CLIP_TOTAL',last['dsp_clips'],'CLIP_STEADY',last['dsp_clips']-first['dsp_clips'],
              'MAX',last['dsp_max_cycles'],'BUDGET',last['dsp_budget_cycles'],'FREE',gc.mem_free())
        assert delta==0,('STEADY FILE UND',name,src,delta)
        # Physical MIC stimulus is unknown: report its clips, do not invent a clean tone.
        if src!='MIC':assert last['dsp_clips']==first['dsp_clips']
    restore_rx()
    gc.collect()
    print('HF_RX_RESTORED',name,'FREE',gc.mem_free())
    click(a.ui.get('rx-button'))
    settle()
print('HF_MIC_FILE_CONTROLS_PASS; no physical touch, analog I/Q or RF measurement')
