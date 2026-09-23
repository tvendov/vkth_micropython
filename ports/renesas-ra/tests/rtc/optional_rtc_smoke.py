"""Manual RA4M2 smoke test. Run on an idle board after an approved flash.

Call run(True) for a populated crystal, run(False) for an unpopulated board.
This script does not reset the board or change its calendar deliberately.
"""
import machine
import os
import time


def run(has_crystal):
    print(os.uname())  # Record the actual firmware running on this board.
    rtc = machine.RTC()
    rtc.wakeup(None)  # This test owns RTC; remove the previous periodic callback.
    rtc = machine.RTC(source="loco")
    print("before", rtc.source(), rtc.datetime())
    start = time.ticks_ms()
    try:
        rtc = machine.RTC(source="sosc")
    except OSError as error:
        print("SOSC error", error, "elapsed_ms", time.ticks_diff(time.ticks_ms(), start))
        if has_crystal:
            raise
        assert machine.RTC().source() == "loco"
    else:
        assert has_crystal, "Unexpected external-clock acceptance"
        assert rtc.source() == "sosc"
        print("SOSC selected", time.ticks_diff(time.ticks_ms(), start))
    before = rtc.datetime()
    time.sleep_ms(1200)
    after = rtc.datetime()
    assert before != after, "RTC is not counting"
    print("after", rtc.source(), after)
    print("RTC smoke complete; reset, AGT, sleep and electrical tests are separate")
