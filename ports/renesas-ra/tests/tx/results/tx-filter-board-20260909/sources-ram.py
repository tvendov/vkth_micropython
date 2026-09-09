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
    assert a._tx_file_label==(None if name=='MIC' else name),(name,a._tx_file_label,a._trx_error)
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

for name in ('AM','USB','LSB'):
    mode(name)
    for src in ('MIC','R:'+name):
        click(a.ui.get('agc-pill'))
        pick_source(src)
        time.sleep_ms(1000)
        owner=a._tx
        for cutoff in ((2000,6000) if name=='AM' else (1800,3000)):
            ui_action(lambda:a._request_tx_filter(cutoff))
            settle()
            time.sleep_ms(200)
            st=owner.status()
            assert a._tx is owner and st['audio_cutoff_active']==cutoff and not st['audio_filter_pending'],st
        first=owner.status()
        time.sleep_ms(1800)
        last=owner.status()
        assert last['running'] and last['outputs_enabled'] and last['error']==0,last
        assert last['dsp_deadline_misses']==0 and last['dsp_max_cycles']<last['dsp_budget_cycles'],last
        assert last['af_frames']>first['af_frames'] and last['dsp_samples']>first['dsp_samples']
        assert a.ui.get('scope-view').get_text()=='AF '+src
        assert last['file_underruns']==first['file_underruns'],('STEADY_UND',src,first,last)
        assert last['file_source']==(src!='MIC') and not last['gen_source']
        print('AF_SOURCE_PASS',name,src,'CUTOFF',last['audio_cutoff_active'],
              'MAX',last['dsp_max_cycles'],'BUDGET',last['dsp_budget_cycles'],
              'AF',first['af_frames'],last['af_frames'],'UND_TRANSITION',first['file_underruns'],
              'UND_STEADY',last['file_underruns']-first['file_underruns'],
              'CLIP',last['dsp_clips'],'FILTER_CLIP',last['audio_filter_clips'])
print('TX_AF_MIC_FILE_RAM_HIL_PASS; physical microphone stimulus unknown')
