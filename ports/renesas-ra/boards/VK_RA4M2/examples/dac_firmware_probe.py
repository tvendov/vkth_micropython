from array import array
from machine import DAC, Pin
import time


DAC_PIN = "P014"  # J19-5 / A4 / DA0
ALT_DAC_PIN = "P015"  # J19-6 / A5 / DA1
MID = 2048
AMP = 1200


def clamp12(value):
    if value < 0:
        return 0
    if value > 4095:
        return 4095
    return value


def make_triangle(length):
    buf = array("H", [MID] * length)
    top = clamp12(MID + AMP)
    bottom = clamp12(MID - AMP)
    half = length // 2
    for i in range(length):
        if i < half:
            buf[i] = bottom + ((top - bottom) * i) // max(1, half - 1)
        else:
            j = i - half
            buf[i] = top - ((top - bottom) * j) // max(1, half - 1)
    return buf


def make_ramp(length):
    buf = array("H", [MID] * length)
    start = clamp12(MID - AMP)
    span = clamp12(MID + AMP) - start
    for i in range(length):
        buf[i] = start + (span * i) // max(1, length - 1)
    return buf


DTC_LOOP = make_triangle(128)
DTC_ONESHOT = make_triangle(192)
DMAC_ONESHOT = make_ramp(2048)


def wait_until_idle(dac, timeout_ms=2000):
    start = time.ticks_ms()
    deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
    while dac.playing():
        if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
            raise RuntimeError("timeout waiting for timed DAC to stop")
        time.sleep_ms(10)
    return time.ticks_diff(time.ticks_ms(), start)


def expect_playback_time(label, elapsed_ms, sample_count, freq):
    expected_ms = max(1, (sample_count * 1000) // freq)
    min_ms = max(5, (expected_ms * 3) // 4)
    if elapsed_ms < min_ms:
        raise RuntimeError(
            "{} stopped too early ({} ms, expected about {} ms)".format(
                label,
                elapsed_ms,
                expected_ms,
            )
        )


def pass_line(label):
    print("PASS:", label)


def run():
    print("Timed DAC firmware probe")
    print("J16 is boot-mode jumper, not DAC output.")
    print("Use {} or {}.".format(DAC_PIN, ALT_DAC_PIN))
    dac = DAC(Pin(DAC_PIN))
    dac.write(MID)

    try:
        print("1/3 DTC circular on {} (J19-5 / A4 / DA0)".format(DAC_PIN))
        dac.write_timed(
            DTC_LOOP,
            440 * len(DTC_LOOP),
            mode=DAC.CIRCULAR,
            transfer=DAC.TRANSFER_DTC,
        )
        time.sleep_ms(400)
        dac.stop()
        dac.write(MID)
        pass_line("DTC circular")

        print("2/3 DTC one-shot")
        dac.write_timed(
            DTC_ONESHOT,
            12000,
            mode=DAC.NORMAL,
            transfer=DAC.TRANSFER_DTC,
        )
        elapsed_ms = wait_until_idle(dac)
        expect_playback_time("DTC one-shot", elapsed_ms, len(DTC_ONESHOT), 12000)
        dac.write(MID)
        pass_line("DTC one-shot")

        print("3/3 DMAC one-shot")
        dac.write_timed(
            DMAC_ONESHOT,
            12000,
            mode=DAC.NORMAL,
            transfer=DAC.TRANSFER_DMAC,
        )
        elapsed_ms = wait_until_idle(dac)
        expect_playback_time("DMAC one-shot", elapsed_ms, len(DMAC_ONESHOT), 12000)
        dac.write(MID)
        pass_line("DMAC one-shot")

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
