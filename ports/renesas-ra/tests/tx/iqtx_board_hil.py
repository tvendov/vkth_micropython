"""Digital RA6M3 IQTX bench tests. Execute from RAM via run_board_hil.py.

RF PA must be disconnected. No analog waveform/spectrum acceptance is implied.
"""
import machine
import time
import gc
import json
import sys
import os
import lvgl as lv

if not lv.is_initialized():
    lv.init()

IQTX = machine.IQTX
checks = 0
failures = []


def emit(name, **values):
    values['event'] = name
    print('IQTX_HIL ' + json.dumps(values))


def check(condition, name):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(name)


def raises(call, exception, errno=None):
    try:
        call()
    except exception as exc:
        check(errno is None or (exc.args and exc.args[0] == errno), 'wrong errno: ' + repr(exc))
    else:
        raise AssertionError('expected exception ' + repr(exception))


def clean_status(tx):
    s = tx.status()
    check(s['error'] == 0, 'native error: ' + repr(s))
    check(s['unexpected_callbacks'] == 0, 'unexpected callback: ' + repr(s))
    return s


def neutral(tx):
    s = clean_status(tx)
    check(s['owned'] and not s['running'] and s['quiesced'], 'not owned+stopped')
    check(s['i_code'] == 2048 and s['q_code'] == 2048 and not s['keyed'], 'not neutral')
    return s


def test_validation():
    for config in ({'mode':3},{'rate':0},{'rate':48001},{'amplitude':0},
                   {'mode':IQTX.AM,'amplitude':1024},{'fm_gain':0},
                   {'ramp_samples':0},{'ramp_samples':257},{'i_zero':-1}):
        raises(lambda:IQTX(**config), ValueError)


def test_cw():
    before = machine.ADC(machine.Pin('P001'))
    tx = IQTX(mode=IQTX.CW, rate=8000, ramp_samples=256)
    try:
        s = neutral(tx)
        emit('cw_prepared',status=s)
        check(s['lut_allocation_bytes'] == 0 and s['cw_pin'] is None, 'CW resources')
        raises(lambda:IQTX(), OSError, 16)
        raises(lambda:before.read_u16(), OSError, 16)
        raises(lambda:machine.DAC(machine.Pin('P014')), OSError, 16)
        tx.start()
        tx.start()
        check(clean_status(tx)['running'], 'start failed')
        for down, endpoint in ((True,2848),(False,2048),(True,2848)):
            started = time.ticks_us()
            tx.key(down)
            call_us = time.ticks_diff(time.ticks_us(),started)
            previous = machine.mem16[0x4005e000]
            first = previous
            samples = 0
            deadline = time.ticks_ms()
            while time.ticks_diff(time.ticks_ms(),deadline) < 45:
                value = machine.mem16[0x4005e000]
                check(2048 <= value <= 2848, 'CW range')
                check(value >= previous if down else value <= previous, 'CW nonmonotonic')
                check(machine.mem16[0x4005e002] == 2048, 'CW Q changed')
                previous = value
                samples += 1
                time.sleep_us(100)
            s = clean_status(tx)
            emit('cw_edge',down=down,call_us=call_us,first=first,last=s['i_code'],polls=samples,status=s)
            check(s['i_code'] == endpoint, 'CW missed endpoint')
            check(s['keyed'] == down and s['transfer_count'] == 4, 'CW state')
            check(call_us < 32000, 'key command blocked for entire ramp')
            tx.key(down)
            time.sleep_ms(40)
            check(clean_status(tx)['i_code'] == endpoint, 'CW hold changed')
        tx.key(False)
        time.sleep_ms(5)
        mid = machine.mem16[0x4005e000]
        tx.key(True)
        time.sleep_ms(40)
        check(clean_status(tx)['i_code'] == 2848, 'CW reversal endpoint')
        emit('cw_reversal',intermediate=mid)
        tx.stop()
        tx.stop()
        neutral(tx)
        raises(lambda:IQTX(), OSError,16)
    finally:
        tx.deinit()
    raises(lambda:tx.status(), OSError,19)
    tx.deinit()
    check(0 <= before.read_u16() <= 65535,'ADC after CW')
    replacement = IQTX()
    tx.deinit()
    neutral(replacement)
    replacement.deinit()


def doc_proof(tx):
    neutral(tx)
    for initial,addend,expected in ((0xfffa,8,2),(0,0xffff,0xffff),(0xffff,1,0),(123,456,579)):
        machine.mem8[0x40054100] = 0x41
        machine.mem16[0x40054104] = initial
        machine.mem16[0x40054102] = addend
        result = machine.mem16[0x40054104]
        check(result == expected,'DOC sum/wrap')
        check(bool(machine.mem8[0x40054100] & 0x20) == (initial+addend>65535),'DOC overflow flag')
        emit('doc_vector',initial=initial,addend=addend,result=result)
    machine.mem8[0x40054100] = 0x41
    machine.mem16[0x40054104] = 0


def test_modulation(mode,gain=2):
    gc.collect()
    before = machine.ADC(machine.Pin('P001'))
    tx = IQTX(mode=mode,fm_gain=gain)
    try:
        s = neutral(tx)
        emit('mod_prepared',status=s,heap=gc.mem_free())
        check(s['lut_allocation_bytes']==131071,'LUT allocation')
        check(s['transfer_count']==(6 if mode==IQTX.AM else 7+gain),'chain size')
        check(s['period_counts']==(s['timer_clock']+22000)//44000,'rounded timer')
        doc_proof(tx)
        tx.start()
        tx.start()
        minimum,maximum=4095,0
        phases=set()
        raws=set()
        started=time.ticks_ms()
        last_report=started
        polls=0
        while time.ticks_diff(time.ticks_ms(),started)<10000:
            s=clean_status(tx)
            check(s['running'] and not s['quiesced'],'flow stopped')
            check(0 <= s['last_adc'] <= 4095,'ADC format')
            if mode==IQTX.AM:
                check(2048<=s['i_code']<=3648 and s['q_code']==2048,'AM DAC range')
            else:
                check(1248<=s['i_code']<=2848 and 1248<=s['q_code']<=2848,'FM DAC range')
                if gain==2:
                    check(s['phase']%2==0,'FM phase parity')
            minimum=min(minimum,s['i_code'])
            maximum=max(maximum,s['i_code'])
            if len(phases)<64: phases.add(s['phase'])
            if len(raws)<64: raws.add(s['last_adc'])
            polls+=1
            if polls%100==0: gc.collect()
            now=time.ticks_ms()
            if time.ticks_diff(now,last_report)>=2000:
                emit('mod_progress',mode=mode,gain=gain,elapsed=time.ticks_diff(now,started),status=s)
                last_report=now
            time.sleep_ms(2)
        emit('mod_observed',mode=mode,gain=gain,i_min=minimum,i_max=maximum,phase_values=len(phases),adc_values=sorted(raws),polls=polls)
        tx.stop()
        neutral(tx)
        raises(lambda:IQTX(),OSError,16)
        tx.start()
        time.sleep_ms(30)
        clean_status(tx)
        tx.stop()
        neutral(tx)
    finally:
        tx.deinit()
    raises(lambda:before.read_u16(),OSError,19)
    replacement=machine.ADC(machine.Pin('P001'))
    check(0<=replacement.read_u16()<=65535,'ADC reconstructed')


def test_orphan_root():
    gc.collect()
    def leave_running():
        tx=IQTX(mode=IQTX.FM)
        tx.start()
    leave_running()
    gc.collect()
    for unused in range(8):
        pressure=bytearray(24000)
        pressure[0]=17
        del pressure
        gc.collect()
        check(1248<=machine.mem16[0x4005e000]<=2848,'orphan I code')
        check(1248<=machine.mem16[0x4005e002]<=2848,'orphan Q code')
    raises(lambda:IQTX(),OSError,16)
    IQTX.release()
    gc.collect()
    tx=IQTX(mode=IQTX.FM)
    neutral(tx)
    tx.deinit()


emit('identity',uname=list(os.uname()),uid=machine.unique_id().hex(),heap=gc.mem_free())
for name,func in (('validation',test_validation),('cw',test_cw),
                  ('am',lambda:test_modulation(IQTX.AM)),
                  ('fm_gain1',lambda:test_modulation(IQTX.FM,1)),
                  ('fm_gain2',lambda:test_modulation(IQTX.FM,2)),
                  ('gc_orphan_root',test_orphan_root)):
    emit('begin',stage=name)
    try:
        IQTX.release()
        gc.collect()
        func()
        emit('pass',stage=name,checks=checks)
    except Exception as exc:
        failures.append((name,repr(exc)))
        emit('fail',stage=name,error=repr(exc),checks=checks)
        sys.print_exception(exc)
    finally:
        IQTX.release()
        gc.collect()
emit('summary',checks=checks,failures=failures,analog_verified=False,full_isr_trace_verified=False)
if failures:
    raise AssertionError('IQTX HIL failed: ' + repr(failures))
