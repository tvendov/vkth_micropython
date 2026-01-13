## Session: Fix RA4M2 build error in FSP flash driver (r_flash_lp)

### Symptom
Build failed when compiling:
- `lib/fsp/ra/fsp/src/r_flash_lp/r_flash_lp.c`

Errors included:
- `#error "r_flash_lp is not a supported module for this board type."`
- `R_FACI_LP undeclared ... did you mean R_FACI_HP`
- `division by zero ... BSP_FEATURE_FLASH_LP_CF_WRITE_SIZE`

### Root cause
`ports/renesas-ra/Makefile` selected the **LP** flash driver (`r_flash_lp`) for `CMSIS_MCU=RA4M2`.
The RA4M2 device headers expose **FACI_HP** (not FACI_LP), so the correct FSP module is `r_flash_hp`.

### Fix
Updated `ports/renesas-ra/Makefile` flash module selection:
- `RA4M1` / `RA4W1` -> `r_flash_lp`
- `RA4M2` -> `r_flash_hp`

### Next step
Rebuild `BOARD=EK_RA4M2` and report the next compiler/linker error (if any).
