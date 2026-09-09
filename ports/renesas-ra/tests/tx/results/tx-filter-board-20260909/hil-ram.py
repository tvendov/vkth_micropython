params={'bw': {'CW': 500, 'AM': 9000, 'USB': 3000, 'LSB': 1800, 'FM': 4000}, 'iqe': 0, 'm': 'AM', 'cal': 161.7, 'genwave': 0, 'again': 2.2, 'atgt': 0.5, 'iqa': 1.0, 'txlevel': 40, 'genfreq': 10000, 'txdev': 2500, 'v': 58, 'beon': 1, 'iqp': 0.0, 's': 100, 'txmic': 100, 'rxauto': 1, 'vfos': [[3500290, 'AM'], [579900, 'AM'], [7597178, 'USB']], 'genlevel': 50, 'act': 1, 'a': 'OFF', 'txdepth': 50, 'rt': [7, 1, 3], 'f': 579900, 'txbw': {'AM': 0, 'USB': 0, 'LSB': 0}}
import sdr_single as s,time,gc
_saved_save=s.save_params
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
    assert a._tx_file_label==name,(name,a._tx_file_label,a._trx_error)
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

assert a._tx_class.GEN_API_VERSION==1
click(a.ui.get('agc-pill'))
pick_source('GEN')
assert a._tx_source_settings=='GEN' and not a._tx_file_on
def choose(text):
    panel=s._KEEP['pick_menu'][0].get_child(0)
    for k in range(1,panel.get_child_count()):
        b=panel.get_child(k)
        if b.get_child(0).get_text()==text:
            click(b)
            return
    raise RuntimeError('Missing choice '+text)
def gen_setting(group,value):
    click(a._set_widgets['tx-loop-button'])
    choose(group)
    choose(value)

assert a._tx_class.AUDIO_FILTER_API_VERSION==1
original_bw=dict(a.p['bw'])
original_vol=a.p['v']
def healthy():
    st=a._tx.status()
    assert a._trx_state=='TX' and st['running'] and st['outputs_enabled'] and st['error']==0,st
    assert st['dsp_deadline_misses']==0 and st['dsp_clips']==0 and st['audio_filter_clips']==0,st
    assert st['dsp_max_cycles']<st['dsp_budget_cycles'],st
    return st
def filter_ready(hz,owner):
    settle()
    time.sleep_ms(80)
    st=healthy()
    assert a._tx is owner and st['audio_cutoff']==hz and st['audio_cutoff_active']==hz,st
    assert not st['audio_filter_pending'] and a.p['txbw'][a._tx_mode]==hz,st
    return st
for name in ('AM','USB','LSB'):
    mode(name)
    owner=a._tx
    first=healthy()
    for k,hz in enumerate(s.TX_AF_CHOICES[name]):
        click(a.ui.get('step-display'))
        click(a.ui.get('btn-mode-filter'))
        assert a._mode_expanded==6
        click(a.ui.get('btn-'+s.MODES[k]))
        st=filter_ready(hz,owner)
        time.sleep_ms(180)
        print('HOME_AF_PASS',name,hz,'DSP_MAX',st['dsp_max_cycles'],'BUDGET',st['dsp_budget_cycles'])
    backend()
    scr=a.ui.get('scr-settings')
    click(a._set_widgets['tx-filter-button'])
    choose('2k' if name=='AM' else '1.8k')
    selected=2000 if name=='AM' else 1800
    filter_ready(selected,owner)
    click(scr.get_child(0).get_child(1))
    spans=[]
    for level in (0,40,100):
        ui_action(lambda:a._apply_audio_gain('TX',level))
        settle()
        time.sleep_ms(200)
        st=healthy()
        peak=1023 if name=='AM' else 2047
        assert st['amplitude']==(peak*level+50)//100,st
        assert st['audio_cutoff_active']==selected and a._tx is owner
        lo=[4095,4095];hi=[0,0]
        for n in range(120):
            z=owner.status()
            for idx,key in enumerate(('i_code','q_code')):
                lo[idx]=min(lo[idx],z[key]);hi[idx]=max(hi[idx],z[key])
            time.sleep_us(173)
        if level==0: assert lo==hi==[2048,2048],(lo,hi)
        else: assert hi[0]-lo[0]>20,(lo,hi)
        if name=='AM':assert lo[1]==hi[1]==2048,(lo,hi)
        spans.append(hi[0]-lo[0])
        print('PWR_PASS',name,level,'I',lo[0],hi[0],'Q',lo[1],hi[1])
    assert abs(spans[1]-spans[2]*.4)<max(10,spans[2]*.1),spans
    ui_action(lambda:a._apply_audio_gain('TX',40))
    settle()
    last=healthy()
    assert last['af_frames']>first['af_frames'] and last['dsp_samples']>first['dsp_samples']
    assert a.ui.get('scope-view').get_text()=='AF GEN'
    assert a.p['bw']==original_bw and a.p['v']==original_vol
    print('GEN_FILTER_MODE_PASS',name,'MAX',last['dsp_max_cycles'],'BUDGET',last['dsp_budget_cycles'],
          'AF',first['af_frames'],last['af_frames'],'FREE',gc.mem_free())
print('TX_AF_PWR_RAM_HIL_PASS; UI events sent programmatically; no physical/RF proof')
