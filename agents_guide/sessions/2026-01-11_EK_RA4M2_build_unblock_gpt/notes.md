## EK_RA4M2 build unblock: GPT (ra_gpt.c)

### Errors
- `ra/ra_gpt.c`: `#error "CMSIS MCU Series is not specified."`
- Missing `GPT_CH_SIZE`, `CH_GAP` and subsequent cascade of syntax errors (because the preprocessor fell into `#else/#error` branches).

### Root cause
`ra_gpt.c` had RA-family conditionals for RA4M1/RA4W1/RA6* but did not include RA4M2.

### Fix applied
Treat `RA4M2` like `RA4M1` in:
- Channel sizing:
  - `#if defined(RA4M1) || defined(RA4M2)`
  - `GPT_CH_SIZE = 8`, `CH_GAP = 0`
- GPT register list `gpt_regs[]` (use RA4M1 mapping GPT0..GPT7)
- GPT PWM pin table `ra_gpt_timer_pins[]` (use RA4M1 mapping)
- Timer width selection logic in `ra_gpt_timer_set_freq()` (32-bit for ch<=1)
- Clock enable/disable selection logic in `ra_gpt_timer_init()` / `ra_gpt_timer_deinit()`

### Next step
Re-run:
- `make BOARD=EK_RA4M2 -j16`

and paste the next compile error (if any).
