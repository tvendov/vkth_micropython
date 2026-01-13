## EK_RA4M2 build unblock: RA4M2 missing conditionals (I2C + ICU)

### Errors
- `ra/ra_i2c.c`: `#error "CMSIS MCU Series is not specified."` (pins tables + clock calc)
- `ra/ra_icu.c`: `#error "CMSIS MCU Series is not specified."` (IRQ pin table)

### Root cause
RA4M2 was not included in RA4-family `#if/#elif` blocks.

### Fixes applied
- `ports/renesas-ra/ra/ra_i2c.c`
  - Treat `RA4M2` same as `RA4M1` for:
    - `scl_pins[]`
    - `sda_pins[]`
    - `ra_i2c_clock_calc()`
- `ports/renesas-ra/ra/ra_icu.c`
  - Treat `RA4M2` same as `RA4M1` in `ra_irq_pins[]` conditional block.

### Next step
Re-run:
- `make BOARD=EK_RA4M2 -j16`

and capture the next error (if any).
