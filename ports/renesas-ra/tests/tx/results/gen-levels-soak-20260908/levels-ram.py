params={'cal': 161.7, 'v': 58, 'again': 2.2, 'iqp': 0.0, 'act': 1, 'bw': {'LSB': 1800, 'CW': 500, 'FM': 4000, 'USB': 3000, 'AM': 9000}, 'txdev': 2500, 'beon': 1, 's': 100, 'genwave': 0, 'atgt': 0.5, 'genfreq': 10000, 'rt': [7, 1, 3], 'vfos': [[3500290, 'AM'], [579900, 'AM'], [7597178, 'USB']], 'f': 579900, 'a': 'OFF', 'rxauto': 1, 'iqe': 0, 'iqa': 1.0, 'm': 'AM', 'txlevel': 40, 'txmic': 100, 'txdepth': 50, 'genlevel': 50}
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

def healthy():
    assert a._trx_state=='TX' and a._trx_pending is None,(a._trx_state,a._trx_error)
    st=a._tx.status()
    assert st['running'] and st['outputs_enabled'] and st['error']==0,st
    assert st['gen_source'] and st['mic_pin'] is None and not st['file_source']
    assert st['dsp_deadline_misses']==0 and st['file_underruns']==0,st
    return st
def clock_check(hz):
    clk=s.TARGETS[a.p['rt'][a.p['act']]][1]
    ratio=s.PLL/(hz*(1.0-s.XTAL_PPM*1e-6))
    div=int(ratio)
    expected=bytes(a._synth._ms_params(div,int((ratio-div)*s._C),s._C))
    got=a._synth.i2c.readfrom_mem(a._synth.addr,42+8*clk,8)
    assert expected==got,('clock',hz,expected,got)

for name in ('AM','USB','LSB','FM'):
    mode(name)
    time.sleep_ms(300)
    print('MODE_START_CLIP',name,healthy()['dsp_clips'])
    clock_check(a.p['f'])
    spans=[]
    for level in (0,25,50,75,100):
        previous_clips=healthy()['dsp_clips']
        backend()
        gen_setting('LEVEL',str(level)+'%')
        click(a.ui.get('scr-settings').get_child(0).get_child(1))
        time.sleep_ms(1000)
        first=healthy()
        lo=[4095,4095,4095];hi=[0,0,0]
        for k in range(192):
            st=a._tx.status()
            # Independent per-register extrema, NOT synchronous I/Q vectors.
            values=(st['last_adc'],st['i_code'],st['q_code'])
            for n in range(3):
                lo[n]=min(lo[n],values[n]);hi[n]=max(hi[n],values[n])
            time.sleep_us(137)
        last=healthy()
        steady_clips=last['dsp_clips']-first['dsp_clips']
        assert steady_clips==0,('Steady clipping',name,level,steady_clips)
        assert last['af_frames']>first['af_frames'] and last['dsp_samples']>first['dsp_samples']
        raw=hi[0]-lo[0]
        expected=2*(2047*level//100)
        assert abs(raw-expected)<=max(8,expected//15),('GEN AF span',name,level,raw,expected)
        assert 0<lo[1]<=hi[1]<4095 and 0<lo[2]<=hi[2]<4095,(lo,hi)
        if name=='AM':assert lo[2]==hi[2]==2048,(lo,hi)
        spans.append(hi[1]-lo[1])
        print('LEVEL',name,level,'AF',lo[0],hi[0],'I',lo[1],hi[1],'Q',lo[2],hi[2],
              'CLIP',last['dsp_clips'],'ADC_RAILS',last['adc_rails'],'AF_FRAMES',last['af_frames'],
              'TRANSITION_CLIP',first['dsp_clips']-previous_clips,'STEADY_CLIP',steady_clips,
              'DSP_MAX',last['dsp_max_cycles'],'BUDGET',last['dsp_budget_cycles'])
    if name!='FM':
        full=spans[-1]
        for k in range(5):
            assert abs(spans[k]-full*k/4)<=max(8,full//12),('DAC digital span',name,spans)
print('GEN_LEVELS_PASS; FM changes deviation, not proportional I/Q envelope; no physical voltage/phase measurement')
