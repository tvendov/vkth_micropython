"""
SOSC (32.768 kHz crystal) on-target verification for VK_RA4M2.

Run via:
    mpremote connect COM21 run examples/test_sosc.py

Tests:
    T3 - machine.RTC().datetime() returns valid tuple
    T4 - SOSCCR.SOSTP=0 (running), SOMCR.SODRV=0 (standard drive)
    T5 - RTC.RCR4.RCKSEL=0 (RTC consumes SOSC, not LOCO)
    T6 - 30 s drift sample (informational; pair with host-side timing for accuracy)
"""
import machine
import time

# Renesas RA4M2 register addresses (User's Manual §8.2.20-21, §28.2)
SOSCCR = 0x4001E480  # bit 0 = SOSTP (1=stop, 0=run)
SOMCR  = 0x4001E481  # bit 1 = SODRV (0=standard, 1=low)
RCR2   = 0x40083024  # RTC Control 2; bit 0 = START
RCR4   = 0x40083028  # RTC Control 4; bit 0 = RCKSEL (0=SOSC, 1=LOCO)

def hexb(addr):
    return machine.mem8[addr]

print("=== VK_RA4M2 SOSC verification ===")

# --- T3 ---
rtc = machine.RTC()
dt = rtc.datetime()
print("[T3] RTC.datetime() =", dt)
assert isinstance(dt, tuple) and len(dt) == 8, "datetime() shape unexpected"

# --- T4 ---
sosccr = hexb(SOSCCR)
somcr  = hexb(SOMCR)
sostp  = sosccr & 0x01
sodrv  = (somcr >> 1) & 0x01
print("[T4] SOSCCR=0x%02X (SOSTP=%d)  SOMCR=0x%02X (SODRV=%d)" % (sosccr, sostp, somcr, sodrv))
assert sostp == 0, "SOSC is STOPPED — bsp_cfg.h SUBCLOCK_POPULATED=0?"
assert sodrv == 0, "SODRV != Standard — check bsp_cfg.h SUBCLOCK_DRIVE"

# --- T5 ---
rcr4   = hexb(RCR4)
rckSel = rcr4 & 0x01
rcr2   = hexb(RCR2)
start  = rcr2 & 0x01
print("[T5] RCR4=0x%02X (RCKSEL=%d -> %s)  RCR2.START=%d" % (
    rcr4, rckSel, "SOSC" if rckSel == 0 else "LOCO", start))
assert rckSel == 0, "RTC source is LOCO — MICROPY_HW_RTC_SOURCE != 0?"
assert start == 1, "RTC counter is not running"

# --- T6: short drift sample (informational) ---
# Read RTC seconds twice with on-target sleep; compares RTC tick rate vs systick.
# Drift estimate is *upper-bounded* by systick accuracy (also derived from CPU PLL).
# Definitive drift test requires host-side wall clock (mpremote) — see test plan T6.
print("[T6] Sampling 10 s …")
t_a = rtc.datetime()
ms_a = time.ticks_ms()
time.sleep(10)
t_b = rtc.datetime()
ms_b = time.ticks_ms()
sec_a = t_a[5] * 60 + t_a[6]
sec_b = t_b[5] * 60 + t_b[6]
delta_rtc = (sec_b - sec_a) % 3600
delta_sys = (time.ticks_diff(ms_b, ms_a)) / 1000.0
print("    RTC delta=%ds   systick delta=%.3fs   diff=%+.3fs" % (delta_rtc, delta_sys, delta_rtc - delta_sys))

print("=== ALL ASSERTIONS PASSED ===")
