"""Start the installed production app after the RAM-only GEN regression."""
from pathlib import Path
from datetime import datetime
import json
import time
import test_tx_generator_board_20260908 as old

WORK = Path(__file__).resolve().parent
DEPLOY = Path((WORK / 'gen-smooth-deployment-path.txt').read_text().strip())


def main():
    run = WORK / ('gen-smooth-production-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    run.mkdir()
    print('RUN', run, flush=True)
    # This iteration's actual state capture, not an older deployment's UI state.
    params = json.loads((DEPLOY / 'production-before.json').read_text())[0]
    assert params['f'] == 579900 and params['m'] == 'AM'
    params.update(genfreq=10000, genlevel=50, genwave=0)
    helpers = old.HELPERS.replace('import sdr_single as s,time,gc',
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
assert after['outputs_enabled'] and after['dsp_clips']==0
assert after['dsp_deadline_misses']==0 and after['file_underruns']==0
assert a.p['txlevel']==40 and a.p['txdepth']==50
s.save_params=_saved_save
if a.save_timer:a.save_timer.resume()
print('NORMAL_SAVE_RESTORED',s.save_params is _saved_save)
print('LEFT_RUNNING_TX_AM_579900_GEN_SINE_1000HZ_50_PERCENT_TX_LEVEL_40')
'''
    code = verify + '\nparams=' + repr(params) + '\n' + helpers + old.SETUP + old.PRODUCTION + finish
    compile(code, '<production GEN after soak>', 'exec')
    (run / 'ram-start.py').write_text(code, encoding='utf-8')
    passed = False
    t = None
    try:
        j = old.d.d.m.h.jopen()
        try:
            j.halt()
            firmware = (DEPLOY / 'candidate/firmware.bin').read_bytes()
            assert bytes(j.memory_read8(0, len(firmware))) == firmware
            print('INSTALLED_FIRMWARE_UNCHANGED', len(firmware), flush=True)
            old.safe_boot(j, old.symbols(DEPLOY / 'candidate/firmware.elf')['boardctrl_run_boot_py'])
        finally:
            j.close()
        time.sleep(1)
        t = old.d.af_connection()
        result = old.checked_execute(t, code, timeout=90)
        (run / 'production.log').write_text(result, encoding='utf-8')
        passed = True
    finally:
        if t:
            old.d.d.m.h.serial_close(t)
        if not passed:
            old.reset_after_test()


if __name__ == '__main__':
    main()
