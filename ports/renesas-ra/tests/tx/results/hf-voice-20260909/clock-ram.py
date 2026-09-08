params={'bw': {'CW': 500, 'AM': 9000, 'USB': 3000, 'LSB': 1800, 'FM': 4000}, 'iqe': 0, 'm': 'AM', 'cal': 161.7, 'genwave': 0, 'again': 2.2, 'atgt': 0.5, 'iqa': 1.0, 'txlevel': 40, 'genfreq': 10000, 'a': 'OFF', 'v': 0, 'beon': 1, 'iqp': 0.0, 's': 100, 'txmic': 100, 'rxauto': 1, 'vfos': [[3500290, 'AM'], [3500000, 'AM'], [7597178, 'USB']], 'genlevel': 50, 'act': 1, 'txdepth': 50, 'rt': [7, 1, 3], 'f': 3500000, 'txdev': 2500}
import sdr_single as s,time,gc
s.save_params=lambda p:None

_setfreq=s.SI5351.set_freq
def trace_setfreq(self,clk,hz):
    ok=_setfreq(self,clk,hz)
    got=self.i2c.readfrom_mem(self.addr,42+8*clk,8)
    print('CLOCK_WRITE',clk,hz,ok,repr(got))
    return ok
s.SI5351.set_freq=trace_setfreq

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

badreads=0
def clock_check(hz):
    global badreads
    clk=s.TARGETS[a.p['rt'][a.p['act']]][1]
    ratio=s.PLL/(hz*(1.0-s.XTAL_PPM*1e-6))
    div=int(ratio)
    want=bytes(a._synth._ms_params(div,int((ratio-div)*s._C),s._C))
    for n in range(3):
        got=a._synth.i2c.readfrom_mem(a._synth.addr,42+8*clk,8)
        print('CLOCK_READ',n,a._trx_state,a.p['m'],a.p['f'],hz,clk,repr(got),'OK',got==want)
        if got!=want:
            badreads+=1
            print('CLOCK_MISMATCH','WANT',repr(want),'LO',a._lo_hz,'PENDING',a._trx_pending)
        time.sleep_ms(5)
for lap in range(6):
    name=('AM','USB','LSB')[lap%3]
    src='MIC' if lap%2==0 else 'R:'+name
    mode(name)
    source(src)
    print('CLOCK_STAGE',lap,name,src)
    clock_check(3500000)
    backend()
    back()
    click(a.ui.get('freq-digits'))
    click(a.ui.get('cancel-button'))
    time.sleep_ms(300)
    clock_check(3500000)
    restore_rx()
    click(a.ui.get('rx-button'))
    settle()
    clock_check(3500000)
print('CLOCK_PROBE_BAD_READS',badreads)
assert badreads==0,('Clock reads mismatched',badreads)
