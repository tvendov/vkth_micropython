"""iter22-A probe: verify RA4M2 RTC clock source per memory
reference_vk_ra4m2_sosc_xtal. RTC peripheral base = 0x40044000 (RA4M2 HUM §24).

Key register: RCR4 (offset 0x28, byte) — bit 0 RCKSEL: 0=LOCO (±15% drift),
1=SUBCLK (±20-50 ppm).

For LoRaWAN RX1 timing 5s window, LOCO ±15% = ±750ms drift would explain
RX2_TIMEOUT despite MinRxSymbols=24."""

import machine

RTC_BASE = 0x40044000

# Per RA4M2 HUM §24.2 register map
R_RCR1 = RTC_BASE + 0x22   # interrupt enable
R_RCR2 = RTC_BASE + 0x24   # start/stop/reset
R_RCR4 = RTC_BASE + 0x28   # RCKSEL (count source)
R_RFRH = RTC_BASE + 0x2A   # frequency H (only when SUBCLK)
R_RFRL = RTC_BASE + 0x2C   # frequency L

# Also check the SOSC and LOCO clock control via System Clock Control
R_SCKDIVCR = 0x4001E020    # System Clock Divider Control
R_SCKSCR   = 0x4001E026    # System Clock Source Control
R_SOSCCR   = 0x4001E480    # SOSC control (0=running, 1=stopped)
R_LOCOCR   = 0x4001E490    # LOCO control (0=running, 1=stopped)

rcr1 = machine.mem8[R_RCR1]
rcr2 = machine.mem8[R_RCR2]
rcr4 = machine.mem8[R_RCR4]
rfrh = machine.mem16[R_RFRH]
rfrl = machine.mem16[R_RFRL]

print("RTC RCR1 (irq enable)  =", hex(rcr1))
print("RTC RCR2 (start/reset) =", hex(rcr2),
      " START=" + ("1" if (rcr2 & 0x01) else "0"))
print("RTC RCR4 (RCKSEL)      =", hex(rcr4),
      " src=" + ("SUBCLK (32.768kHz xtal, ±20-50ppm)" if (rcr4 & 0x01) else "LOCO (32.768kHz internal, ±15%)"))
print("RTC RFRH (freq H)      =", hex(rfrh))
print("RTC RFRL (freq L)      =", hex(rfrl))

soscr = machine.mem8[R_SOSCCR]
lococr = machine.mem8[R_LOCOCR]
print("SOSC CR (sub-osc stop) =", hex(soscr),
      " " + ("STOPPED" if (soscr & 0x01) else "running"))
print("LOCO CR (LOCO stop)    =", hex(lococr),
      " " + ("STOPPED" if (lococr & 0x01) else "running"))

if (rcr4 & 0x01) == 0:
    print("VERDICT: RTC on LOCO -> ±15% drift at 5s RX1 = ±750ms window misalign")
    print("FIX: regen FSP cfg with RTC clock_source = RTC_CLOCK_SOURCE_SUBCLK")
else:
    print("VERDICT: RTC on SUBCLK -> drift OK; RX2_TIMEOUT root cause elsewhere")
