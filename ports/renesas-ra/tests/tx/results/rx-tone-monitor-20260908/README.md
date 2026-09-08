# Passive RX tone monitor and lazy keypad — 2026-09-08

Target: VK_RA6M3, J-Link1120000058 / COM25. No tests installed on flash.
This is selected-tone recognition, not a CTCSS/DTMF/DCS protocol decoder.

## Evidence map

- `native-host.log`: rebuilt actual TX core and tone C sources with GCC,
  C11/O2/Wall/Wextra/Werror/pedantic; 18 core groups / 8403968 checks,
  tone DDS/detector/reset/weak-input vectors PASS. Run with explicit native
  Python: `tests/tx/run_host_tests.py --cc C:/msys_64/mingw64/bin/gcc.exe`.
- `python-host.log`: eight actual-method/source-contract Python suites PASS,
  including partial keypad allocation failure and release of callbacks/timer.
- `radioonly-build.log`: shared build-VK_RA6M3, -j16, explicit MinGW Python,
  MICROPY_PY_CV2_QSPI=0, USER_C_MODULES empty. Existing RWX segment warning.
- `native-pass-initial-verify-failure.log`: RX native injection vectors PASS,
  then real VERIFY MemoryError allocating116B. Do not label the entire run PASS.
- `isolated-verify-memoryerror.log`: separate small GUI test also failed,
  allocating136B. This is distinct from UART source-transfer corruption.
- `gui-pass-keypad-transfer-failure.log`: lazy app passes VERIFY/TONE/WAIT/OFF,
  then UART loses letters in `digest` before keypad execution. The keypad test
  was NOT executed in this run. Echo retries alone did not fix the UART.
- `rx-monitor-gui-small.log`: whole script uploaded/hashed before RX start;
  actual button callbacks for VERIFY, selected1750Hz TONE,1000Hz WAIT,OFF,BACK PASS.
- `lazy-keypad-hil.log`: separate fresh RAM suite; four open/close iterations,
  BS/cancel/unchanged-frequency OK and VERIFY reopen PASS. Free75536B after
  each closed keypad; not a claim of long-run leak freedom.
- `generator-regression.log`: corrected TX GEN suite repeated on final app;
  USB/LSB/AM/FM, frequency/wave/level menus, advancing AF, Si5351 CLK1 TX x1,
  zero reported DSP clips/deadline misses/FILE underruns. FM mute residual
  0.01891231Hz at deviation2500/gain100; threshold0.1Hz is not an all-settings bound.
- `firmware-deployment.json`: RX monitor firmware and initial app,72 prior
  files preserved; dataflash and external QSPI code unchanged.
- `deployment.json`: final lazy-keypad app-only update;74 prior files preserved,
  internal firmware unchanged. Source and MPY SHA readback included.
- `production-gen.log`: final TX AM579900Hz, GEN SINE1000Hz/50%, TX LEVEL40,
  AM depth50, scope AF GEN and advancing AF. Normal save callback restored;
  no reset after this intentional production start.

Each isolated HIL suite ended with a J-Link NORMAL reset; the full reset
transcripts and host runners remain in the working directory. All script
preparation/echo/hash checks occurred in RAM. Physical touch, DAC voltages,
RF spectrum, protocol tolerances and repeater operation are NOT VERIFIED.

## Resource measurements and archives

AM detector OFF/ON window averages102705/122585cycles, budget320000cycles,
display32%/38%. No new sample/pixel buffer; ARM detector80B plus4B explicit
control state. Overall BSS649800→649896B (+96B); text+1632B; heap281600B unchanged.
Do not equate fixed heap size to free application memory: VERIFY still consumes
most of it (7920B free with the complete RAM GUI harness present).

Full external16MiB images, internal2MiB snapshot and validated dataflash record:

- `C:/Users/teodor/Desktop/stem/sdr/SDR_TRANCEIVER_RA6M3/backups/rx-tone-monitor-1120000058-20260908-211735`
- `C:/Users/teodor/Desktop/stem/sdr/SDR_TRANCEIVER_RA6M3/backups/rx-tone-lazy-keypad-1120000058-20260908-214059`

Canonical app and done/next/protocol documents remain in SDR_TRANCEIVER_RA6M3;
tracked copies are under boards/VK_RA6M3/examples and examples/project-status.
