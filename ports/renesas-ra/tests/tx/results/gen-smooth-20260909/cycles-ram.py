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

begin=time.ticks_ms()
free_low=999999
free_high=0
for lap in range(8):
    name=('AM','USB','LSB','FM')[lap%4]
    mode(name)
    backend()
    gen_setting('FREQUENCY','1000 Hz')
    gen_setting('LEVEL','50%')
    click(a.ui.get('scr-settings').get_child(0).get_child(1))
    first=healthy()
    clock_check(a.p['f'])
    click(a.ui.get('freq-digits'))
    assert s.lv.screen_active()==a.ui.get('scr-freq-input'),a._trx_error
    click(a.ui.get('cancel-button'))
    assert a.ui.w['scr-freq-input'] is None and a.blink_timer is None
    time.sleep_ms(4500)
    st=healthy()
    assert st['af_frames']>first['af_frames'] and st['dsp_samples']>first['dsp_samples']
    assert st['dsp_clips']==0,st
    click(a.ui.get('rx-button'))
    start=time.ticks_ms()
    while (a._trx_pending is not None or a._trx_state!='RX') and time.ticks_diff(time.ticks_ms(),start)<6000:
        time.sleep_ms(20)
    assert a._trx_state=='RX' and a.be.running and a._tx is None,(a._trx_state,a.be.err,a._trx_error)
    assert a.be.iq.tone_monitor()==(0,False,0,0,False)
    clock_check(a._normal_lo_hz(a.p['f'])*4)
    start_blocks=a.be.iq.status()['blocks']
    click(a.ui.get('freq-digits'))
    assert s.lv.screen_active()==a.ui.get('scr-freq-input'),a.be.err
    click(a.ui.get('cancel-button'))
    time.sleep_ms(4500)
    assert a.be.iq.status()['blocks']>start_blocks
    assert a._trx_error is None,a._trx_error
    gc.collect()
    free=gc.mem_free()
    if lap>=4:
        free_low=min(free_low,free);free_high=max(free_high,free)
    print('CYCLE',lap,name,'RX_FREE',free,'RX_BLOCKS',a.be.iq.status()['blocks'],
          'TX_AF',st['af_frames'],'TX_CLIP',st['dsp_clips'],'TX_DEADLINE',st['dsp_deadline_misses'])
    click(a.ui.get('rx-button'))
    settle()
    if a._tx_source_settings!='GEN':
        click(a.ui.get('agc-pill'))
        pick_source('GEN')
print('SOAK_PASS','MS',time.ticks_diff(time.ticks_ms(),begin),'WARM_RX_FREE',free_low,free_high,
      'PHYSICAL_TOUCH_ANALOG_RF_NOT_MEASURED')
