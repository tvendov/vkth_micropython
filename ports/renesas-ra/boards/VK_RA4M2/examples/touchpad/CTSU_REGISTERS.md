# CTSU1 register notes for RA4M2

This note focuses only on the register fields that matter to the current
`TouchPad` port and the cap-meter extension.

## CTSUCR1

Key fields:

- `CTSUCLK[1:0]`
  Selects the operating clock:
  - `00 -> PCLKB`
  - `01 -> PCLKB/2`
  - `10 -> PCLKB/4`
- `CTSUATUNE1`
  Power supply capacity adjustment:
  - `0 -> normal output`
  - `1 -> high-current output`

Current tree state:

- `CTSUCLK = 00b`
- `CTSUATUNE1 = 0`

## CTSUSDPRS

Fields:

- `CTSUPRRATIO[3:0]`
  Measurement time and pulse count adjustment.
- `CTSUPRMODE[1:0]`
  Base pulse count selector.
- `CTSUSOFF`
  High-pass noise reduction:
  - `0 -> on`
  - `1 -> off`

Current tree state:

- `CTSUSDPRS = 0x23`
- `CTSUPRRATIO = 3`
- `CTSUPRMODE = 2`
- `CTSUSOFF = 0`

Meaning:

- `prmode = 2 -> 62 base pulses`
- `measurement_pulses = 62 * 3 + 1 = 187`

## CTSUSST

Purpose:

- Stabilization wait time for TSCAP voltage before measurement starts.

Hardware manual rule:

- keep it fixed at `0x10`

Current tree state:

- `CTSUSST = 0x10`

This should remain fixed in normal mode. If exposed at all, it should be a
debug-only escape hatch.

## CTSUSSC

Field:

- `CTSUSSDIV[3:0]`
  Spectrum diffusion divider selection derived from base clock.

This is a per-element setting, because `CTSUSSC` is written for each channel
in the `CTSUWR` phase.

Current wrapper default:

- `ssdiv = CTSU_SSDIV_4000`

For the metrology API, `ssdiv` belongs in per-channel config.

## CTSUSO0

Fields:

- `CTSUSNUM[5:0]`
  Repeats the measurement pulse count `snum + 1` times.
- `CTSUSO[9:0]`
  Sensor offset value.

Current tree state:

- `CTSUSNUM = 7`
- `CTSUSO = 0x100` as the default wrapper seed before any tuning or manual
  offset write

## CTSUSO1

Fields:

- `CTSURICOA[7:0]`
- `CTSUSDPA[4:0]`
  Base clock divider input to the sensor drive pulse.
- `CTSUICOG[1:0]`
  ICO gain adjustment.

Current tree state:

- `CTSUSO1 = 0x380F`

Breakdown:

- `CTSUICOG = 1` -> `66%`
- `CTSUSDPA = 24`
- `CTSURICOA = 0x0F`

With current board clocks:

- `PCLKB = 50 MHz`
- `CTSUCLK = PCLKB`
- `base_hz = 50 MHz / (2 * (24 + 1)) = 1 MHz`

## CTSUST

Important status bits:

- `CTSUDTSR`
  Data transfer status flag.
- `CTSUSOVF`
  Sensor counter overflow flag.
- `CTSUROVF`
  Reference counter overflow flag.

Overflow behavior:

- if sensor counter overflows, `CTSUSC` reads as `0xFFFF`
- if reference counter overflows, `CTSURC` reads as `0xFFFF`

For cap-meter mode, overflow should be treated as:

`(sen == 0xFFFF) || (ref == 0xFFFF) || CTSUSOVF || CTSUROVF`

## CTSUSC and CTSURC

- `CTSUSC`
  Sensor ICO counter result.
- `CTSURC`
  Reference ICO counter result.

These are the two raw counters needed for metrology mode.

## Per-channel vs global split

Global / CTSU instance:

- `CTSUCLK`
- `CTSUPRMODE`
- `CTSUPRRATIO`
- `CTSUATUNE1`
- `CTSUSOFF`
- `CTSUSST` (fixed)

Per-channel / per-element:

- `CTSUSSDIV`
- `CTSUSO`
- `CTSUSNUM`
- `CTSUSDPA`
- `CTSUICOG`

This split matches how the hardware is written:

- `CTSUSDPRS`, `CTSUCR1`, `CTSUSST` are shared engine settings
- `CTSUSSC`, `CTSUSO0`, `CTSUSO1` are loaded per element during `CTSUWR`
