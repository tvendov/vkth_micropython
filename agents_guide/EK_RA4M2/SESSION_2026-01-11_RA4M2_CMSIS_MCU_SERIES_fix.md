## Session: Fix RA4M2 build error "CMSIS MCU Series is not specified"

### Problem
Building the Renesas-RA port with `CMSIS_MCU=RA4M2` failed early with preprocessor errors:
- `ports/renesas-ra/ra/ra_adc.h: #error "CMSIS MCU Series is not specified."`
- `ports/renesas-ra/ra/ra_int.h: #error "CMSIS MCU Series is not specified."`

Root cause: common RA helpers had conditionals for RA4M1/RA4W1/RA6* but not RA4M2.

### Changes made (code)
1) `ports/renesas-ra/ra/ra_int.h`
- Added `RA4M2` into the RA4 branch so `IRQ_MAX` is defined (48), matching RA4M1/RA4W1.

2) `ports/renesas-ra/ra/ra_adc.h`
- Added `RA4M2` into RA4 14-bit ADC resolution selection.
- Treated `RA4M2` like `RA4M1` for `enum ADC14_PIN` channel constants.
- Added `RA4M2` into `RA_ADC_DEF_RESOLUTION` (14).

3) `ports/renesas-ra/ra/ra_adc.c`
- Added `RA4M2` into all RA4 conditionals used for:
  - TSN register selection
  - pin->ADC-channel table selection
  - ADC resolution (14/12) logic
  - internal temperature sensor conversion

### Notes / assumptions
- RA4M2 ADC channel numbering was assumed to be identical to RA4M1 for the purpose of unblocking the build.
- If the RA4M2 device/package differs in available analog channels, we may need to adjust the ANxxx list + pin map later (functional fix vs compile fix).

### Next step
Rebuild for `BOARD=EK_RA4M2` and report the next failing file (if any).

