"""Restore this iteration's captured production state, without installing tests."""
from pathlib import Path
import json
import time
import hf_voice_hil_20260909 as h


def main():
    run = Path(h.POINTER.read_text().strip())
    state = json.loads((run / 'before.json').read_text())
    params, trx, mode, source, loop = state
    assert (trx, mode, source, loop) == ('TX', 'AM', 'GEN', True), state[1:]
    assert params['f'] == 579900 and params['txlevel'] == 40
    helpers = h.old.HELPERS.replace('import sdr_single as s,time,gc',
        'import sdr_single as s,time,gc\n_saved_save=s.save_params\ns.save_params=lambda p:None')
    verify = '''
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
'''
    finish = '''
assert a._trx_state=='TX' and a._tx_mode=='AM' and a._iq_file_loop
assert a.p==captured,('Captured parameters not restored',a.p,captured)
assert after['outputs_enabled'] and after['gen_source'] and after['mic_pin'] is None
assert after['gen_frequency_dhz']==params['genfreq'] and after['gen_level']==params['genlevel']
assert after['dsp_clips']==after['dsp_deadline_misses']==after['file_underruns']==0
clk=s.TARGETS[a.p['rt'][a.p['act']]][1]
ratio=s.PLL/(a.p['f']*(1.0-s.XTAL_PPM*1e-6))
div=int(ratio)
want=bytes(a._synth._ms_params(div,int((ratio-div)*s._C),s._C))
got=a._synth.i2c.readfrom_mem(a._synth.addr,42+8*clk,8)
assert got==want,('Production Si5351 readback',got,want)
print('PRODUCTION_CLOCK_READBACK',clk,a.p['f'],repr(got))
s.save_params=_saved_save
if a.save_timer:a.save_timer.resume()
print('NORMAL_SAVE_RESTORED',s.save_params is _saved_save)
print('RESTORED_CAPTURED_TX',a._tx_mode,a.p['f'],a._tx_source_settings,
      'GEN_DHZ',a.p['genfreq'],'GEN_LEVEL',a.p['genlevel'],
      'TX_LEVEL',a.p['txlevel'],'DEPTH',a.p['txdepth'])
'''
    code = verify + '\ncaptured=' + repr(params) + '\nparams=' + repr(params) + '\n' + helpers + h.old.SETUP + h.old.PRODUCTION + finish
    compile(code, '<HF production restore>', 'exec')
    target = run / 'production-ram.py'
    assert not target.exists(), 'Preserve earlier production attempt'
    target.write_text(code, encoding='utf-8')
    t = None
    passed = False
    try:
        j = h.old.d.d.m.h.jopen()
        try:
            j.halt()
            binary = (h.DEPLOY / 'candidate/firmware.bin').read_bytes()
            assert bytes(j.memory_read8(0, len(binary))) == binary
            print('INSTALLED_FIRMWARE_UNCHANGED', len(binary), flush=True)
            h.old.safe_boot(j, h.old.symbols(h.DEPLOY / 'candidate/firmware.elf')['boardctrl_run_boot_py'])
        finally:
            j.close()
        time.sleep(1)
        t = h.old.d.af_connection()
        output = h.old.checked_execute(t, code, timeout=90)
        (run / 'production-hil.log').write_text(output, encoding='utf-8')
        passed = True
    except BaseException as exc:
        (run / 'production-error.txt').write_text(repr(exc), encoding='utf-8')
        raise
    finally:
        if t: h.old.d.d.m.h.serial_close(t)
        if not passed: h.old.reset_after_test()


if __name__ == '__main__':
    main()
