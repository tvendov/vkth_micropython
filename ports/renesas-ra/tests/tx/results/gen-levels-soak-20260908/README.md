# GEN levels and RX/TX lifecycle: 2026-09-08

Target: VK_RA6M3, J-Link **1120000058**, UART **COM25**. No other probe used.
Installed source corresponds to commit `3eced7055`; the binary retains its
pre-commit build label `2fa6d06ebd-dirty`. **No firmware or application change
and no flash upload in this iteration.** Board-side scenarios ran only in RAM.

## Results

Fixture: RF579900 Hz, GEN SINE1000 Hz, TX LEVEL40, AF gain100,
AM depth50%, FM deviation2500 Hz. Levels0/25/50/75/100% in each mode.

| Mode | Independent I code span at five GEN levels | Q | New steady CLIP |
| --- | --- | --- | --- |
| AM | 0, 102, 204, 306, 408 | Constant2048 | 0 |
| USB | 0, 408, 818, 1228, 1636 | Varying | 0 |
| LSB | 0, 408, 818, 1228, 1636 | Varying | 0 |
| FM | Not a proportional envelope test | Varying | 0 |

Each point waits1s, then takes192 status reads with137us sleeps. Raw AF,
I and Q extrema are collected **independently**. `ra_tx_hw_get_status()`
explicitly does not provide an atomic I/Q sample pair. These results do not
measure analog voltage, quadrature, image rejection or I²+Q².

Transient limitations are retained, not reset away:

- USB/LSB each start at100% with10 CLIP. Moving to0 adds10 each.
- Moving75→100 adds7 for USB and6 for LSB in the refined run.
- FM begins with5 CLIP; subsequent sampled steady intervals add0.
- The next mode inherits100% from the preceding mode in this level sweep.
- SSB limits after its FIR/gain; FM limits after DC/filter/gain. Both count
  these events before output-level scaling (`ra_tx_core_ssb_sample`,
  `ra_tx_core_fm_sample`). They need not reach DAC endpoints.
- FM `adc_rails` also counts GEN's digital4095 input; `gen_source=True`,
  `mic_pin=None`. This is not physical ADC overdrive evidence.

Lifecycle:16 cycles, rotating AM/USB/LSB/FM, GEN50%; TX BACKEND/GEN settings,
TX keypad/cancel, RX restore, RX keypad/cancel, TX restart.
`SOAK_PASS MS204653 WARM_RX_FREE66848 66976`:128-byte range after the first
four cycles, no monotonically falling trend. TX CLIP/deadline/FILE underrun0.
AF and RX block counters advance. Selected CLK1 multisynth register readback
matches TX RFx1 and RX LOx4; this is not a physical clock measurement.
Physical touch, MIC/FILE and hours-long stability are not covered.

## Test correction and failure preservation

The initial level scenario passed its digital assertions, then the host failed
with `Settings changed` from comparing all512 bytes of dataflash. Its original
two512-byte snapshots were not saved, so their exact mismatch is unrecoverable.
Do not interpret this as a demonstrated firmware flash-writing defect.

The follow-up read validates the SDR1 length and matches the written368-byte
record against the previous deployment archive. Refined levels/cycles save
both halted-core512-byte snapshots and compare only that written record.
All refined-run differing offsets lie after its end. No dataflash writes or
register repair were performed. The dataflash contents are distinct from the
external QSPI filesystem; this check does not claim a full QSPI readback.

Each isolated suite and the record-check diagnostic ends with a J-Link NORMAL
reset. The final production launch is left running, without a reset afterward.

## Artifacts

Text log line endings/trailing whitespace are normalized for Git; event text
is retained. Original host logs remain in the scratch paths printed by each
`RUN` header. Device scenarios retain their verified source text.

- `levels-initial-host.log`, `levels-initial-ram.py`: first level run, including
  the failed512-byte comparison. No original before/after snapshots exist.
- `record-check-host.log`, `record-check-1.bin`, `record-check-2.bin`: subsequent
  read-only check, not substitutes for the missing initial snapshots.
- `levels-host.log`, `levels-ram.py`, `levels-before.json`,
  `levels-dataflash-before.bin`, `levels-dataflash-after.bin`: refined run.
- `cycles-host.log`, `cycles-ram.py`, `cycles-before.json`,
  `cycles-dataflash-before.bin`, `cycles-dataflash-after.bin`: lifecycle run.
- `production-host.log`, `production-ram.py`: final installed-file verification
  and production start; normal save callback restored.
- `host-*.py`: orchestration snapshots from the workspace, with their existing
  host-helper dependencies. They are provenance, not self-contained runners.
  The generated `*-ram.py` files contain the exact complete device scenarios.
- `verify_evidence.py`, `artifact-audit.log`: standalone offline consistency
  check PASS for uploaded-script SHAs/syntax, stored records and reported
  rows. Run with Python3; it does not connect to hardware or repeat HIL.

**Do not save these scenarios to board flash or paste them into an already
running SDR session.** The host verifies the installed binary, performs the
known isolated boot, uploads the entire RAM scenario with echo/SHA checking
before starting RX, and resets after each isolated suite. A REPL prompt alone
does not establish that the background application is stopped.

Host runtime: `C:\Users\teodor\AppData\Local\Programs\Python\Python310\python.exe -E`.
Scratch commands: `tx_gen_level_soak_20260908.py levels`, then `cycles`, followed
by `start_gen_after_soak_20260908.py`. Build/host C tests not rerun: this
iteration changes tests and documentation only.

## Final production state and readback

TX AM579900 Hz, GEN SINE1000 Hz/50%, TX LEVEL40, depth50. AF advances;
final status AF51/DSP samples50101, outputs enabled, error/CLIP/deadline/UND0.
No analog/DAC/RF measurement is claimed.

- Firmware1594288B matched the archived binary byte-for-byte before launch;
  archive SHA256 `ef22e94ba86b22463fe6d0d3ed0c7545e88e476a7e6e8a8ca7745752a313958d`.
- `/flash/sdr_single.py.source` readback SHA256
  `f6bee007fede2dcd3d7e5673005791a8f7103eb6a228fc19645fac5343142467`.
- `/flash/sdr_single.mpy` readback SHA256
  `d6cc332a76cf4377f38e123e590563a2efa5ea56c87a115aac67bacd25e45b98`.

Next: bounded GEN start/level transition smoothing with retained phase/history,
then separate repeater-tone mixing with voice and RX detection under speech.
The present GEN substitutes for MIC; passive RX TONE MON is not full CTCSS,
DTMF, DCS, squelch gating or repeater certification.
