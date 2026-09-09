"""Approved TX AF/PWR upload. Known target; production files only in flash."""
from pathlib import Path
from datetime import datetime
import ast
import json
import shutil
import time
import deploy_tx_af_20260907 as a
from deploy_tx_audio_20260908 import safe_boot, symbols, snapshot
from deploy_axis_resume_20260908 import verify_record
from rx_dac512_ram_hil_20260907 import checked_execute

d = a.d
WORK = Path(__file__).resolve().parent
OLD = d.m.ROOT/'backups/gen-smooth-1120000058-20260909-004135'
OLD_SHA = '692fa2c29ed92d5a16d005c6f3669ba6a4ea18603f3731a572236b4be41181a3'
NEW_SHA = '88f641d14e2c37dd40d4333ab8f3583009df552baca15d6f80100071cc57315c'
PY_SHA = 'b41b71b18a93574d0ace7251515c0613a4dc96b66d78f1e5e531c4c2b7dfae29'
MPY_SHA = 'cd12449adfe5b28ad90716eade51d322fa0d9af5cb9583654b2bec050f46e70c'


def main():
    binary = (d.BUILD/'firmware.bin').read_bytes()
    oldbinary = (OLD/'candidate/firmware.bin').read_bytes()
    source = (d.m.ROOT/'sdr_single.py').read_bytes()
    mpy = (WORK/'sdr_single_tx_filter_candidate.mpy').read_bytes()
    assert d.m.sha(binary) == NEW_SHA and d.m.sha(oldbinary) == OLD_SHA
    assert d.m.sha(source) == PY_SHA and d.m.sha(mpy) == MPY_SHA
    assert source == (d.BUILD.parent/'boards/VK_RA6M3/examples/sdr_single.py').read_bytes()
    sym = symbols(d.BUILD/'firmware.elf')
    assert len(binary) < 0x1c0000 and sym['__HeapLimit']-sym['__HeapBase'] == 281600
    assert not any(k.startswith(('cv2_', 'mp_module_cv2', 'ra_lab_')) for k in sym)
    assert all(k in sym for k in ('ra_tx_core_af_prepare', 'ra_tx_core_af_sample'))
    run = d.m.ROOT/'backups'/('tx-filter-1120000058-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    candidate = run/'candidate'
    candidate.mkdir(parents=True)
    for name in ('firmware.bin','firmware.elf','firmware.map'):
        shutil.copy2(d.BUILD/name, candidate/name)
    shutil.copy2(d.m.ROOT/'sdr_single.py', candidate/'sdr_single.py')
    shutil.copy2(WORK/'sdr_single_tx_filter_candidate.mpy', candidate/'sdr_single.mpy')
    (WORK/'tx-filter-deployment-path.txt').write_text(str(run), encoding='utf-8')
    print('ARCHIVE', run, flush=True)
    t = a.af_connection()
    try:
        result = checked_execute(t, "import sys\ns=sys.modules.get('sdr_single')\na=s._KEEP.get('app') if s else None\nprint('SAVED_UI',repr((a.p,a._trx_state,a._tx_mode,a._tx_source_settings,a._iq_file_loop) if a else None))\n", timeout=20)
        saved = ast.literal_eval(next(x[len('SAVED_UI '):] for x in result.splitlines() if x.startswith('SAVED_UI ')))
        (run/'production-before.json').write_text(json.dumps(saved, indent=2))
    finally:
        d.m.h.serial_close(t)
    j = d.m.h.jopen()
    ready = False
    try:
        j.halt()
        before_internal = bytes(j.memory_read8(0, 0x200000))
        assert before_internal[:len(oldbinary)] == oldbinary, 'Installed firmware conflict; NO writes'
        (run/'internal-before-2MiB.bin').write_bytes(before_internal)
        record = bytes(j.memory_read8(0x40100000,512))
        end = verify_record(j, record)
        (run/'dataflash-record-before.bin').write_bytes(record[:end])
        safe_boot(j, symbols(OLD/'candidate/firmware.elf')['boardctrl_run_boot_py'])
        j.halt()
        snapshot(j, run/'qspi-before-16MiB.bin')
        files = d.manifest(run/'qspi-before-16MiB.bin')
        (run/'files-before.json').write_text(json.dumps(files,indent=2))
        assert files['/sdr_single.mpy']['sha256']=='d6cc332a76cf4377f38e123e590563a2efa5ea56c87a115aac67bacd25e45b98'
        assert files['/sdr_single.py.source']['sha256']=='f6bee007fede2dcd3d7e5673005791a8f7103eb6a228fc19645fac5343142467'
        for name in ('sdr_single.pre-txfilter.mpy','sdr_single.pre-txfilter.py.source',
                     'sdr_single.mpy.new','sdr_single.py.source.new'):
            assert '/'+name not in files, name
        print('BACKUP_PASS',len(files),'files',flush=True)
        j.flash_file(str(candidate/'firmware.bin'), 0)
        j.halt()
        assert bytes(j.memory_read8(0,len(binary))) == binary
        assert bytes(j.memory_read8(0x1c0000,0x40000)) == before_internal[0x1c0000:]
        assert bytes(j.memory_read8(0x40100000,end)) == record[:end]
        print('FIRMWARE_READBACK_PASS',len(binary),flush=True)
        safe_boot(j, sym['boardctrl_run_boot_py'])
        ready = True
    finally:
        if not ready: j.reset(halt=True)
        j.close()
    time.sleep(1)
    t = a.af_connection()
    try:
        d.execute(t,'import os, gc; gc.collect()')
        assert t.eval("os.statvfs('/flash')[0]*os.statvfs('/flash')[3]") > len(source)+len(mpy)+32768
        for target,data in (('sdr_single.py.source',source),('sdr_single.mpy',mpy)):
            last=[-1]
            def progress(count,total):
                part=count//32768
                if part!=last[0] or count==total:
                    print('UPLOAD',target,count,total,flush=True)
                    last[0]=part
            t.fs_writefile('/flash/'+target+'.new',data,chunk_size=512,progress_callback=progress)
            assert t.fs_hashfile('/flash/'+target+'.new','sha256',chunk_size=1024).hex()==d.m.sha(data)
            print('APP_READBACK_PASS',target,len(data),flush=True)
        checked_execute(t,"os.rename('/flash/sdr_single.mpy','/flash/sdr_single.pre-txfilter.mpy')\nos.rename('/flash/sdr_single.py.source','/flash/sdr_single.pre-txfilter.py.source')\nos.rename('/flash/sdr_single.mpy.new','/flash/sdr_single.mpy')\nos.rename('/flash/sdr_single.py.source.new','/flash/sdr_single.py.source')\nos.sync()\n")
        d.execute(t,"import machine; assert machine.IQTX.AUDIO_FILTER_API_VERSION==1; print('AF_API_V1_PASS')")
    finally:
        d.m.h.serial_close(t)
    j=d.m.h.jopen()
    try:
        j.halt()
        snapshot(j,run/'qspi-after-16MiB.bin')
        assert bytes(j.memory_read8(0,len(binary)))==binary
        assert bytes(j.memory_read8(0x40100000,end))==record[:end]
        after=d.manifest(run/'qspi-after-16MiB.bin')
        rename={'/sdr_single.mpy':'/sdr_single.pre-txfilter.mpy',
                '/sdr_single.py.source':'/sdr_single.pre-txfilter.py.source'}
        for path,rec in files.items(): assert after[rename.get(path,path)]==rec,path
        assert after['/sdr_single.mpy']['sha256']==MPY_SHA
        assert after['/sdr_single.py.source']['sha256']==PY_SHA
        assert len(after)==len(files)+2
        assert (run/'qspi-before-16MiB.bin').read_bytes()[0xc00000:] == (run/'qspi-after-16MiB.bin').read_bytes()[0xc00000:]
        (run/'files-after.json').write_text(json.dumps(after,indent=2))
        info=dict(status='PASS',probe=1120000058,port='COM25',firmware_sha256=NEW_SHA,
                  firmware_bytes=len(binary),source_sha256=PY_SHA,mpy_sha256=MPY_SHA,
                  heap_bytes=281600,previous_sha256=OLD_SHA,firmware_readback=True,
                  original_files_preserved=len(files),reserved_qspi_4mib_identical=True,
                  dataflash_record_identical=True,boot_main_unchanged=True,
                  tests_written=False,opencv=False,production_started=False)
        (run/'deployment.json').write_text(json.dumps(info,indent=2))
        print('TX_FILTER_DEPLOY_PASS_SAFE_REPL',info,flush=True)
    finally:
        j.restart()
        j.close()


if __name__=='__main__': main()
