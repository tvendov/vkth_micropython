params={'bw': {'CW': 500, 'AM': 9000, 'USB': 3000, 'LSB': 1800, 'FM': 4000}, 'iqe': 0, 'm': 'AM', 'cal': 161.7, 'genwave': 0, 'again': 2.2, 'atgt': 0.5, 'iqa': 1.0, 'txlevel': 40, 'genfreq': 10000, 'txdepth': 50, 'v': 0, 'beon': 1, 'iqp': 0.0, 's': 100, 'txmic': 100, 'rxauto': 1, 'vfos': [[3500290, 'AM'], [579900, 'AM'], [7597178, 'USB']], 'genlevel': 50, 'act': 1, 'rt': [7, 1, 3], 'f': 579900, 'txdev': 2500, 'a': 'OFF'}
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
assert a.be.running and a.be.iq is not None,(a.be.err,a._trx_state)
iq=a.be.iq
assert iq.tone_monitor()==(0,False,0,0,False),iq.tone_monitor()
if a.save_timer:a.save_timer.pause()
print('MON_DEFAULT_OFF',iq.tone_monitor(), 'FREE',gc.mem_free())

def signal(name,hz=1000,ampl=400):
    kind={'am':1,'usb':2,'lsb':3,'cw':4,'fm':5}[name]
    iq.demod(name)
    # Existing SSB TESTER emits signed complex AF directly (no separate carrier).
    # CW uses a constant carrier: its receiver BFO creates the 700-Hz audio.
    ssb=name in ('usb','lsb')
    iq.tune(0 if ssb else 3000)
    iq.bandwidth(6000)
    iq.inject(1,hz if ssb else 3000,ampl,kind,
              hz if name in ('am','fm') else 0,50,0,0,0,0,1000 if name=='fm' else 0)

signal('am')
time.sleep_ms(1200)
base=iq.timing()
iq.tone_monitor(10000)
time.sleep_ms(1200)
on=iq.timing()
print('MON_LOAD_OFF_ON',base,on)
assert iq.tone_monitor()[1],iq.tone_monitor()
for value in (-1,1,499,30001):
    try:
        iq.tone_monitor(value)
        raise AssertionError('invalid monitor input accepted')
    except ValueError:pass
    assert iq.tone_monitor()[0]==10000

for name in ('am','usb','lsb','fm','cw'):
    hz=700 if name=='cw' else 1000
    signal(name)
    iq.tone_monitor(hz*10)
    time.sleep_ms(1200)
    st=iq.tone_monitor()
    print('MON_MODE',name,st,iq.timing(),iq.audio_status())
    assert st[1] and st[4] and st[2]>=700,(name,st)
    iq.volume(0)
    iq.audio_filter('cw')  # deliberate wrong voice-band filter is after monitor
    iq.squelch(2000)
    time.sleep_ms(800)
    assert iq.tone_monitor()[1],('monitor depends on output controls',name,iq.tone_monitor())
    iq.block(6,0)
    time.sleep_ms(30)
    assert not iq.tone_monitor()[1] and not iq.tone_monitor()[4]
    iq.block(6,1)
    time.sleep_ms(1000)
    assert iq.tone_monitor()[1],('DEM resume',name,iq.tone_monitor())
    iq.block(2,0)
    time.sleep_ms(800)
    assert iq.tone_monitor()[1],('DEC Fs/2 bypass',name,iq.tone_monitor())
    iq.block(2,1)

# Restore AM1000, then wrong frequency, silence, and a new injection boundary.
signal('am')
iq.squelch(0)
iq.tone_monitor(10000)
time.sleep_ms(1000)
assert iq.tone_monitor()[1]
iq.inject(1,3000,400,1,1500,50,0,0,0,0,0)
time.sleep_ms(1000)
assert not iq.tone_monitor()[1],('wrong tone',iq.tone_monitor())
print('MON_WRONG_TONE',iq.tone_monitor())
signal('am',1000,0)
time.sleep_ms(1000)
assert not iq.tone_monitor()[1],('silence',iq.tone_monitor())
signal('fm',100,400)
iq.tone_monitor(1000)
time.sleep_ms(1100)
assert iq.tone_monitor()[1],('100Hz standalone FM',iq.tone_monitor())
print('MON_FM_100HZ',iq.tone_monitor())
iq.inject(1,0,400,5,100,50,0,0,2,0,1000)
time.sleep_ms(50)
assert not iq.tone_monitor()[1] and iq.tone_monitor()[3]==0,('new OUT boundary',iq.tone_monitor())
time.sleep_ms(1100)
assert iq.tone_monitor()[1]
iq.tone_monitor(0)
before=iq.status()['blocks']
time.sleep_ms(400)
assert iq.status()['blocks']>before and iq.tone_monitor()==(0,False,0,0,False)
iq.stop()
assert not iq.tone_monitor()[4]
iq.demod('am')
iq.tone_monitor(10000)
assert not iq.tone_monitor()[1] and not iq.tone_monitor()[4]
iq.start()
time.sleep_ms(50)
assert not iq.tone_monitor()[1]
iq.stop()
print('RX_TONE_MONITOR_BOARD_PASS; native injection only; no analog/RF/CTCSS-speech certification')
