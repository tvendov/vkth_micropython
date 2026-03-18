from array import array
from machine import DAC, Pin, mem8, mem32
import time


DAC_PIN = "P014"  # J19-5 / A4 / DA0
TIMER_CH = 0
AGT_IRQ = 46  # VECTOR_NUMBER_AGT0_INT on VK_RA4M2
AGT_EVENT = 0x040  # ELC/ICU event number for AGT0_INT on RA4M2

ICU_BASE = 0x40006000
ICU_DELSR_BASE = ICU_BASE + 0x280
ICU_IELSR_BASE = ICU_BASE + 0x300
DMAC_BASE = 0x40005000
DMA_BASE = 0x40005200
BUS_BASE = 0x40003000
CPSCU_BASE = 0x40008000
DMAC_CH_STRIDE = 0x40

DMAST = DMA_BASE + 0x00
DMECHR = DMA_BASE + 0x40
BUS3ERRSTAT = BUS_BASE + 0x1A20
DMACDTCERRSTAT = BUS_BASE + 0x1A24
BUS3ERRCLR = BUS_BASE + 0x1A28
DMACDTCERRCLR = BUS_BASE + 0x1A2C
ICUSARC = CPSCU_BASE + 0x48

MID = 2048
AMP = 1200


def make_ramp(length):
    start = max(0, MID - AMP)
    stop = min(4095, MID + AMP)
    span = stop - start
    buf = array("H", [MID] * length)
    for i in range(length):
        buf[i] = start + (span * i) // max(1, length - 1)
    return buf


BUF = make_ramp(2048)


def reg32(addr):
    return mem32[addr]


def reg8(addr):
    return mem8[addr]


def clear_error_state():
    dmechr = reg32(DMECHR)
    if dmechr & 0x10000:
        mem32[DMECHR] = 0x10000
    bus3 = reg8(BUS3ERRSTAT)
    if bus3:
        mem8[BUS3ERRCLR] = bus3 & 0x1B
    dmacdtc = reg8(DMACDTCERRSTAT)
    if dmacdtc & 0x01:
        mem8[DMACDTCERRCLR] = 0x01


def dump_icu():
    print("IELSR[{}] = 0x{:08x}".format(AGT_IRQ, reg32(ICU_IELSR_BASE + 4 * AGT_IRQ)))
    for ch in range(8):
        value = reg32(ICU_DELSR_BASE + 4 * ch)
        print("DELSR[{}] = 0x{:08x}".format(ch, value))


def dump_error_regs():
    dmechr = reg32(DMECHR)
    print(
        "DMAST=0x{:02x} ICUSARC=0x{:08x} DMECHR=0x{:08x} DMESTA={} DMECH={} BUS3ERRSTAT=0x{:02x} DMACDTCERRSTAT=0x{:02x}".format(
            reg8(DMAST),
            reg32(ICUSARC),
            dmechr,
            1 if (dmechr & 0x10000) else 0,
            dmechr & 0x7,
            reg8(BUS3ERRSTAT),
            reg8(DMACDTCERRSTAT),
        )
    )


def dump_dmac():
    for ch in range(8):
        base = DMAC_BASE + DMAC_CH_STRIDE * ch
        dmcra = reg32(base + 0x08)
        tail = reg32(base + 0x1C)
        dte = tail & 0xFF
        dmreq = (tail >> 8) & 0xFF
        dmsts = (tail >> 16) & 0xFF
        delsr = reg32(ICU_DELSR_BASE + 4 * ch)
        print(
            "DMAC{}: DMCRA=0x{:08x} DMCRAL={} DTE=0x{:02x} DMREQ=0x{:02x} DMSTS=0x{:02x} DELSR=0x{:08x}".format(
                ch,
                dmcra,
                dmcra & 0xFFFF,
                dte,
                dmreq,
                dmsts,
                delsr,
            )
        )


def snapshot(label):
    print("---", label, "---")
    print("playing =", dac.playing())
    dump_error_regs()
    dump_icu()
    dump_dmac()


print("Timed DAC DMAC register probe")
print("Using {} with timer={}".format(DAC_PIN, TIMER_CH))
dac = DAC(Pin(DAC_PIN))
dac.write(MID)

try:
    clear_error_state()
    dac.write_timed(
        BUF,
        12000,
        mode=DAC.NORMAL,
        transfer=DAC.TRANSFER_DMAC,
        timer=TIMER_CH,
    )
    snapshot("after start")
    time.sleep_ms(50)
    snapshot("after 50 ms")
    time.sleep_ms(200)
    snapshot("after 250 ms")
finally:
    try:
        dac.stop()
    except Exception:
        pass
    dac.write(MID)
    snapshot("after stop")
