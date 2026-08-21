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

## Build check

Built successfully from MSYS2/UCRT64:

```sh
export PATH=/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra
make BOARD=VK_RA6M3 -j8
```

Resulting artifacts:

- `build-VK_RA6M3/firmware.bin`: 1545168 bytes
- `build-VK_RA6M3/firmware.elf`: 17033872 bytes
- `build-VK_RA6M3/firmware.hex`: 4346315 bytes

