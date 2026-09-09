# TX audio-band / PWR local implementation — 2026-09-09

Scope: MIC, decoded FILE and GEN into common configurable AF conditioning for
AM/USB/LSB. Existing amplitude control is labelled PWR drive (not watts).
No board, COM, J-Link, reset, flash write, ADC/DAC electrical or RF test.

## Evidence

- `python-summary.log`: 14 host scripts, zero failures; individual logs retain
  the final results. Actual UI methods are extracted from the production source.
- `core-host.log`: core 18 groups / 8,403,968 checks; new AF 1,531,595 checks;
  tone and GEN transition regressions PASS. The full-scale square tests still
  report genuine pre-existing overload; zero clips is not universally claimed.
- `test_tx_af.log`: 11,255 checks; actual common sample code and modulators,
  nine MIC/FILE/GEN × AM/USB/LSB cases; scope sees post-filter AF exactly.
- `test_sdr_tx_filter.log`: HOME/BACKEND callbacks, old-record migration,
  separate RX/TX settings, no FILE restart, deferred dataflash save; the largest
  record exercised is 404/506 payload bytes. The dataflash object is a host mock.
- `build.sh`, `build.log`: shared RadioOnly build, native MinGW Python, -j16,
  CV2 QSPI disabled and no user C modules; PASS with the existing RWX warning.
- `artifacts.json`, `size.log`: exact working-tree source/build hashes and ELF
  comparison with the previously deployed GEN-smooth archive. No new deployment.

The 308-byte AF state fits in the existing 1024-byte mutually exclusive CW DSP
workspace. Configuration/status enlarge the TX owner by 12 bytes (1580→1592).
Whole-image BSS remains649912 bytes; g_heap remains281600 bytes at1fff7038.
These are static linkage results, not live GUI memory measurements.

## Reproduction (host only)

Use `C:/Users/teodor/AppData/Local/Programs/Python/Python310/python.exe -E`.
Run `tests/tx/run_host_tests.py --cc C:/msys_64/mingw64/bin/gcc.exe`, then
each `tests/tx/test_*.py`. Four require the same `--cc` argument:
`test_am_mic_dispatch`, `test_audio_native_control`, `test_lcd_capture_resume`,
and `test_tx_af`. Paths here are relative to `ports/renesas-ra`.
Build from repository root with MSYS bash and this directory's `build.sh`.
MPY: `mpy-cross/build/mpy-cross.exe -march=armv7emsp -O2 -s sdr_single.py
-o <candidate.mpy> ports/renesas-ra/boards/VK_RA6M3/examples/sdr_single.py`.

## Not verified

Worst-case ISR time during the two-bank 20-ms crossfade, real MIC CPU capture,
new GUI heap/layout/touch, FILE continuity through menus, physical I/Q and RF
power/bandwidth. These remain next-step acceptance items, not passing tests.
