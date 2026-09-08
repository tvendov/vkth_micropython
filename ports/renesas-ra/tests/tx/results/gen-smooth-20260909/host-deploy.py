"""GEN envelope firmware only. Known1120000058/COM25, full QSPI preservation."""
from pathlib import Path
from datetime import datetime
import ast
import json
import shutil
import deploy_tx_af_20260907 as a
from deploy_tx_audio_20260908 import safe_boot, symbols, snapshot
from deploy_axis_resume_20260908 import verify_record
from rx_dac512_ram_hil_20260907 import checked_execute

d = a.d
WORK = Path(__file__).resolve().parent
OLD = Path((WORK/'rx-tone-monitor-deployment-path.txt').read_text().strip())
OLD_SHA = 'ef22e94ba86b22463fe6d0d3ed0c7545e88e476a7e6e8a8ca7745752a313958d'
NEW_SHA = '692fa2c29ed92d5a16d005c6f3669ba6a4ea18603f3731a572236b4be41181a3'


def main():
    binary = (d.BUILD/'firmware.bin').read_bytes()
    oldbinary = (OLD/'candidate/firmware.bin').read_bytes()
    assert d.m.sha(binary) == NEW_SHA and d.m.sha(oldbinary) == OLD_SHA
    sym = symbols(d.BUILD/'firmware.elf')
    assert len(binary) < 0x1c0000 and sym['__HeapLimit']-sym['__HeapBase'] == 281600
    assert not any(k.startswith(('cv2_', 'mp_module_cv2', 'ra_lab_')) for k in sym)
    assert all(k in sym for k in ('ra_tone_smooth_next','ra_tone_smooth_request',
                                 'ra_tone_detect','ra_iq_adc_set_tone_monitor'))
    run = d.m.ROOT/'backups'/('gen-smooth-1120000058-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    candidate = run/'candidate'
    candidate.mkdir(parents=True)
    for name in ('firmware.bin','firmware.elf','firmware.map'):
        shutil.copy2(d.BUILD/name, candidate/name)
    (WORK/'gen-smooth-deployment-path.txt').write_text(str(run), encoding='utf-8')
    print('ARCHIVE', run, flush=True)
    t = a.af_connection()
    try:
        result = checked_execute(t, "import sdr_single as s\na=s._KEEP.get('app')\nprint('SAVED_UI',repr((a.p,a._trx_state,a._tx_mode,a._tx_source_settings,a._iq_file_loop) if a else (s.load_params(),'REPL',None,None,True)))\n", timeout=20)
        saved = ast.literal_eval(next(x[len('SAVED_UI '):] for x in result.splitlines() if x.startswith('SAVED_UI ')))
        (run/'production-before.json').write_text(json.dumps(saved, indent=2))
    finally:
        d.m.h.serial_close(t)
    j = d.m.h.jopen()
    complete = False
    try:
        j.halt()
        before = bytes(j.memory_read8(0, 0x200000))
        assert before[:len(oldbinary)] == oldbinary, 'Installed firmware conflict; NO writes'
        (run/'internal-before-2MiB.bin').write_bytes(before)
        record = bytes(j.memory_read8(0x40100000,512))
        end = verify_record(j, record)
        (run/'dataflash-record-before.bin').write_bytes(record[:end])
        safe_boot(j, symbols(OLD/'candidate/firmware.elf')['boardctrl_run_boot_py'])
        j.halt()
        snapshot(j, run/'qspi-before-16MiB.bin')
        files = d.manifest(run/'qspi-before-16MiB.bin')
        (run/'files-before.json').write_text(json.dumps(files,indent=2))
        j.flash_file(str(candidate/'firmware.bin'), 0)
        j.halt()
        assert bytes(j.memory_read8(0,len(binary))) == binary
        assert bytes(j.memory_read8(0x1c0000,0x40000)) == before[0x1c0000:]
        assert bytes(j.memory_read8(0x40100000,end)) == record[:end]
        safe_boot(j, sym['boardctrl_run_boot_py'])
        j.halt()
        snapshot(j, run/'qspi-after-16MiB.bin')
        assert (run/'qspi-before-16MiB.bin').read_bytes() == (run/'qspi-after-16MiB.bin').read_bytes()
        assert bytes(j.memory_read8(0,len(binary))) == binary
        assert bytes(j.memory_read8(0x40100000,end)) == record[:end]
        j.restart()
        complete = True
    finally:
        if not complete:
            j.reset(halt=True)
        j.close()
    info = dict(firmware_sha256=NEW_SHA,firmware_bytes=len(binary),heap_bytes=281600,
                previous_sha256=OLD_SHA,firmware_readback=True,original_files=len(files),
                qspi_16mib_identical=True,dataflash_written_record_identical=True,
                app_written=False,tests_written=False,opencv=False,lab=False)
    (run/'deployment.json').write_text(json.dumps(info,indent=2))
    print('GEN_SMOOTH_FIRMWARE_DEPLOY_PASS', info, flush=True)


if __name__=='__main__':main()
