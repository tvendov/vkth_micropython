"""Known-board HF MIC/FILE tests. RAM only; no firmware, FS or setting writes."""
from pathlib import Path
from datetime import datetime
import argparse
import ast
import json
import time
import test_tx_generator_board_20260908 as old

WORK = Path(__file__).resolve().parent
DEPLOY = Path((WORK / 'gen-smooth-deployment-path.txt').read_text().strip())
POINTER = WORK / 'hf-voice-hil-path.txt'

CHECKS = '''
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
'''

CONTROLS = '''
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
'''

SOAK = '''
begin=time.ticks_ms()
warm=[]
for lap in range(12):
    name=('AM','USB','LSB')[lap%3]
    src='MIC' if lap%2==0 else 'R:'+name
    mode(name)
    source(src)
    owner=a._tx
    clock_check(3500000)
    backend()
    time.sleep_ms(400)
    back()
    click(a.ui.get('freq-digits'))
    assert s.lv.screen_active()==a.ui.get('scr-freq-input')
    click(a.ui.get('cancel-button'))
    time.sleep_ms(1500)
    first=healthy(src)
    time.sleep_ms(10000)
    last=healthy(src)
    assert a._tx is owner and last['af_frames']>first['af_frames']
    delta=last['file_underruns']-first['file_underruns']
    loops=a._iq_file._consumed_total//a._iq_file._total_samples if src!='MIC' else 0
    print('HF_SOAK_TX',lap,name,src,'AF',last['af_frames']-first['af_frames'],
          'UND_TRANSITION',first['file_underruns'],'UND_STEADY',delta,
          'CLIP',last['dsp_clips'],'CLIP_STEADY',last['dsp_clips']-first['dsp_clips'],
          'DEADLINE',last['dsp_deadline_misses'],'LOOPS',loops)
    assert delta==0,('STEADY UND',lap,src,delta)
    if src!='MIC':assert loops>=2
    restore_rx()
    gc.collect()
    free=gc.mem_free()
    warm.append(free)
    print('HF_SOAK_RX',lap,'FREE',free,'BLOCKS',a.be.iq.status()['blocks'])
    click(a.ui.get('rx-button'))
    settle()
print('HF_VOICE_SOAK_PASS','MS',time.ticks_diff(time.ticks_ms(),begin),
      'RX_FREE',min(warm[3:]),max(warm[3:]),'NO_ANALOG_RF_PROOF')
'''

CLOCK_PROBE = '''
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
'''


def capture():
    run = old.d.d.m.ROOT / 'backups' / ('hf-voice-1120000058-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    run.mkdir()
    POINTER.write_text(str(run), encoding='utf-8')
    print('RUN', run, flush=True)
    t = None
    try:
        t = old.d.af_connection()
        out = old.checked_execute(t, "import sdr_single as s\na=s._KEEP.get('app')\nprint('CURRENT',repr((a.p,a._trx_state,a._tx_mode,a._tx_source_settings,a._iq_file_loop) if a else (s.load_params(),'REPL',None,None,True)))\n", timeout=20)
        state = ast.literal_eval(next(line[8:] for line in out.splitlines() if line.startswith('CURRENT ')))
        (run / 'before.json').write_text(json.dumps(state, indent=2), encoding='utf-8')
        (run / 'capture.log').write_text(out, encoding='utf-8')
    finally:
        if t: old.d.d.m.h.serial_close(t)


def test(which, label=None):
    run = Path(POINTER.read_text().strip())
    prefix = label or which
    assert prefix.replace('-', '').replace('_', '').isalnum(), 'Invalid artifact label'
    assert not (run / (prefix + '-ram.py')).exists(), 'Do not overwrite earlier evidence'
    params = json.loads((run / 'before.json').read_text())[0]
    params.update(f=3500000, m='AM', beon=1, rxauto=1, v=0,
                  txlevel=40, txmic=100, txdepth=50, txdev=2500)
    params['vfos'][params['act']] = [3500000, 'AM']
    helpers = old.HELPERS.replace('import sdr_single as s,time,gc',
        'import sdr_single as s,time,gc\ns.save_params=lambda p:None')
    helpers = helpers.replace("assert a._tx_file_label==name,", "assert (a._tx_file_label or 'MIC')==name,")
    if which == 'clock':
        # Diagnostic-only wrapper: records writes/readback, does not retry or repair.
        tracing = '''
_setfreq=s.SI5351.set_freq
def trace_setfreq(self,clk,hz):
    ok=_setfreq(self,clk,hz)
    got=self.i2c.readfrom_mem(self.addr,42+8*clk,8)
    print('CLOCK_WRITE',clk,hz,ok,repr(got))
    return ok
s.SI5351.set_freq=trace_setfreq
'''
        helpers = helpers.replace('_saved_loader=s.load_params', tracing+'\n_saved_loader=s.load_params')
    code = 'params=' + repr(params) + '\n' + helpers + CHECKS + {
        'controls': CONTROLS, 'soak': SOAK, 'clock': CLOCK_PROBE}[which]
    compile(code, '<HF ' + which + ' RAM>', 'exec')
    (run / (prefix + '-ram.py')).write_text(code, encoding='utf-8')
    j = old.d.d.m.h.jopen()
    record = None
    t = None
    partial = []
    execute_original = old.d.d.execute
    def streamed(connection, command, timeout=20):
        if command != 'exec(_r5code)': return execute_original(connection, command, timeout)
        def consume(data):
            chunk = data.decode('utf-8', errors='replace')
            partial.append(chunk)
            print(chunk, end='', flush=True)
        out, err = connection.exec_raw(command, timeout=timeout, data_consumer=consume)
        if err: raise RuntimeError(err.decode('utf-8', errors='replace'))
        return ''.join(partial) if partial else out.decode('utf-8', errors='replace')
    old.d.d.execute = streamed
    try:
        j.halt()
        binary = (DEPLOY / 'candidate/firmware.bin').read_bytes()
        assert bytes(j.memory_read8(0, len(binary))) == binary, 'Installed firmware mismatch'
        record = bytes(j.memory_read8(0x40100000, 512))
        assert record[:4] == b'SDR1'
        (run / (prefix + '-dataflash-before.bin')).write_bytes(record)
        old.safe_boot(j, old.symbols(DEPLOY / 'candidate/firmware.elf')['boardctrl_run_boot_py'])
        j.close()
        j = None
        time.sleep(1)
        t = old.d.af_connection()
        output = old.checked_execute(t, code, timeout=300)
        (run / (prefix + '-hil.log')).write_text(output, encoding='utf-8')
        j = old.d.d.m.h.jopen()
        j.halt()
        after = bytes(j.memory_read8(0x40100000, 512))
        (run / (prefix + '-dataflash-after.bin')).write_bytes(after)
        end = (6 + int.from_bytes(record[4:6], 'little') + 3) & ~3
        assert 8 <= end <= 512 and record[:end] == after[:end], 'Written settings changed'
        print('WRITTEN_SETTINGS_UNCHANGED', end, flush=True)
    except BaseException as exc:
        (run / (prefix + '-error.txt')).write_text(repr(exc), encoding='utf-8')
        raise
    finally:
        old.d.d.execute = execute_original
        (run / (prefix + '-partial.log')).write_text(''.join(partial), encoding='utf-8')
        if j: j.close()
        if t: old.d.d.m.h.serial_close(t)
        old.reset_after_test()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('capture', 'controls', 'soak', 'clock'))
    parser.add_argument('--label', help='New artifact prefix; earlier runs are never overwritten')
    args = parser.parse_args()
    if args.action == 'capture': capture()
    else: test(args.action, args.label)
