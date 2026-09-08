"""RAM-only GEN level and menu/RX-TX regression, not analogue I/Q proof."""
from pathlib import Path
from datetime import datetime
import argparse
import ast
import json
import time
import test_tx_generator_board_20260908 as old

WORK=Path(__file__).resolve().parent
DEPLOY=Path((WORK/'rx-tone-monitor-deployment-path.txt').read_text().strip())

CHECKS='''
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
'''

LEVELS='''
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
'''

CYCLES='''
begin=time.ticks_ms()
free_low=999999
free_high=0
for lap in range(16):
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
'''

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('suite',choices=('levels','cycles'))
    args=parser.parse_args()
    run=WORK/('gen-'+args.suite+'-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    run.mkdir()
    print('RUN',run,flush=True)
    t=None
    try:
        # Read current state before resetting; do not use a historical UI value.
        t=old.d.af_connection()
        captured=old.checked_execute(t,"import sdr_single as s\na=s._KEEP.get('app')\nprint('CURRENT',repr((a.p,a._trx_state,a._tx_source_settings) if a else (s.load_params(),'REPL',None)))\n",timeout=20)
        state=ast.literal_eval(next(line[8:] for line in captured.splitlines() if line.startswith('CURRENT ')))
        (run/'before.json').write_text(json.dumps(state,indent=2))
    finally:
        if t:old.d.d.m.h.serial_close(t)
    params=state[0]
    params.update(genfreq=10000,genlevel=50,genwave=0,txlevel=40,txmic=100,txdepth=50,txdev=2500)
    helpers=old.HELPERS.replace('import sdr_single as s,time,gc',
                               'import sdr_single as s,time,gc\ns.save_params=lambda p:None')
    code='params='+repr(params)+'\n'+helpers+old.SETUP+CHECKS+(LEVELS if args.suite=='levels' else CYCLES)
    compile(code,'<RAM GEN '+args.suite+'>','exec')
    (run/'ram-suite.py').write_text(code,encoding='utf-8')
    j=old.d.d.m.h.jopen()
    try:
        j.halt()
        binary=(DEPLOY/'candidate/firmware.bin').read_bytes()
        assert bytes(j.memory_read8(0,len(binary)))==binary,'Unexpected firmware; no test run'
        record=bytes(j.memory_read8(0x40100000,512))
        assert record[:4]==b'SDR1',record[:6]
        end=(6+int.from_bytes(record[4:6],'little')+3)&~3
        assert 8<=end<=512
        (run/'dataflash-before.bin').write_bytes(record)
        old.safe_boot(j,old.symbols(DEPLOY/'candidate/firmware.elf')['boardctrl_run_boot_py'])
    finally:j.close()
    time.sleep(1)
    t=None
    try:
        t=old.d.af_connection()
        out=old.checked_execute(t,code,timeout=300)
        (run/'hil.log').write_text(out,encoding='utf-8')
        j=old.d.d.m.h.jopen()
        try:
            # Readback only: no post-suite flash modifications are authorized.
            j.halt()
            after=bytes(j.memory_read8(0x40100000,512))
            (run/'dataflash-after.bin').write_bytes(after)
            print('DATAFLASH_DIFF_OFFSETS',[k for k in range(512) if after[k]!=record[k]],flush=True)
            assert after[:end]==record[:end],'Written settings record changed'
        finally:j.close()
        print('DATAFLASH_WRITTEN_RECORD_UNCHANGED',end,flush=True)
    finally:
        if t:old.d.d.m.h.serial_close(t)
        old.reset_after_test()

if __name__=='__main__':main()
