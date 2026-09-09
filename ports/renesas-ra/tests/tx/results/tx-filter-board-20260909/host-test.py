"""RAM-only AF/PWR UI smoke; no test payload or test settings in flash."""
from pathlib import Path
from datetime import datetime
import json
import sys
import time
import deploy_tx_filter_20260909 as dep
from test_tx_generator_board_20260908 import HELPERS, SETUP
from reset_sdr_after_test_20260907 import reset_after_test

WORK = Path(__file__).resolve().parent

TEST = '''
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
'''

PRODUCTION = '''
assert a._tx_class.AUDIO_FILTER_API_VERSION==1
time.sleep_ms(1000)
first=a._tx.status()
time.sleep_ms(1500)
last=a._tx.status()
assert a._trx_state=='TX' and last['running'] and last['outputs_enabled'] and last['error']==0,last
assert last['af_frames']>first['af_frames'] and last['dsp_samples']>first['dsp_samples']
assert last['dsp_deadline_misses']==0 and last['file_underruns']==0 and last['dsp_clips']==0,last
assert a.ui.get('scope-view').get_text()=='AF GEN'
s.save_params=_saved_save
if a.save_timer:a.save_timer.resume()
print('NORMAL_SAVE_RESTORED',s.save_params is _saved_save)
print('PRODUCTION_TX_AF_PWR',a._tx_mode,a.p['f'],a.p['txlevel'],a.p['txbw'],last,'FREE',gc.mem_free())
print('LEFT_RUNNING_TX_AM_GEN_SINE_1KHZ_LEVEL50_PWR40; NO_RESET_AFTER_START')
'''

SOURCES = '''
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
'''


def main():
    run=Path((WORK/'tx-filter-deployment-path.txt').read_text().strip())
    production='--production' in sys.argv
    phase='production' if production else 'sources' if '--sources' in sys.argv else 'hil'
    saved=json.loads((run/'production-before.json').read_text())
    assert saved is not None and saved[1:4]==['TX','AM','GEN'],saved
    params=saved[0]
    params['txbw']=dict(AM=0,USB=0,LSB=0)
    helpers=HELPERS.replace('import sdr_single as s,time,gc',
        'import sdr_single as s,time,gc\n_saved_save=s.save_params\ns.save_params=lambda p:None')
    helpers=helpers.replace("assert a._tx_file_label==name,", "assert a._tx_file_label==(None if name=='MIC' else name),")
    code='params='+repr(params)+'\n'+helpers+SETUP+(PRODUCTION if production else SOURCES if phase=='sources' else TEST)
    compile(code,'<TX filter RAM '+phase+'>','exec')
    (run/(phase+'-ram.py')).write_text(code,encoding='utf-8')
    j=dep.d.m.h.jopen()
    try:
        j.halt()
        binary=(run/'candidate/firmware.bin').read_bytes()
        assert bytes(j.memory_read8(0,len(binary)))==binary
        dep.safe_boot(j,dep.symbols(run/'candidate/firmware.elf')['boardctrl_run_boot_py'])
    finally:j.close()
    time.sleep(1)
    t=None
    passed=False
    try:
        t=dep.a.af_connection()
        result=dep.checked_execute(t,code,timeout=90)
        (run/(phase+'.log')).write_text(result,encoding='utf-8')
        passed=True
    finally:
        if t:dep.d.m.h.serial_close(t)
        if not production or not passed:reset_after_test()


if __name__=='__main__':main()
