# Доказване на реалната честота на Timer(6) / AGT5 на VK_RA4M2.
# Скриптът:
#   1) конфигурира Timer(6) на 1000 Hz callback rate (=> 500 Hz toggle на P302)
#   2) чете live AGT5 регистрите
#   3) decode-ва очакваната честота от live clock tree + raw AGT setup
#   4) мери реалната callback честота чрез DWT cycle counter (не чрез SysTick)

import machine
import micropython
from array import array
from machine import Pin, Timer

micropython.alloc_emergency_exception_buf(128)

SYSTEM_BASE = 0x4001E000
SCKDIVCR = SYSTEM_BASE + 0x20
SCKSCR = SYSTEM_BASE + 0x26
PLLCCR = SYSTEM_BASE + 0x28

AGT5_BASE = 0x400E8500
AGT5_AGT = AGT5_BASE + 0x00
AGT5_AGTCMA = AGT5_BASE + 0x02
AGT5_AGTCMB = AGT5_BASE + 0x04
AGT5_AGTCR = AGT5_BASE + 0x08
AGT5_AGTMR1 = AGT5_BASE + 0x09
AGT5_AGTMR2 = AGT5_BASE + 0x0A
AGT5_AGTCMSR = AGT5_BASE + 0x0E

DEMCR = 0xE000EDFC
DWT_CTRL = 0xE0001000
DWT_CYCCNT = 0xE0001004

TOGGLE_PIN = "P302"
TIMER_ID = 6
REQ_IRQ_HZ = 1000
MEASURE_CALLBACKS = 400

div_values = {0: 1, 1: 2, 2: 4, 3: 8, 4: 16, 5: 32, 6: 64, 7: 128, 8: 3, 9: 6, 10: 12}
clock_names = {0: "HOCO", 1: "MOCO", 2: "LOCO", 3: "MAIN_OSC", 4: "SUBCLOCK", 5: "PLL", 6: "PLL2"}


def read8(addr):
    return machine.mem8[addr]


def read16(addr):
    return machine.mem16[addr]


def read32(addr):
    return machine.mem32[addr]


def decode_div(encoded):
    return div_values.get(encoded, None)


def fmt_div(value):
    return ("/%d" % value) if value is not None else "unknown"


def get_live_clocks():
    sckdivcr = read32(SCKDIVCR)
    sckscr = read8(SCKSCR) & 0x07
    pllccr = read16(PLLCCR)

    ick_div = decode_div((sckdivcr >> 24) & 0x0F)
    pclkb_div = decode_div((sckdivcr >> 8) & 0x0F)
    plidiv = decode_div(pllccr & 0x03)
    plsrcsel = (pllccr >> 4) & 0x01
    pllmul = (((pllccr >> 8) & 0x3F) + 1) / 2

    source_hz = None
    if sckscr == 3:
        source_hz = 16_000_000
    elif sckscr == 5 and plsrcsel == 0 and plidiv is not None:
        source_hz = int((16_000_000 // plidiv) * pllmul)

    iclk_hz = source_hz // ick_div if source_hz is not None and ick_div else None
    pclkb_hz = source_hz // pclkb_div if source_hz is not None and pclkb_div else None

    return {
        "sckdivcr": sckdivcr,
        "sys_source": clock_names.get(sckscr, "UNKNOWN"),
        "pll_source": "MAIN_OSC" if plsrcsel == 0 else "HOCO",
        "pll_input_div": plidiv,
        "pll_mul": pllmul,
        "source_hz": source_hz,
        "iclk_hz": iclk_hz,
        "pclkb_hz": pclkb_hz,
    }


def get_agt5_raw():
    return {
        "AGT": read16(AGT5_AGT),
        "AGTCMA": read16(AGT5_AGTCMA),
        "AGTCMB": read16(AGT5_AGTCMB),
        "AGTCR": read8(AGT5_AGTCR),
        "AGTMR1": read8(AGT5_AGTMR1),
        "AGTMR2": read8(AGT5_AGTMR2),
        "AGTCMSR": read8(AGT5_AGTCMSR),
    }


def decode_agt_tick_hz(agtmr1, agtmr2, pclkb_hz):
    tck = (agtmr1 >> 4) & 0x07
    cks = agtmr2 & 0x07

    if (tck & ~0x03) == 0:
        if pclkb_hz is None:
            return None, "PCLKB(?)", tck, cks
        divider = tck
        if divider != 0:
            divider ^= 2
        return pclkb_hz >> divider, "PCLKB path", tck, cks

    return 32768 >> cks, "32k path", tck, cks


def dwt_enable():
    machine.mem32[DEMCR] = read32(DEMCR) | (1 << 24)
    machine.mem32[DWT_CYCCNT] = 0
    machine.mem32[DWT_CTRL] = read32(DWT_CTRL) | 1


stats = array("I", [0, 0])
tpin = Pin(TOGGLE_PIN, Pin.OUT, value=0)


def _toggle(_t):
    stats[1] ^= 1
    tpin.value(stats[1])
    stats[0] += 1


print("=== Timer(6) / AGT5 proof ===")
print("Requested callback rate =", REQ_IRQ_HZ, "Hz")
print("Expected square wave on", TOGGLE_PIN, "=", REQ_IRQ_HZ // 2, "Hz")

clocks = get_live_clocks()
print("Live system source      =", clocks["sys_source"])
print("Live PLL source         =", clocks["pll_source"])
print("Live PLL input div      =", fmt_div(clocks["pll_input_div"]))
print("Live PLL multiplier     = x%.1f" % clocks["pll_mul"])
print("Live source clock       =", clocks["source_hz"], "Hz")
print("Live ICLK               =", clocks["iclk_hz"], "Hz")
print("Live PCLKB              =", clocks["pclkb_hz"], "Hz")

ttmr = Timer(TIMER_ID)
start_count = 0
start_cycles = 0
end_count = 0
end_cycles = 0
expected_irq_hz = None

try:
    stats[0] = 0
    stats[1] = 0
    tpin.value(0)

    ttmr.init(freq=REQ_IRQ_HZ, callback=_toggle, hard=True)

    raw = get_agt5_raw()
    period_counts = ttmr.period()
    driver_freq = ttmr.freq()

    tick_hz, tick_path, tck, cks = decode_agt_tick_hz(raw["AGTMR1"], raw["AGTMR2"], clocks["pclkb_hz"])
    expected_irq_hz = (tick_hz / period_counts) if tick_hz is not None and period_counts else None
    expected_sig_hz = (expected_irq_hz / 2) if expected_irq_hz is not None else None

    print()
    print("AGT5 raw registers:")
    print("  AGT     =", hex(raw["AGT"]))
    print("  AGTCMA  =", hex(raw["AGTCMA"]))
    print("  AGTCMB  =", hex(raw["AGTCMB"]))
    print("  AGTCR   =", hex(raw["AGTCR"]))
    print("  AGTMR1  =", hex(raw["AGTMR1"]))
    print("  AGTMR2  =", hex(raw["AGTMR2"]))
    print("  AGTCMSR =", hex(raw["AGTCMSR"]))
    print("  TCK     =", tck)
    print("  CKS     =", cks)
    print("  Timer.period() raw counts =", period_counts)
    print("  Timer.freq() getter       =", driver_freq, "Hz")

    if expected_irq_hz is not None:
        print("  Decoded AGT tick path     =", tick_path)
        print("  Decoded AGT tick clock    =", tick_hz, "Hz")
        print("  Expected IRQ from raw cfg = %.3f Hz" % expected_irq_hz)
        print("  Expected signal on P302   = %.3f Hz (period %.6f ms)" % (expected_sig_hz, 1000.0 / expected_sig_hz))

    dwt_enable()

    while stats[0] < 16:
        pass

    start_count = stats[0]
    start_cycles = read32(DWT_CYCCNT)
    target = start_count + MEASURE_CALLBACKS

    while stats[0] < target:
        pass

    end_cycles = read32(DWT_CYCCNT)
    end_count = stats[0]

finally:
    ttmr.deinit()
    tpin.value(0)

elapsed_callbacks = end_count - start_count
elapsed_cycles = (end_cycles - start_cycles) & 0xFFFFFFFF

print()
print("DWT measurement:")
print("  Measured callbacks       =", elapsed_callbacks)
print("  Elapsed core cycles      =", elapsed_cycles)

if clocks["iclk_hz"]:
    elapsed_s = elapsed_cycles / clocks["iclk_hz"]
    measured_irq_hz = elapsed_callbacks / elapsed_s
    measured_sig_hz = measured_irq_hz / 2
    measured_period_ms = 1000.0 / measured_sig_hz

    print("  Measured IRQ frequency   = %.3f Hz" % measured_irq_hz)
    print("  Measured P302 frequency  = %.3f Hz" % measured_sig_hz)
    print("  Measured P302 period     = %.6f ms" % measured_period_ms)

    print()
    if abs(measured_irq_hz - REQ_IRQ_HZ) < 30:
        print("VERDICT: Timer(6) callback rate е близо до исканите 1000 Hz.")
    elif abs(measured_irq_hz - 1600) < 50:
        print("VERDICT: Timer(6) callback rate е около 1600 Hz => P302 е около 800 Hz => период ~1.25 ms.")
    elif abs(measured_irq_hz - 800) < 50:
        print("VERDICT: Timer(6) callback rate е около 800 Hz => P302 е около 400 Hz => период ~2.5 ms.")
    else:
        print("VERDICT: Timer(6) callback rate е различен и трябва да се decode-не по отпечатаните raw стойности.")

    if expected_irq_hz is not None:
        delta = measured_irq_hz - expected_irq_hz
        print("Measured - decoded IRQ delta = %.3f Hz" % delta)
else:
    print("Cannot convert DWT cycles to seconds: live ICLK could not be decoded.")