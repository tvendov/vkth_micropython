from machine import DAC, Pin
from array import array
import time

DAC_PIN = "P014"   # или "P015"
MID = 2048
AMP = 1400

def clamp12(v):
    if v < 0:
        return 0
    if v > 4095:
        return 4095
    return v

def triangle_sample(i, period, amp):
    half = period // 2
    pos = i % period
    if pos < half:
        return -amp + (2 * amp * pos) // max(1, half - 1)
    pos -= half
    return amp - (2 * amp * pos) // max(1, half - 1)

def make_triangle_table(length, amp):
    buf = array("H", [MID] * length)
    for i in range(length):
        buf[i] = clamp12(MID + triangle_sample(i, length, amp))
    return buf

def make_tone_burst(length, cycles, amp):
    buf = array("H", [MID] * length)
    period = max(8, length // max(1, cycles))
    for i in range(length):
        level = (amp * (length - 1 - i)) // max(1, length - 1)
        buf[i] = clamp12(MID + triangle_sample(i, period, level))
    return buf

def make_noise_burst(length, amp):
    buf = array("H", [MID] * length)
    lfsr = 0xACE1
    for i in range(length):
        bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1
        lfsr = (lfsr >> 1) | (bit << 15)
        level = (amp * (length - 1 - i)) // max(1, length - 1)
        buf[i] = clamp12(MID + level if (lfsr & 1) else MID - level)
    return buf

def wait_until_idle(dac, timeout_ms=2500):
    start = time.ticks_ms()
    deadline = time.ticks_add(start, timeout_ms)
    while dac.playing():
        if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
            raise RuntimeError("timeout waiting for timed DAC to stop")
        time.sleep_ms(10)
    return time.ticks_diff(time.ticks_ms(), start)

def expect_duration(label, elapsed_ms, sample_count, sample_rate):
    expected_ms = max(1, (sample_count * 1000) // sample_rate)
    minimum_ms = max(5, (expected_ms * 3) // 4)
    if elapsed_ms < minimum_ms:
        raise RuntimeError(
            "%s stopped too early (%d ms, expected about %d ms)"
            % (label, elapsed_ms, expected_ms)
        )

DTC_LOOP = make_triangle_table(128, AMP)
DTC_ONE_SHOT = make_tone_burst(256, 8, AMP)
DMAC_ONE_SHOT = make_noise_burst(2048, AMP)

def run():
    print("DAC timed sound test on", DAC_PIN)
    print("1/3 DTC circular")
    print("2/3 DTC one-shot")
    print("3/3 DMAC one-shot")

    dac = DAC(Pin(DAC_PIN))
    dac.write(MID)

    try:
        dac.write_timed(
            DTC_LOOP,
            440 * len(DTC_LOOP),
            mode=DAC.CIRCULAR,
            transfer=DAC.TRANSFER_DTC,
        )
        time.sleep_ms(600)
        dac.stop()
        dac.write(MID)
        print("PASS: DTC circular")
        time.sleep_ms(200)

        sample_rate = 8000
        dac.write_timed(
            DTC_ONE_SHOT,
            sample_rate,
            mode=DAC.NORMAL,
            transfer=DAC.TRANSFER_DTC,
        )
        elapsed = wait_until_idle(dac)
        expect_duration("DTC one-shot", elapsed, len(DTC_ONE_SHOT), sample_rate)
        dac.write(MID)
        print("PASS: DTC one-shot")
        time.sleep_ms(200)

        sample_rate = 12000
        dac.write_timed(
            DMAC_ONE_SHOT,
            sample_rate,
            mode=DAC.NORMAL,
            transfer=DAC.TRANSFER_DMAC,
        )
        elapsed = wait_until_idle(dac)
        expect_duration("DMAC one-shot", elapsed, len(DMAC_ONE_SHOT), sample_rate)
        dac.write(MID)
        print("PASS: DMAC one-shot")

        print("ALL PASS")
    finally:
        try:
            dac.stop()
        except Exception:
            pass
        try:
            dac.write(MID)
        except Exception:
            pass

run()
