# VK_RA6M3 done changes

## ADC/PGA for RA6M3

- Added RA6M3-only PGA support for ADC channels AN000..AN002 and AN100..AN102.
- Added PGA register control in `ports/renesas-ra/ra/ra_adc.c`:
  - `RA_ADC_PGA_OFF`
  - `RA_ADC_PGA_BYPASS`
  - `RA_ADC_PGA_SINGLE`
  - `RA_ADC_PGA_DIFFERENTIAL`
- Added gain tables and nominal gain reporting:
  - single-ended gain codes from x2.000 to x13.333
  - differential gain codes x1.500, x2.333, x4.000, x5.667
- PGA register writes are done as nibble read-modify-write operations so reserved/reset bits are preserved.
- PGA configuration is rejected while ADC scan is running.

## PGAVSS wiring

- Differential PGA now prepares the real negative input pin automatically:
  - AN000/AN001/AN002 use `P003 / PGAVSS000`
  - AN100/AN101/AN102 use `P007 / PGAVSS100`
- The driver enables analog mode (`PmnPFS.ASEL = 1`) on the proper PGAVSS pin before enabling differential PGA.
- There is no internal grounding of the second PGA input. The board must externally wire PGAVSS to the intended reference.

## machine.ADC Python API

- Added RA6M3 PGA constants to `machine.ADC`.
- Added methods:
  - `adc.pga_supported()`
  - `adc.pga()`
  - `adc.pga("bypass")`
  - `adc.pga("single", ADC.PGA_GAIN_4_000)`
  - `adc.pga("differential", ADC.PGA_DIFF_GAIN_1_500)`
  - `adc.set_gain(...)`
  - `adc.gain()`
- PGA-capable ADC pins default to `RA_ADC_PGA_BYPASS` when the channel was still in `RA_ADC_PGA_OFF`, so normal ADC reads do not leave the special PGA pin path unavailable.
- Added required qstr entries in `ports/renesas-ra/qstrdefsport.h`.

## LCD vsync helper

- Added a GLCDC VSYNC counter in `ports/renesas-ra/boards/VK_RA6M3/machine_lcd.c`.
- Added `lcd.vsync([timeout_ms=20])`.
- The method blocks until the next GLCDC line-detect/VSYNC event or returns `False` on timeout.
- Intended use: gate LVGL/direct-mode rendering to reduce visible flicker during frequent updates.

## ADC1 scan-end vector

- Added `ADC1_SCAN_END` to `ports/renesas-ra/boards/VK_RA6M3/ra_gen/vector_data.h`.
- Increased `VECTOR_DATA_IRQ_COUNT` from 57 to 58.
- Allocated IRQ slot 57:
  - `VECTOR_NUMBER_ADC1_SCAN_END`
  - `ADC1_SCAN_END_IRQn`
- Added slot 57 to `g_vector_table` in `vector_data.c`, using the existing `adc_scan_end_isr`.
- Added slot 57 to `g_interrupt_event_link_select` as `EVENT_ADC1_SCAN_END`.
- Purpose: make ADC1/Q scan-end observable for diagnostics, statistics and possible transfer activation source.
- This does not select the final I/Q transport design. Real I/Q capture should still avoid two independent ADC0/ADC1 transfers and use a synchronized transport path such as DTC chain or DMAC interleaving/offset.

## Build check

Built successfully from MSYS2/UCRT64:

```sh
export PATH=/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra
make BOARD=VK_RA6M3 -j8
```

Resulting artifacts:

- `build-VK_RA6M3/firmware.bin`: 1545168 bytes
- `build-VK_RA6M3/firmware.elf`: 17033728 bytes
- `build-VK_RA6M3/firmware.hex`: 4346315 bytes

## Rebuild after PnDEN and PGAVSS ASEL fixes

Rebuilt successfully from MSYS2/UCRT64 after the current `ra_adc.c` PGA changes.

Included in the rebuilt artifact:

- `PnDEN` handling for the full PGA unit when differential input is selected.
- Rejection of mixed single-ended and differential PGA usage inside one ADC unit.
- PGAVSS analog mode (`PmnPFS.ASEL = 1`) for differential PGA.
- PGAVSS analog mode (`PmnPFS.ASEL = 1`) for single-ended PGA, where the board must wire PGAVSS to AVSS0.
- Previously committed `ADC1_SCAN_END` vector support.

Resulting artifacts from the rebuild:

- `build-VK_RA6M3/firmware.bin`: 1545320 bytes
- `build-VK_RA6M3/firmware.elf`: 17034924 bytes
- `build-VK_RA6M3/firmware.hex`: 4346736 bytes
- timestamp: 2026-08-21 20:31:50

Note: at the time of this rebuild, `ports/renesas-ra/ra/ra_adc.c` still had uncommitted changes.

## Coherent I/Q capture driver (SDR receive path)

Report tag: **SDR-RA6M3-BUILD-20260821-02**.

Files touched (absolute paths):

- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\ra\ra_iq_adc.h` — new.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\ra\ra_iq_adc.c` — new.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\Makefile` — build hook only.

- Added `ra/ra_iq_adc.h` and `ra/ra_iq_adc.c`: single-activation coherent I/Q capture.
- Added the RA6M3-only build hook to `ports/renesas-ra/Makefile`, right after the audioadc
  block:

  ```make
  ifeq ($(CMSIS_MCU),RA6M3)
  CFLAGS += -DMICROPY_HW_ENABLE_IQ_ADC=1
  HAL_SRC_C += ra/ra_iq_adc.c
  endif
  ```

Design as implemented (per ARCH-TRIG-002 / ARCH-ADC-002):

- AGT reserves a channel; its event drives `ELSR8` (ELC_AD00) and `ELSR10` (ELC_AD10)
  simultaneously — one trigger source for both units.
- Both units opened with `ADC_TRIGGER_SYNC_ELC` via copy-and-override of `g_adc0_cfg` /
  `g_adc1_cfg`; the generated files stay untouched.
- S&H enabled on both units, identical `ADSSTR` / `SSTSH`, `SHMD = 1`.
- Transport: one DTC chain from `ADC0_SCAN_END`. Descriptor 0 reads unit-0 `ADDR` into the I
  buffer and chains into descriptor 1, which reads unit-1 into the Q buffer and raises the
  interrupt. One activation, two samples — I/Q skew is structurally impossible.
- `ADC1_SCAN_END` pulls no data. The slot stays NVIC-disabled; `IELSR[57].IR` is read and
  cleared in the block callback as an O(1) liveness check for the Q unit (weak evidence: it
  proves at least one scan completed in the block, not per-sample coherence, but it is the only
  check that does not violate REQ-RT-004).
- The callback rewrites both descriptors' `p_dest` and `length` unconditionally, so the driver
  does not depend on unconfirmed DTC repeat-region reload semantics.

Code review / fix pass before commit:

- Closed the ADC open partial-failure leak: if `R_ADC_ScanCfg()` fails, the already-opened ADC
  unit is closed immediately.
- ADC1 scan-end is kept diagnostic-only: `R_ADC_Open()` can enable any valid scan-end IRQ, so the
  driver now disables and clears `VECTOR_NUMBER_ADC1_SCAN_END` immediately after opening unit 1.
- Fixed DTC repeat length handling after callback retargeting: live descriptors now use the same
  encoded repeat/block `length` format that FSP writes during `R_DTC_Open()` / `R_DTC_Reconfigure()`.
- `ra_iq_adc_start()` now reconfigures the DTC descriptor table before enabling capture and cleans
  up DTC/ADC state if either ADC scan start fails.
- Fixed the `P000 == 0` cleanup bug by tracking whether I/Q ADC pins were enabled, instead of using
  pin number zero as a sentinel.
- `ra_iq_adc_acquire()` now snapshots and clears the ready block under a short ADC0 scan-end IRQ
  critical section, avoiding a race with the DTC callback.

Not done (by the phasing rules): no Python wrapper (phase 5, after the C path is stable); buffers
come out as raw `uint16_t` with no centering (DC removal is phase 3).

Build check — built from MSYS2/UCRT64, `make BOARD=VK_RA6M3 -j8`, exit 0. `ra/ra_iq_adc.c`
compiled and linked cleanly.

- `build-VK_RA6M3/firmware.bin`: 1545320 bytes
- `build-VK_RA6M3/firmware.elf`: 17034924 bytes
- `build-VK_RA6M3/firmware.hex`: 4346736 bytes
- `build-VK_RA6M3/ra/ra_iq_adc.o`: 84896 bytes
- timestamp: 2026-08-21 21:13:49

Important caveat: `firmware.bin` is byte-for-byte identical to the pre-I/Q rebuild above. With
`-ffunction-sections` / `--gc-sections` the entire driver is garbage-collected because nothing
references it yet (no Python wrapper). This build therefore proves the driver **compiles and links
cleanly**, not that its code sits on a reachable path. Stronger evidence follows once the phase-5
wrapper calls into it.

Evidence class: **code review + clean compile only**. Nothing has been executed. First bench check
is exactly the DTC-chain behaviour in repeat/ping-pong mode: does the interrupt arrive once per
block, and does descriptor 1 actually run on every activation.

## Phase-5 Python wrapper: `machine.IQADC`

Report tag: **SDR-RA6M3-BUILD-20260821-03**.

Files touched (absolute paths):

- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\machine_iq_adc.c` — new wrapper.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\modmachine.c` — register `IQADC`.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\qstrdefsport.h` — new qstrs.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\Makefile` — `SRC_C += machine_iq_adc.c`.

Wraps `ra_iq_adc_*` (the coherent I/Q driver above) as one MicroPython peripheral. The ISR/DTC
path stays entirely in C; Python is a polling control-plane consumer that receives an
already-complete block.

Python API:

```python
from machine import IQADC, ADC
from array import array

iq = IQADC(i_pin="P000", q_pin="P004", rate=48000, block=128,
           pga=ADC.PGA_BYPASS, gain=0)

ib = array('H', bytearray(2 * 128))   # pre-allocated ONCE, before start()
qb = array('H', bytearray(2 * 128))

iq.start()
seq = iq.read_block(ib, qb)           # -> int sequence, or None if no block ready
if seq is not None:
    ...                               # ib/qb now hold raw uint16 codes (no centering)
iq.stop()
```

Methods:

- `IQADC(i_pin, q_pin, *, rate=48000, block=128, pga=ADC.PGA_BYPASS, gain=0)` → `ra_iq_adc_init()`.
  `i_pin` must resolve to AN000..AN002, `q_pin` to AN100..AN102 (else `ValueError`);
  `block` in 1..256 (`RA_IQ_ADC_MAX_BLOCK_SAMPLES`); `pga = RA_ADC_PGA_OFF` is rejected.
- `start()` → `ra_iq_adc_start()`; `stop()` → `ra_iq_adc_stop()`; `deinit()` → `ra_iq_adc_deinit()`.
- `read_block(ib, qb)` → `ra_iq_adc_acquire()`, `memcpy` into the two caller `array('H')` buffers,
  returns the block sequence number or `None` when no block is ready. For `block=N` the buffers
  hold `N` samples = `2*N` bytes.
- Zero-allocation getters for the running loop: `blocks()`, `overruns()`, `unit1_stalls()`,
  `last_error()`, `ready()`.
- `status()` → dict (`initialised, running, ready, rate, block, blocks, overruns, unit1_stalls,
  last_error`).

Realtime contract (REQ-RT-002 — no allocation after `start()`):

- Consumer pre-allocates `ib`/`qb` once, before `start()`.
- `read_block()` and the int/bool getters allocate nothing: they return only
  `MP_OBJ_NEW_SMALL_INT(...)` / `mp_const_*`.
- `status()` builds a dict and therefore **allocates** — it is control-plane only and must not be
  called from inside the running capture loop; use the int getters there instead.
- No per-sample and no per-block Python callback in this version; `read_block()` is pure polling.

Code review / fix pass before commit:

- Invalid `pga` and `gain` arguments are rejected as `ValueError` before touching the hardware path.
- Hot-path counter returns are guarded: if `seq`, `blocks`, `overruns` or `unit1_stalls` exceed
  `MP_SMALL_INT_MAX`, the getter raises `OverflowError` instead of returning a malformed small-int
  or allocating a long int.
- `read_block()` now sanity-checks the C driver's returned sample count before copying into the
  caller buffers.

Known limitation: `blocks`/`seq` are `uint32` and eventually exceed `MP_SMALL_INT_MAX`.
At 48 kHz with `block=128` this happens after about 16 days on this port; a sustained-capture
consumer should stop/deinit/recreate the object before then. Acceptable for v1; noted for the
sustained-capture path.

Not done (deferred by phasing): no gain auto-ranging, no DC removal (phase 3), no stream iterator,
no `read_block_into` fast path beyond the current caller-buffer form (it already is zero-alloc).

Build proof — built from MSYS2/UCRT64, `make BOARD=VK_RA6M3 -j8`, exit 0. Unlike the previous
driver-only builds, the wrapper now references `ra_iq_adc_*`, so `--gc-sections` no longer strips
it:

- `arm-none-eabi-nm firmware.elf | grep -Ei 'ra_iq_adc|iqadc'` → `machine_iqadc_type` plus the
  driver/wrapper method symbols (was 0 before the wrapper). The driver
  entry points `ra_iq_adc_init` / `ra_iq_adc_acquire` / `ra_iq_adc_deinit` / `ra_iq_adc_get_status`
  are now global `T` symbols, and the full `machine_iqadc_*` type/method table is present.
- `build-VK_RA6M3/firmware.bin`: 1548640 bytes (was 1545320 — the driver plus wrapper are now
  actually linked in).
- `build-VK_RA6M3/firmware.elf`: 17136636 bytes
- `build-VK_RA6M3/firmware.hex`: 4356080 bytes
- `build-VK_RA6M3/machine_iq_adc.o`: 183468 bytes
- timestamp: 2026-08-21 21:54:01

Evidence class: **code review + clean compile + link/symbol evidence**. This proves the I/Q code
now sits on a reachable path in the firmware; it has still **not** been executed on hardware. The
first runtime check remains the DTC chain in repeat/ping-pong mode, now driveable from the REPL via
`IQADC(...).start()` + `read_block()`.

## First hardware bring-up on VK_RA6M3 (COM18)

Report tag: **SDR-RA6M3-BRINGUP-20260821-01**. Evidence class: **hardware, on target**.

Flashed the wrapper build to the board with J-Link (`R7FA6M3AH`, SWD, `loadbin firmware.bin @ 0x0`,
program & verify O.K., 1572864 bytes) and drove `machine.IQADC` from the REPL over `mpremote`
(COM18). Registers read live with `machine.mem8/16/32`.

Result: `IQADC(...)` and `start()` run without fault (the REPL survives, so no hard fault), but
capture produces **zero blocks** — after 5 s `blocks=0, overruns=0, unit1_stalls=0, ready=0` with
`running=1`.

Register bisection of the AGT -> ELC -> ADC0/ADC1 -> DTC -> callback chain (all values below are
post-`start()`):

- AGT: reserved channel 0 runs. `AGTCR=0x23` -> `TSTART=1, TCSTF=1, TUNDF=1`, counter moving. The
  timer counts and underflows, i.e. it is generating `ELC_EVENT_AGT0_INT` (64).
- ELC: `ELCR=0x80` -> `ELCON=1` (ELC enabled). `ELSR[8]=ELSR[10]=0x040` = `ELC_EVENT_AGT0_INT`, so
  both ADC units are linked to the single AGT event (ARCH-TRIG-002 holds on silicon).
- ADC0/ADC1: `ADCSR=0x0240` -> `TRGE=1, EXTRG=0` (synchronous ELC trigger, armed). `ADSTRGR=0x090A`
  = the exact FSP value for a single ELC group-A trigger (`TRSA=0x09=ADC_ELC_TRIGGER` in bits
  13:8, `TRSB=0x0A` in bits 5:0). `ADANSA0=0x0001` (one channel selected). Configuration is correct.
- Transport, the decisive probe: `IELSR[48]` (ADC0_SCAN_END) = `0x0100004B` -> event 75, `DTCE=1`
  (bit 24), `IR=0`; `IELSR[57]` (ADC1_SCAN_END, NVIC-disabled, not a DTC source) = `0x00010051` ->
  event 81, **`IR=1` latched**. The latched ADC1 scan-end flag proves both ADC units are actually
  being triggered and completing scans, and the cleared ADC0 flag proves the DTC is being activated
  by ADC0_SCAN_END.

Root cause (localized, not yet fixed): the whole trigger path works end to end — AGT underflow ->
ELC -> both ADC units scan -> ADC0_SCAN_END activates the DTC. What never happens is the
**block-boundary completion interrupt reaching the CPU**: `ADC0_SCAN_END.IR` is cleared by the DTC
on every activation and the NVIC `adc_scan_end_isr` (the block callback that increments `blocks`
and flips `ready`) never runs. This is exactly the DTC repeat / ping-pong re-arm behaviour the
driver header flagged as unverified against the manual: in repeat mode the DTC transfers forever
without ever asserting the transfer-complete interrupt at the block boundary. The fix belongs in
the DTC descriptor setup in `ra/ra_iq_adc.c` (mode / transfer count / `DISEL` so the block boundary
raises the interrupt), and is the next step before any I/Q or coherence test.

Not a wrapper bug: `machine.IQADC`, the register-level configuration, the ELC link and the ADC
trigger arming are all correct on hardware. The gap is solely the DTC completion-interrupt
generation in the C driver.

## DTC mode fix — I/Q transport verified on hardware (GREEN)

Report tag: **SDR-RA6M3-BRINGUP-20260821-02**. Evidence class: **hardware, on target**.

Root cause confirmed and fixed in `ra/ra_iq_adc.c`: the DTC chain descriptors were built in
`TRANSFER_MODE_REPEAT`. Per FSP a repeat-mode transfer never "ends", so `TRANSFER_IRQ_END` is
never delivered to the CPU — exactly the `IELSR[48].DTCE=1, IR=0, blocks=0` signature observed
above. Change:

- Both chain descriptors switched from `TRANSFER_MODE_REPEAT` to `TRANSFER_MODE_NORMAL`, so the
  transfer completes after `block_samples` activations and the block-boundary interrupt reaches the
  CPU (runs `ra_iq_block_callback`).
- The block callback now re-arms the chain for the next ping-pong half every block: new `p_dest`,
  raw `length = block_samples`, then `R_DTC_Reconfigure(...)`. Normal-mode DTC self-clears its DTCE
  on completion, so the explicit re-arm is required to resume transport.
- `ra_iq_dtc_repeat_length()` removed: encoded repeat/block length is not used on the normal-mode
  path; the raw sample count is the correct `length`.

Rebuilt (`make BOARD=VK_RA6M3 -j16`, exit 0, `firmware.bin` 1548640 bytes), flashed over J-Link,
and driven from the REPL on COM18:

```
IQADC("P000","P004", rate=48000, block=128, pga=ADC.PGA_BYPASS)
```

Result — capture now runs:

- `blocks` increments; `seq` returned by `read_block()` is monotonic 1,2,3,... with no gaps.
- `overruns = 0`, `unit1_stalls = 0` over the sampled window: the ping-pong re-arm keeps up and the
  ADC1 (Q) scan-end liveness flag latches every block.
- Live data flowing on both channels (I ~3010 codes, Q ~2864 codes on floating/DC pins) — raw 12-bit
  `uint16`, no centering, as designed.

What this proves: the full transport — AGT -> ELC (single event to both units) -> ADC0/ADC1 sync
scan -> single-activation DTC chain -> block-boundary interrupt -> ping-pong swap -> `read_block()`
— works on silicon. What is still unproven: actual I/Q phase coherence, which needs a known coherent
signal source on P000/P004 (the transport is structurally single-activation, and `unit1_stalls=0`
is consistent with coherence, but it is not a phase measurement). That is the next bench step.

## Phase-3 block DSP: DC removal + x2 decimation (in C)

Report tag: **SDR-RA6M3-DSP-20260821-01**. Evidence class: **hardware, on target** (verified on
VK_RA6M3 / COM18 on 2026-08-22 after a J-Link reset cleared the AGT lock-out described below).

Bench result: `iq.dsp_status()` returns `{dsp_samples: 64, dsp_blocks: N, i_mean, q_mean}` with
`dsp_samples == block/2 == 64`, `dsp_blocks` incrementing in lock-step with `blocks`, and per-block
DC means computed live (e.g. i_mean ~2900 -> ~1900 as the floating input settled). `overruns == 0`,
`unit1_stalls == 0`. The C block-boundary DC-removal + x2-decimation stage runs on silicon.

Added an integer, allocation-free DSP stage that runs in the block callback, on the just-captured
half:

- `ra_iq_dsp_process()` in `ra/ra_iq_adc.c`: computes the per-block mean of I and Q, then produces
  `block_samples/2` centered, x2-decimated samples per channel as `centered = (x[2j]+x[2j+1])/2 -
  mean` (equivalent to averaging two mean-removed samples; mean removal is linear). Integer only, so
  no FPU state is touched in the ISR.
- Output buffers `s_i_dc` / `s_q_dc` (`int16`, `RA_IQ_ADC_MAX_BLOCK_SAMPLES/2`), overwritten each
  block. Counters in a new `ra_iq_dsp_status_t` (`dsp_blocks`, `dsp_samples`, `i_mean`, `q_mean`),
  reset in `start()`, read via `ra_iq_adc_get_dsp_status()`.
- Python: `iq.dsp_status()` returns `{dsp_blocks, dsp_samples, i_mean, q_mean}` (control-plane dict,
  not the realtime path). New qstrs in `qstrdefsport.h`.

## CMSIS-DSP enabled for VK_RA6M3

- `boards/VK_RA6M3/mpconfigboard.mk`: `MICROPY_HW_ENABLE_DSP = 1`. This compiles the CMSIS-DSP f32
  library (FIR, biquad, `arm_cmplx_mag_f32`, RFFT, RMS, ...) for `ARM_MATH_CM4`, available to C code
  (intended for the phase-3 AM demod / filters).
- `ports/renesas-ra/Makefile`: `moddsp.c` (the Python `dsp` module) is left OUT of the build. Its
  `MP_QSTR_*` tokens are not picked up by the qstr scan on this port and it collides with the
  frozen-collected `run` qstr; it is not needed for the C-side DSP path. The library itself still
  builds and links.

## Build environment note (IMPORTANT) — UCRT64 vs MINGW64

The firmware links from **UCRT64** (`arm-none-eabi-gcc`), but the LVGL Python binding
(`build/lvgl/lv_mpy.c`, ~39 k lines) is generated by `python_api_gen_mpy.py`, which needs a **host
`gcc` and `pycparser`** — present only in **MINGW64** (`/mingw64/bin/gcc`, pycparser 2.22), NOT in
UCRT64. So the generated `lv_mpy.c` must be produced under MINGW64 and is then compiled under
UCRT64. A `make clean` under UCRT64 deletes `lv_mpy.c`; regenerating it there produces an empty
stub (0-byte preprocessor output -> no bindings -> `undefined reference to mp_lv_roots` at link).

Recovery, if `lv_mpy.c` is ever lost: from a MINGW64 shell,
`export PATH=/mingw64/bin:/usr/bin:$PATH && make BOARD=VK_RA6M3 build-VK_RA6M3/lvgl/lv_mpy.c`, then
build the firmware normally from UCRT64. Do not `make clean` the VK_RA6M3 tree from UCRT64.

- `boards/VK_RA6M3/dave2d_port.c`: anchored `MP_REGISTER_ROOT_POINTER(void *mp_lv_roots)` and
  `mp_lv_user_data` here (the TU that owns their GC lifecycle), because the copies in the generated
  `lv_mpy.c` do not survive the qstr preprocessing pass on a clean build. Identical registrations
  deduplicate, so this is safe alongside a good `lv_mpy.c`.

Build after all of the above: MSYS2/UCRT64, `make BOARD=VK_RA6M3 -j8`, exit 0, `firmware.bin`
1550004 bytes, 50 `ra_iq_*`/`iqadc` symbols linked, CMSIS-DSP compiled, LVGL binding restored.

## Hardware lock-out found on the bench: AGT stuck running across warm reset

During bring-up the DSP build was initially blocked on hardware: `IQADC(...)` failed at construction
with `OSError(EIO)`. Instrumenting `ra_iq_adc_init()` localized it to the very first step:
`ra_iq_reserve_timer()` returning false. Register readout (`machine.mem8`) showed **both AGT0 and
AGT1 running** (`AGTCR = 0x23`: `TSTART=1, TCSTF=1`) at power-up, and `ra_agt_timer_reserve()`
correctly rejected a channel whose timer was already counting.

The AGT run state survives every warm reset tried (J-Link `r` = SYSRESETREQ, and RSetType 5 =
reset core+peripherals), and register writes to stop it (`AGTCR.TSTART=0`, `AGTCR.TSTOP=1`) do not
take. This matches the known RA behaviour that SYSRESETREQ does not clear all peripheral state. A
leftover from an earlier test session left both AGTs counting.

Conclusion: **this was a board-state lock-out, not a driver defect.** Recovery (operator,
2026-08-22): a **J-Link reset clears it — no power cycle needed**. `reset_go.jlink` (`r; g; q`) was
enough here; after it, `AGTCR = 0x00` on both units and `IQADC()` constructs and runs normally. The
Phase-3 DSP smoke test above was then verified on VK_RA6M3 / COM18. Operator rule: run a J-Link reset
**before every REPL / mpremote start** on this board. Robustness follow-up still worth considering:
have the I/Q driver force-stop a stuck AGT during reserve so a crashed session without `deinit()`
cannot lock out the next run without a debugger reset.

AM demod and timed DAC/DMAC audio sink status: code path is now present and builds through the
existing timer-paced double-buffered DAC API,
`ra_dac_write_timed_double_buffered(ch, buf_a, buf_b, ..., freq, fill_cb, stop_cb, ctx, timer_ch)`.
It is exposed as `IQADC.am_dac(P014[, ch])`, `am_dac_stop()` and `am_status()`, feeds decimated AM
envelope samples from the block callback to the DAC fill callback through a static SPSC ring, and does
not use Python in the realtime data path. `ra_gen/vector_data.c/.h` now also allocate `DMAC0_INT` and
`DMAC1_INT` IRQ slots for the DAC DMAC completion callbacks. This part is not yet bench-verified;
next proof is a P014 scope/headphone/load check plus `am_status()` counters.
