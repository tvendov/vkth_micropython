# CTSU1 on RA4M2: working model

## Scope

This note is about `CTSU1` on `RA4M2` used as a raw capacitance measurement
engine, not as a threshold-only touch button.

## Current port defaults

Current source-tree defaults:

- `PCLKB = 50 MHz`
- `CTSU_CFG_PCLK_DIVISION = 0`
- `CTSUCLK = PCLKB`
- `CTSUSDPA = 24`
- `CTSUICOG = 1` (`66%`)
- `CTSUPRMODE = 2` (`62 base pulses`)
- `CTSUPRRATIO = 3`
- `CTSUSNUM = 7`
- `CTSUATUNE1 = 0` (`normal output`)
- `CTSUSST = 0x10`

With these values:

- `ctsuclk_hz = 50 MHz`
- `base_hz = ctsuclk_hz / (2 * (sdpa + 1))`
- `base_hz = 50 MHz / (2 * 25) = 1 MHz`

So the current drive/base clock is about `1 MHz`.

## Mental model

The correct model is:

`base/drive timing -> sensor drive pulse -> charge/discharge current -> sensor ICO frequency -> counter gate -> raw counts`

Important:

- `1 MHz` is not the measured frequency.
- The measured frequency is `Sensor ICO`.
- `CTSUSC` counts `Sensor ICO`.
- `CTSURC` counts `Reference ICO`.

This means CTSU behaves much more like a frequency counter with a gate than
like a direct ADC.

## Measurement flow

For each measured element:

1. `CTSUWR` phase writes the per-element registers:
   - `CTSUSSC`
   - `CTSUSO0`
   - `CTSUSO1`
2. Writing `CTSUSO1` starts the sensor drive pulse.
3. CTSU waits for the `CTSUSST` stabilization interval.
4. Measurement starts.
5. `Sensor ICO` and `Reference ICO` are counted during the measurement window.
6. `CTSURD` phase transfers data.
7. Software reads the results from `CTSUSC` and `CTSURC`.

The hardware manual states that after `CTSU_CTSURD`, software reads
`CTSUSC` first and then `CTSURC`.

## What the pulse settings mean

`CTSUPRMODE` selects the base pulse count:

- `0 -> 510`
- `1 -> 126`
- `2 -> 62`

`CTSUPRRATIO` expands that pulse group:

- `measurement_pulses = base_pulses * prratio + 1`

`CTSUSNUM` repeats the measurement pulse count:

- groups = `snum + 1`

So for the current settings:

- `base_pulses = 62`
- `prratio = 3`
- `measurement_pulses = 62 * 3 + 1 = 187`
- `groups = 7 + 1 = 8`

## Measurement gate time

For CTSU1 on RA4M2, the useful timing formula is:

- `group_time = (base_pulses * prratio + 1 + base_pulses - 2) * 0.25 * base_cycle`
- `gate_time = group_time * (snum + 1)`

With:

- `base_cycle = 1 us`
- `base_pulses = 62`
- `prratio = 3`
- `snum = 7`

That gives:

- `group_time = 61.75 us`
- `gate_time ~= 494 us`

Important correction:

- `187 * 8 = 1496` is not a time.
- It is a pulse-count product.
- The actual measurement gate is about `494 us`.

## Why overflow happens

If `CTSUSC = 0xFFFF`, the sensor counter overflowed.
If `CTSURC = 0xFFFF`, the reference counter overflowed.

For the current settings, the raw count is roughly:

- `CTSUSC ~= f_sensor_ico * gate_time`

So to increase range you must reduce:

- `f_sensor_ico`
or
- `gate_time`
or both.

## What changes range

These settings mainly change excitation / analog gain:

- `CTSUCLK`
- `CTSUSDPA`
- `CTSUICOG`
- `CTSUATUNE1`

These settings mainly change gate time / accumulation:

- `CTSUPRMODE`
- `CTSUPRRATIO`
- `CTSUSNUM`

Practical meaning:

- lower drive/base frequency -> smaller current through the capacitance
- lower ICO gain -> lower counted frequency
- shorter gate -> fewer accumulated counts

That is the correct path for a `0..100 pF` capacitance meter.
