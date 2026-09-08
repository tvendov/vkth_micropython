# GEN transition smoothing — 2026-09-09

Scope: the next recorded GEN task after `gen-levels-soak-20260908`.
Keep raw DDS and RX detector unchanged. Add a TX-only amplitude envelope,
without clearing CLIP counters or resetting live modulator history. A new
TX owner/mode is still a new stream; phase continuity across modes is not claimed.

## Evidence chain

| Stage | Evidence | Result |
| --- | --- | --- |
| Raw reference | `raw-reference-host.log` | Expected FAIL: AM0/FM168/USB1312/LSB1312 clips |
| New actual C core | `core-host.log` | 18 groups/8403968 core checks; tone and new transition vectors PASS |
| Scope/sample contract | `af-host.log` | Actual extracted FILE/GEN C callbacks;3524 checks PASS |
| Python-driven regressions | `python-host.log` | All13 suites PASS; mocks/extracted C, not hardware |
| Build | `build.sh`, `build.log` | Shared RadioOnly -j16 PASS; existing RWX warning retained |
| Upload | `deployment.json`, `deploy-host.log` | Internal firmware readback PASS; full16MiB QSPI unchanged |
| GEN levels/shapes | `levels-host.log`, `levels-ram.py` | 20 level cases +36 frequency/wave cases; cumulative CLIP0 |
| Lifecycle | `cycles-host.log`, `cycles-ram.py` | 8 RX/TX cycles,102542ms; TX clip/deadline/FILE UND0 |
| RX monitor | `rx-host.log`, `rx-native-*`, `rx-gui-*` | Native injection + separate VERIFY GUI callbacks PASS |
| Production restore | `production-host.log`, `production-ram.py` | TX AM579900Hz GEN1kHz/50%, LEVEL40, depth50; AF advancing |

The raw-reference command deliberately selects `GEN_SMOOTH=0` in
`tests/tx/test_gen_transitions.c`. The test executable returns failure at
`total == 0`; this proves the tested counterexample, not a failed new firmware.
Both implementations use the same real modulators and24 phase offsets ×14 stages.
New mode totals are all0. Full-scale1kHz square is a negative control: real
filter overload remains (FM1851; USB/LSB1799); no all-waveform overload cure claimed.

The envelope changes full-scale amplitude over20ms, rounded up to a sample.
Frequency/wave changes fade out, switch at zero emitted amplitude, and fade in:
40ms plus up to two sample periods. Requests preserve the running phase; repeat
requests do not restart the fade. Settled samples match the raw DDS exactly.
The header comment and host PASS wording were clarified after build to state
sample rounding; no executable production C behavior changed after the build.

GEN is an audio replacement, not yet a repeater signalling tone mixed with voice.
No physical DAC voltage/phase, RF modulation quality or touch gesture is proved.

## Settings and measured bounds

Level test: sine1000Hz, GEN0/25/50/75/100%, gain100, TX LEVEL40,
AM depth50, FM deviation2500. AM I spans0/102/204/306/408 codes,
Q2048; USB/LSB I spans0/408/818/1228/1636. FM level controls deviation,
not an I/Q amplitude shrink. DADR0/1 status fields are read nonatomically:
do not derive quadrature phase or simultaneous I²+Q² from them.

Shape test: AM/USB/LSB/FM ×1000/1750/500Hz ×SQUARE/TRIANGLE/SINE at50%.
All56 cases have cumulative CLIP0. `adc_rails` can count synthetic4095 in FM;
GEN does not open an ADC. It is not a physical ADC saturation measurement.
Max measured DSP callback cycles/budget: AM592/5000, USB/LSB4428/10000,
FM1114/5000. These are high-water observations, not full ISR timing or a
controlled benchmark of smoothing overhead.

RX/TX cycles use real GUI callbacks, HOME/BACKEND/keypad, GEN50%, all4 modes.
RX free66848..66960 bytes, Si5351 CLK1 TXx1/RX LOx4 register readback PASS.
This102.542s repeat does not replace the earlier204.653s/16-cycle result,
and neither is a long MIC/FILE or physical-touch soak.

RX native monitor checks AM/USB/LSB/FM1kHz, CW700Hz, FM100Hz, wrong tone,
silence, OFF/BYP, stop/start. Settled mode audio/ring underruns0.
AM DSP window102691→122393cycles:32%OFF→38%ON; not zero CPU cost.
The small GUI test checks1750TONE→1000WAIT→OFF→BACK. With its RAM harness,
HOME free75872, VERIFY7920, after BACK74624 bytes. The native and GUI suites
are separate cold runs; do not add their heap usage together.

## Firmware and preservation

- Target: J-Link1120000058 / COM25, no rediscovery or second board.
- Previous firmware1594288B SHA256:
  `ef22e94ba86b22463fe6d0d3ed0c7545e88e476a7e6e8a8ca7745752a313958d`.
- New firmware1594536B (+248), SHA256:
  `692fa2c29ed92d5a16d005c6f3669ba6a4ea18603f3731a572236b4be41181a3`.
- Build label `911b6c1f92-dirty` predates this commit. ELF text1594549,
  data0, BSS649912 (+16); raw generator12B, smooth state28B, detector80B.
- Heap281600B unchanged; no CV2/LAB retained; no new audio/pixel buffer.
- Full local backup:
  `C:\Users\teodor\Desktop\stem\sdr\SDR_TRANCEIVER_RA6M3\backups\gen-smooth-1120000058-20260909-004135`.
  Large binary/ELF/map/QSPI backups are intentionally outside Git.
- All16MiB QSPI byte-identical immediately before/after internal firmware
  upload, preserving76 files. No application, MPY, IQ record or test was written.
- Before final production start, source and MPY readback hashes still matched:
  source326295B `f6bee007fede2dcd3d7e5673005791a8f7103eb6a228fc19645fac5343142467`;
  MPY92388B `d6cc332a76cf4377f38e123e590563a2efa5ea56c87a115aac67bacd25e45b98`.
- Captured dataflash buffers are512B, but SDR1 ends at368B. Written bytes
  are identical before/after each levels/cycles suite. Differences after368B
  are outside the written record and are not claimed as setting changes.
- Tests were RAM-only. Whole source echo/SHA was checked before RX starts.
  Each isolated suite ended with J-Link NORMAL reset. Final production start
  restores normal saving and deliberately has no reset afterwards.

## Test provenance and failures

`initial-cli-failure.log`: omitted required `--cc`; host CLI only.
`initial-af-stub-failure.log`: stale test stub missed DWT/two-argument callback
and software-source helper. Fixed only the host fixture, then reran all13 suites.
Neither host failure touched/reset the board. They are preserved, not erased
from the evidence history.

`*-ram.py` are complete source payloads whose hashes appear in the host logs.
They are evidence, not boot files. Never copy them to board flash.
`host-*.py` and `production-start-host.py` preserve the workstation orchestration;
their helper imports refer to the existing scratch workspace
`C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2`.
The earlier level/cycle base is archived under
`../gen-levels-soak-20260908/host-level-soak.py`. Running a host deployment
wrapper requires the exact known-board backup pointers and explicit hardware
scope; these files are not a generic autodetect/flashing utility.

Only logs have trailing whitespace/terminal NUL padding normalized for Git;
RAM payload source is byte-preserved. Original logs remain in the scratch
workspace and full backup. `verify_evidence.py` is safe offline: verifies
payload SHA/syntax, records, row counts and claims; never opens COM/J-Link.

Portable host rerun (from firmware checkout):

```powershell
& 'C:\Users\teodor\AppData\Local\Programs\Python\Python310\python.exe' -E `
  ports/renesas-ra/tests/tx/run_host_tests.py --cc C:/msys_64/mingw64/bin/gcc.exe
& 'C:\Users\teodor\AppData\Local\Programs\Python\Python310\python.exe' -E `
  ports/renesas-ra/tests/tx/test_tx_af.py --cc C:/msys_64/mingw64/bin/gcc.exe
```
