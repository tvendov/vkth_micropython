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
