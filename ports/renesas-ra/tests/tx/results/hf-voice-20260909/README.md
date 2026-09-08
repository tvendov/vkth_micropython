# HF MIC / FILE validation — 2026-09-09

Scope: execute the user's HF AM/USB/LSB plan after deferring repeater
signalling. No production source changes, build or firmware/filesystem writes.
The board ran complete echo/SHA-checked RAM scripts. Each isolated suite,
including the failed first soak, ended with a J-Link NORMAL reset.

Target: J-Link **1120000058 / COM25**. The other board was not used.
Installed firmware was compared byte-for-byte with the archived candidate:
1594536 B, SHA256
`692fa2c29ed92d5a16d005c6f3669ba6a4ea18603f3731a572236b4be41181a3`.
This is the preceding GEN-smoothing firmware, label `911b6c1f92-dirty`;
it is not a new build of this evidence-only commit.

## 1. Controls: six combinations passed the stated digital assertions

Temporary RF setting: 3500000 Hz; TX LEVEL40, audio gain100, AM depth50.
The existing R:AM/R:USB/R:LSB recordings were used, not overwritten.
The microphone's external stimulus was **not confirmed**. MIC means the
physical ADC path was enabled, not that a known clean tone was present.

`controls-ram.py`, `controls-hil.log` and `controls-host.log` record:

- Real LVGL callbacks for source/mode, BACKEND, BACK and LOOP; this is not
  a physical touch test. LOOP OFF→ON keeps the same file/TX owner.
- Scope AF frames and DSP samples advance for MIC and FILE. Scope OFF
  stops capture but not the source/modulator; TIME resumes capture.
- LEVEL0/20/40: expected native amplitude values, including both DAC
  codes2048 at LEVEL0. Gain80→100 and AM depth75→50 take effect without
  replacing the owner. AM Q remains2048.
- Si5351 CLK1 multisynth readback agrees with TX x1 / RX LO x4 settings.
- All six cases: no CLIP, unexpected callback or DSP deadline miss;
  no new FILE UND in the measured steady5s interval after each action group.
- RX restores after each mode, blocks advance, free heap62112 B after GC
  with this larger RAM harness still present.

| Mode / source | AF frames / steady5s | Cumulative UND before steady interval | New UND / steady5s | DSP max / budget cycles |
| --- | ---: | ---: | ---: | ---: |
| AM / MIC | 120 | 0 | 0 | 301 / 2728 |
| AM / R:AM | 120 | 8130 | 0 | 253 / 5000 |
| USB / MIC | 85 | 0 | 0 | 4183 / 10000 |
| USB / R:USB | 109 | 6916 | 0 | 4176 / 10000 |
| LSB / MIC | 85 | 0 | 0 | 4183 / 10000 |
| LSB / R:LSB | 109 | 6816 | 0 | 4176 / 10000 |

UND is a count of starved file sample requests, not a count of whole UI
hitches. These cumulative values include the preceding actions; the test
does not isolate which menu/control caused each event. Therefore this is
**not interruption-free FILE playback**. The scope callbacks and differently
configured sample rates also make these rows unsuitable as a clean CPU benchmark.

## 2. Initial soak failed: retain the evidence

`soak-ram.py`, `soak-partial.log`, `soak-error.txt`, `soak-host.log`:
lap0 AM/MIC passed. At lap1 USB/R:USB, following RX→TX, CLK1 readback failed:

```text
expected: ff ff 00 70 4d fd ff cd
read:     00 ff 00 f0 00 00 00 00
```

This proves a readback discrepancy, **not its cause or the physical output
frequency**. There was no repeated read captured at that failure. It is
UNKNOWN whether the programming, I2C readback or another mechanism was wrong.
The script failed and the board was reset; it was not labelled PASS.

`clock-ram.py` then added RAM-only write tracing and three repeated reads
per checkpoint. Six mode/source/RX↔TX cycles gave72 matching reads and27
recorded writes; `CLOCK_PROBE_BAD_READS 0`. No corrective write/retry was
added. Its extra logging/readbacks change timing, so this does not fix or
disprove the first failure.

## 3. Uninstrumented soak repeat passed its assertions

`soak-repeat1-ram.py` is **byte-identical** to the failed `soak-ram.py`:
Uploaded LF-string SHA256
`d4ec92f054ea8483552b79be609dcf5ef9b4334afe3fc13e307a11ee5caef99e`.
The original evidence was preserved under separate names.

- 12 RX↔TX cycles in193585 ms; each mode/source pair is exercised twice.
- BACKEND→BACK and frequency keypad→CANCEL on each cycle.
- Each10s steady interval: AF advances, new UND0, CLIP0, deadline0;
  no MemoryError or unexpected callback assertion failure.
- The six FILE cases completed6..7 recording loops and retained their owner.
- Before the steady intervals FILE UND was11247..14820 across the action
  groups. This remains open, despite the steady-interval PASS.
- RX free heap after the first three cycles:66480..66640 B. A short,
  allocation-bearing RAM harness is not proof of leak freedom or of the
  production application's total available heap.
- TX and RX Si5351 assertions passed in this repeat. The earlier isolated
  readback failure remains unresolved, not silently removed from the result.

## 4. State and preservation

For controls, diagnostic and successful repeat, the valid368 B SDR1
settings record is byte-identical before/after. Captures are512 B; bytes
beyond the valid record are not part of the preservation comparison.
There is only a before-capture for the failed first soak.

The production launch verifies the installed source/MPY SHA and firmware
readback, then restores the **actual** `before.json` state, not a stale
deployment snapshot:

```text
TX AM579900 Hz; SOURCE GEN; SINE1000 Hz, GEN50%; TX LEVEL40; depth50
CLK1 TX x1 readback matches; AF advances; CLIP/deadline/UND/error0
Normal settings-save callback resumed; no reset after production start
```

The physical DAC/RF waveform was not measured. Source/MPY unchanged:

- source: `f6bee007fede2dcd3d7e5673005791a8f7103eb6a228fc19645fac5343142467`
- MPY: `d6cc332a76cf4377f38e123e590563a2efa5ea56c87a115aac67bacd25e45b98`

Full original logs: canonical project
`backups/hf-voice-1120000058-20260909-011714`.
Archived log line endings/encoding and trailing prompt whitespace were
normalized here; RAM payload
files and binary captures are unchanged. Windows stored RAM source as CRLF;
the verified upload uses the original LF string. The offline audit explicitly
normalizes source line endings when matching the uploaded SHA.
`host-hf-voice.py` and
`host-production.py` record the orchestration used; their imports/pointers
refer to existing local deployment helpers, not a standalone portable tool.
The exact device-side programs are the `*-ram.py` files.

## Open acceptance / next steps

1. If Si5351 mismatch recurs: preserve the first read, capture repeated and
   per-register reads **before reprogramming**, along with desired mode,
   channel and last write. Separate read corruption from persistent wrong
   programming; do not mask failures with silent retries.
2. Physical two-channel scope: AM envelope/Q DC, USB/LSB I/Q amplitude and
   phase; then unwanted-sideband rejection after the external modulator.
   These require an instrument/operator; register snapshots do not substitute.
3. FILE transition UND remains an explicit limitation. The user allowed
   brief menu-transition deviations and deferred UI performance work; no
   unrequested UI or repeater redesign was made here.

Run `python -E verify_evidence.py` in a working host Python to audit this
archive offline. This does not access, reset or flash a board.
