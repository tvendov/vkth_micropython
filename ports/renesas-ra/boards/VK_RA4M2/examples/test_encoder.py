# TEST B3: DIAGNOSTIC - ISR counter + compare regs
# Shows if ISR fires but callback doesn't run
# Run: mpremote run test_encoder.py
# Stop: Ctrl+C

from machine import Encoder
import asyncio

flag = asyncio.ThreadSafeFlag()
irq_count = 0
wake_count = 0

def on_move(e):
    global irq_count
    irq_count += 1
    flag.set()

enc = Encoder(mode=Encoder.X2, filter=Encoder.FILTER_64, value=0, debounce=0)
enc.irq(handler=on_move)
print(enc)
print("TEST B3: ISR diagnostics")
print("Rotate encoder slowly. Ctrl+C to stop.")

async def reader():
    global wake_count
    prev_pos = enc.value()
    while True:
        await flag.wait()
        wake_count += 1
        pos = enc.value()
        if pos != prev_pos:
            s = enc.status()
            cnt = s['GTCNT']
            ccra = s.get('GTCCRA', '?')
            ccrb = s.get('GTCCRB', '?')
            isr = s.get('isr_count', '?')
            sf = s.get('sched_fail', '?')
            coal = s.get('irq_coalesced', '?')
            sched = s.get('irq_scheduled', '?')
            pend = s.get('irq_pending', '?')
            gtst = s['GTST']
            print("pos={} cnt=0x{:04X} ccra=0x{:04X} ccrb=0x{:04X} gtst=0x{:X} isr={} coal={} sf={} pend={} cb={} wk={}".format(
                pos, cnt, ccra, ccrb, gtst, isr, coal, sf, pend, irq_count, wake_count))
            prev_pos = pos

async def main():
    asyncio.create_task(reader())
    while True:
        await asyncio.sleep_ms(999999)

try:
    asyncio.run(main())
except KeyboardInterrupt:
    pass

print("\n--- FINAL ---")
s = enc.status()
print("pos:", enc.value(), "irq:", irq_count)
print("status:", s)
enc.irq(handler=None)
enc.deinit()

