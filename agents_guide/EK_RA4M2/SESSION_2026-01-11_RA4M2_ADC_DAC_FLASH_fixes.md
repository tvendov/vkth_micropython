## Session: RA4M2 build fixes (ADC TSN type, DAC series, Flash HP API)

### Build errors observed (BOARD=EK_RA4M2)
- `ra/ra_adc.c`: `unknown type name 'R_TSN_Type'` and TSN calibration field access errors.
- `ra/ra_dac.c`: `#error "CMSIS MCU Series is not specified."` and `DAC_CH_SIZE` undefined.
- `ra/ra_flash.c`: implicit declarations for `R_FLASH_LP_*` (but `R_FLASH_HP_*` is available).

### Root cause
1. **ADC internal temperature calibration**: RA4M2 CMSIS headers in this repo do not expose `R_TSN_Type`, so the RA4 TSN calibration access via `R_TSN_Type` fails.
2. **DAC**: `ra_dac.c` did not treat RA4M2 as part of the RA4 family list.
3. **Flash**: RA4M2 uses **FACI_HP**, therefore the FSP API is `R_FLASH_HP_*`, but `ra_flash.c` was still selecting `R_FLASH_LP_*` for RA4M2.

### Fixes applied
- `ports/renesas-ra/ra/ra_adc.c`
  - Removed dependency on `R_TSN_Type`.
  - Read RA4 TSN calibration bytes directly from fixed address `0x407EC000` (same address previously used).
- `ports/renesas-ra/ra/ra_dac.c`
  - Added `RA4M2` to RA4-family conditionals so `DAC_CH_SIZE` and DAC pins are defined.
- `ports/renesas-ra/ra/ra_flash.c`
  - Mapped `RA4M2` to `R_FLASH_HP_*` APIs; kept `RA4M1/RA4W1` on `R_FLASH_LP_*`.

### Next step
Re-run:
- `make BOARD=EK_RA4M2 -j16`

and send the next compiler/linker error (if any).
