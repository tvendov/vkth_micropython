
import hashlib,binascii,gc
for path,wanted in (
    ('/flash/sdr_single.py.source','f6bee007fede2dcd3d7e5673005791a8f7103eb6a228fc19645fac5343142467'),
    ('/flash/sdr_single.mpy','d6cc332a76cf4377f38e123e590563a2efa5ea56c87a115aac67bacd25e45b98')):
    digest=hashlib.sha256()
    with open(path,'rb') as f:
        while True:
            chunk=f.read(1024)
            if not chunk:break
            digest.update(chunk)
    got=binascii.hexlify(digest.digest()).decode()
    assert got==wanted,(path,got)
    print('INSTALLED_FILE_UNCHANGED',path,got)
del digest,chunk,path,wanted,got
gc.collect()

params={'cal': 161.7, 'v': 58, 'again': 2.2, 'iqp': 0.0, 'act': 1, 'bw': {'LSB': 1800, 'CW': 500, 'FM': 4000, 'USB': 3000, 'AM': 9000}, 'txdev': 2500, 'beon': 1, 's': 100, 'genwave': 0, 'atgt': 0.5, 'genfreq': 10000, 'rt': [7, 1, 3], 'vfos': [[3500290, 'AM'], [579900, 'AM'], [7597178, 'USB']], 'f': 579900, 'a': 'OFF', 'rxauto': 1, 'iqe': 0, 'iqa': 1.0, 'm': 'AM', 'txlevel': 40, 'txmic': 100, 'txdepth': 50, 'genlevel': 50}
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

time.sleep_ms(1000)
before=a._tx.status()
time.sleep_ms(1000)
after=a._tx.status()
assert after['gen_source'] and after['error']==0 and after['running']
assert after['af_frames']>before['af_frames'] and a.ui.get('scope-view').get_text()=='AF GEN'
print('PRODUCTION_GEN',a._tx_mode,a.p['f'],after)

assert after['outputs_enabled'] and after['dsp_clips']==0
assert after['dsp_deadline_misses']==0 and after['file_underruns']==0
assert a.p['txlevel']==40 and a.p['txdepth']==50
s.save_params=_saved_save
if a.save_timer:a.save_timer.resume()
print('NORMAL_SAVE_RESTORED',s.save_params is _saved_save)
print('LEFT_RUNNING_TX_AM_579900_GEN_SINE_1000HZ_50_PERCENT_TX_LEVEL_40')
