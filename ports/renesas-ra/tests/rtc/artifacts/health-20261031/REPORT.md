# RA4M2 RTC Health, 2026-10-31

Requested scope: report RTC failure through Python APIs, retain crystal-optional
startup, build the standard build-VK_RA4M2/firmware.hex, do not flash.

Runtime availability now checks START/calendar mode and bounded R64CNT progress.
A failure is latched as EIO until explicit clock-source selection or startup
reinitialization. No silent runtime fallback to LOCO is introduced. Existing
cold-start LOCO fallback and explicit SOSC selection failure/recovery are retained.
time.time(), localtime() without an argument, time_ns(), and RTC calendar access
now reject an unavailable clock; failed consistent snapshots raise EAGAIN.
time_ns uses one date/time snapshot rather than separate reads across midnight.
Internal filesystem timestamp handling is unchanged so boot/file access is not
made dependent on a newly introduced Python exception there.

Each availability check waits for a counter tick, with a 50 ms polling budget.
This is on-access detection, not a background monitor or a crystal accuracy test.
Interrupts can extend wall time; these accesses are not latency-critical IRQ APIs.

## Validation

- Actual driver model: 29 scenarios passed.
- Actual binding model: 16 exception/reentry/source scenarios passed.
- Three invalid FSP configurations rejected; integration source contracts passed.
- Existing I2C suites: 20, 62 and 14 scenarios passed.
- Clean default VK_RA4M2 build passed; unchanged board feature defaults.
- Source base: 23f5607b7 plus this patch; build identifies itself as dirty.
- ELF: text 394872, data 0, bss 128940 bytes.
- Retained symbols: ra_rtc_is_running 0x0002fdf8,
  rtc_check_available 0x0003e21c, mp_hal_time_ns 0x0003e688.
- HEX: 1110746 bytes; generated 2026-10-31.
- HEX SHA256: e4e8724ec54b3acd7c1e85781836af2afb80af09b64a1f687e31f69c4bd49934.
- BIN SHA256: 823d09cd202976811ea92cef094383a10cbba42b7e71785070eb70fe63464563.
- objcopy HEX-to-BIN conversion matches the original BIN SHA256 exactly.
- Linker warning: a LOAD segment has RWX permissions; not changed in this task.
- No COM access, reset, flashing or physical oscillator test performed.

Build output: ports/renesas-ra/build-VK_RA4M2/firmware.hex.
Previous firmware is preserved under previous-firmware next to this report.
The build script and logs are stored here for reproduction; mpy-cross is the
existing build-i2c-20260923 executable. FSP optional-clock patches already present
in the checkout are required, as described in the parent README.
