"""Read actual DTC descriptors after checked stop; no descriptor patching.

Only the public IQTX API starts/stops the production engine. Tests then inspect
the final SRAM addresses. DAC registers are neutral after stop, so this does not
claim to capture the last analog pair or measure the analog skew.
"""
import machine,time,json,gc
import lvgl as lv
if not lv.is_initialized(): lv.init()
IQTX=machine.IQTX
checks=0

def check(ok,message):
    global checks
    checks+=1
    if not ok: raise AssertionError(message)

def emit(event,**data):
    data['event']=event
    print('IQTX_DTC '+json.dumps(data))

def ram(address):
    check(0x1ffe0000<=address<0x20080000,'pointer outside RA6M3 SRAM')
    return address

def chain(irq):
    table=ram(machine.mem32[0x40005404])
    check(table%1024==0,'DTC vector alignment')
    descriptor=ram(machine.mem32[table+irq*4])
    check(descriptor%4==0,'descriptor alignment')
    return descriptor

def src(descriptor,index): return machine.mem32[descriptor+16*index+4]
def dest(descriptor,index): return machine.mem32[descriptor+16*index+8]

try:
    tx=IQTX(mode=IQTX.CW,rate=44000,ramp_samples=220)
    try:
        descriptor=chain(15+tx.status()['timer'])
        tx.start()
        durations=[]
        for direction,endpoint in ((True,2848),(False,2048)):
            started=time.ticks_us()
            tx.key(direction)
            durations.append(time.ticks_diff(time.ticks_us(),started))
            time.sleep_ms(10)
            s=tx.status()
            check(s['error']==0 and s['unexpected_callbacks']==0,'CW callback/error')
            check(s['i_code']==endpoint and s['q_code']==2048,'CW endpoint')
            # D0 control/SAR/count are changed by the completion chain itself.
            word=machine.mem32[descriptor]
            check((word>>30)&3==1,'CW D0 did not become repeat')
            check((word>>22)&3==0,'CW D0 chain still enabled')
            check((word>>26)&3==0,'CW hold source not fixed')
            check(machine.mem16[descriptor+14]==0x0101,'CW repeat count')
            check(dest(descriptor,0)==0x4005e000,'CW destination')
            check(machine.mem16[ram(src(descriptor,0))]==endpoint,'CW hold source code')
            emit('cw_hardware_hold',direction=direction,word=hex(word),count=hex(machine.mem16[descriptor+14]),source=hex(src(descriptor,0)),status=s)
        emit('cw_44k_key_us',durations=durations)
    finally: tx.deinit()

    for mode,gain in ((IQTX.AM,2),(IQTX.FM,1),(IQTX.FM,2)):
        gc.collect()
        tx=IQTX(mode=mode,fm_gain=gain)
        try:
            descriptor=chain(48)
            i_index=4 if mode==IQTX.AM else 5+gain
            bank=ram(src(descriptor,i_index))
            check(bank%65536==0,'LUT bank alignment')
            initial_irq=tx.status()['unexpected_callbacks']
            for run in range(8):
                tx.start()
                time.sleep_ms(15+run)
                tx.stop()
                s=tx.status()
                check(s['quiesced'] and not s['running'],'not stopped')
                check(s['error']==0 and s['unexpected_callbacks']==initial_irq,'IRQ/error')
                raw=machine.mem16[ram(dest(descriptor,0))]
                check(raw==s['last_adc'],'ADC snapshot register disagrees')
                observed_i=src(descriptor,i_index)
                if mode==IQTX.AM:
                    offset=raw*2
                    check(machine.mem16[0x40054104]==offset,'AM DOC != 2*ADC')
                    check(observed_i==bank+offset,'AM patched SAR')
                    expected=2048+min(max(800+raw-2048,0),1600)
                    check(machine.mem16[ram(observed_i)]==expected,'AM LUT code')
                    observed_q=None
                else:
                    phase=machine.mem16[ram(dest(descriptor,2+gain))]
                    check(phase==s['phase'],'phase snapshot')
                    check(machine.mem16[0x40054104]==phase,'DOC final phase')
                    offset=(phase>>8)*256
                    observed_q=src(descriptor,i_index+1)
                    check(observed_i==bank+offset,'FM I patched SAR')
                    check(observed_q==observed_i+2,'FM Q did not share index')
                emit('stopped_chain',mode=mode,gain=gain,iteration=run,bank=hex(bank),raw=raw,phase=s['phase'],sar_i=hex(observed_i),sar_q=hex(observed_q) if observed_q else None)
        finally: tx.deinit()
    emit('summary',checks=checks,passed=True,analog_pair_captured=False)
finally:
    IQTX.release()
