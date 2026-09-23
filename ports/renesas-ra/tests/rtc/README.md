# Optional RA4M2 subclock

One `BOARD=VK_RA4M2` image supports the populated and unpopulated 32.768 kHz crystal variants. This does not merge different processors into one binary. RA6M3 and RA6M5 remain separate BOARD builds and retain their configured startup source.

## API

```python
import machine

rtc = machine.RTC()             # Keep the current source and calendar.
print(rtc.source())              # "loco" or "sosc"; raises on a known RTC fault.
rtc.wakeup(None)                 # Disable periodic wakeup before changing source.
try:
    rtc = machine.RTC(source="sosc")  # Explicitly request the populated crystal.
except OSError as error:
    print(error)                # ETIMEDOUT on failed selection; do not claim SOSC.
    print(machine.RTC().source())  # "loco" after successful recovery, or EIO.

# Optional low-frequency AGT use, after a successful external-clock selection:
# timer = machine.Timer(1, freq=2, source="sosc")
# Check that this timer channel is free in the application first.
```

Cold startup uses LOCO. A retained running calendar is checked and kept, including an already selected SOSC. VM callbacks are not retained across a new VM startup. Source changes with active RTC interrupts are rejected. AGT cannot select optional SOSC before successful RTC counting verification. Returning RTC to LOCO does not stop an already verified SOSC shared with a timer.

The explicit constructor is synchronous. The configured 1000 ms stabilization interval uses the VM event wait, with exception cleanup and reentry/standby guards. It is not a hard real-time deadline or a nonblocking API. Register handshakes have a finite 2000 x 10 us polling budget each. Counting checks have a finite 500 x 100 us polling budget. Interrupt processing can extend elapsed wall time. These short verification/reconfiguration stages still occupy the caller; they do not have the I2C asynchronous API's no-spin contract.

`SOSTP` is not an oscillator-ready signal. The counter test establishes observed progress, not crystal frequency, ppm accuracy, electrical startup margin or continuous clock-failure detection. The board's stabilization interval requires physical validation for the fitted crystal. Selecting a new clock resets subsecond phase and clock-specific calibration; calendar fields are restored. A failed recovery raises an RTC error rather than supplying a fabricated working time source.

## FSP Dependency

This change includes `lib/fsp/ra/fsp/src/bsp/mcu/all/bsp_clocks.c` in the FSP submodule, not just top-level port files. The runtime-owned startup and RTC-preservation branches are enabled only by `BSP_CLOCK_CFG_SUBCLOCK_RUNTIME` on VK_RA4M2. A stock FSP checkout without that patch is not the tested configuration. Keep the FSP patch alongside the port patch and apply it to the recorded FSP revision when reproducing the build. No upstream submodule commit has been created by this work.

## Tests

In MSYS2 Bash, from the main checkout:

```bash
export PATH=/mingw64/bin:/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra
python3 tests/test_rtc_optional.py
bash tests/rtc/build-candidates.sh
```

Host tests compile the actual driver, selected binding functions and FSP startup branches with vendor register layouts and a simulated clock. They cannot prove behavior on a physical board. `optional_rtc_smoke.py` is a manual HIL entry point, not an executed result.

Hardware acceptance remains separate: both physical crystal variants, reset/power cycles, calendar retention, absent/slow crystal, exception interruption, RTC wakeup, AGT coexistence and sleep. No flash or reset is performed by the build script.
