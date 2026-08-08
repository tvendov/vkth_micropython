# Пример: Доказване дали 16 MHz кварцът се ползва на VK_RA4M2.
# Ресурси на VK_RA4M2: CPU = RA4M2, кварц = 16 MHz, системен clock source = live decode от SYSTEM регистрите.
# Бележка: Скриптът не разчита на compile-time константи; чете директно хардуерните регистри.

import machine  # Импортираме machine, за да четем live memory-mapped clock регистрите.

SYSTEM_BASE = 0x4001E000  # Base адресът на SYSTEM блока за RA4M2.
SCKDIVCR = SYSTEM_BASE + 0x20  # System Clock Division Control Register.
SCKSCR = SYSTEM_BASE + 0x26  # System Clock Source Control Register.
PLLCCR = SYSTEM_BASE + 0x28  # PLL Clock Control Register.
PLLCR = SYSTEM_BASE + 0x2A  # PLL Control Register.
OSCSF = SYSTEM_BASE + 0x3C  # Oscillation Stabilization Flag Register.
ST_CTRL = 0xE000E010  # SysTick Control and Status Register.
ST_LOAD = 0xE000E014  # SysTick Reload Value Register.

clock_names = {
    0: "HOCO",
    1: "MOCO",
    2: "LOCO",
    3: "MAIN_OSC",
    4: "SUBCLOCK",
    5: "PLL",
    6: "PLL2",
}

div_values = {
    0: 1,
    1: 2,
    2: 4,
    3: 8,
    4: 16,
    5: 32,
    6: 64,
    7: 128,
    8: 3,
    9: 6,
    10: 12,
}


def read8(addr):
    return machine.mem8[addr]


def read16(addr):
    return machine.mem16[addr]


def read32(addr):
    return machine.mem32[addr]


def decode_div(encoded):
    return div_values.get(encoded, None)


def fmt_div(encoded):
    value = decode_div(encoded)
    return ("/%d" % value) if value is not None else ("raw=%d" % encoded)


print("=== VK_RA4M2 clock proof: ползва ли се 16 MHz кварцът? ===")

sckdivcr = read32(SCKDIVCR)
sckscr = read8(SCKSCR) & 0x07
pllccr = read16(PLLCCR)
pllcr = read8(PLLCR)
oscsf = read8(OSCSF)
st_ctrl = read32(ST_CTRL)
st_load = read32(ST_LOAD)

ick_enc = (sckdivcr >> 24) & 0x0F
pclkb_enc = (sckdivcr >> 8) & 0x0F

plidiv_sel = pllccr & 0x03
plsrcsel = (pllccr >> 4) & 0x01
pllmul_reg = (pllccr >> 8) & 0x3F

pll_input_div = decode_div(plidiv_sel)
pll_mul = (pllmul_reg + 1) / 2
pll_source_name = "MAIN_OSC(16MHz кварц)" if plsrcsel == 0 else "HOCO"
sys_source_name = clock_names.get(sckscr, "UNKNOWN")

main_osc_ready = bool(oscsf & (1 << 3))
pll_ready = bool(oscsf & (1 << 5))
hoco_ready = bool(oscsf & (1 << 0))
pll_stopped = bool(pllcr & 0x01)

print("machine.freq()           =", machine.freq())
print("SCKSCR.CKSEL             =", sckscr, "=>", sys_source_name)
print("SCKDIVCR                 =", hex(sckdivcr))
print("  ICK divider            =", fmt_div(ick_enc))
print("  PCLKB divider          =", fmt_div(pclkb_enc))
print("PLLCCR                   =", hex(pllccr))
print("  PLL source             =", pll_source_name)
print("  PLL input divider      =", ("/%d" % pll_input_div) if pll_input_div is not None else ("raw=%d" % plidiv_sel))
print("  PLL multiplier         = x%.1f" % pll_mul)
print("PLLCR                    =", hex(pllcr), "(PLL stopped =", pll_stopped, ")")
print("OSCSF                    =", hex(oscsf))
print("  MOSCSF(main osc ready) =", main_osc_ready)
print("  PLLSF(pll ready)       =", pll_ready)
print("  HOCOSF(hoco ready)     =", hoco_ready)
print("SysTick LOAD             =", hex(st_load), "=> configured core =", (st_load + 1) * 1000, "Hz")
print("SysTick CTRL             =", hex(st_ctrl))

if pll_source_name.startswith("MAIN_OSC") and pll_input_div is not None:
    pll_in_hz = 16_000_000 // pll_input_div
    pll_out_hz = int(pll_in_hz * pll_mul)
    iclk_div = decode_div(ick_enc)
    pclkb_div = decode_div(pclkb_enc)
    print("Derived PLL input        =", pll_in_hz, "Hz")
    print("Derived PLL output       =", pll_out_hz, "Hz")
    if iclk_div is not None:
        print("Derived ICLK             =", pll_out_hz // iclk_div, "Hz")
    if pclkb_div is not None:
        print("Derived PCLKB            =", pll_out_hz // pclkb_div, "Hz")

print()
if sys_source_name == "MAIN_OSC" and main_osc_ready:
    print("VERDICT: ДА — системата върви директно от 16 MHz кварца.")
elif sys_source_name == "PLL" and plsrcsel == 0 and main_osc_ready and pll_ready and not pll_stopped:
    print("VERDICT: ДА — 16 MHz кварцът се ползва като вход към PLL и PLL е текущият system clock source.")
elif plsrcsel == 0 and main_osc_ready:
    print("VERDICT: ЧАСТИЧНО — кварцът работи и е PLL source, но текущият system clock source не е PLL/Main Osc.")
elif main_osc_ready:
    print("VERDICT: НЕ КАТО АКТИВЕН SOURCE — main oscillator е стабилен, но live decode не показва, че системата върви от него.")
else:
    print("VERDICT: НЕ Е ДОКАЗАНО — live регистрите не показват активен 16 MHz main oscillator в clock path.")

if machine.freq() != (st_load + 1) * 1000:
    print("WARNING: machine.freq() и SysTick configured core НЕ съвпадат.")
    print("Това е силен индикатор за timebase/clock mismatch.")