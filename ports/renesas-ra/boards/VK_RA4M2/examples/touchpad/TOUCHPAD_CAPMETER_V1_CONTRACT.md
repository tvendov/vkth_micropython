# TouchPad capacitance-meter v1 contract

## Goal

Extend `machine.TouchPad` so the existing touch API stays intact, while a new
metrology API exposes raw CTSU behavior for capacitance measurement.

This is a `v1` contract. The first patch intentionally stops before baseline
storage, pF interpolation, or user-facing calibration workflows.

## Existing API stays as touch API

Do not change the behavior of:

- `read()`
- `value()`
- `config(threshold)`
- `offset_tune()`
- `offsets()`
- `set_offset()`

These remain touch-oriented.

## New metrology API

Per-channel / per-element methods:

```python
tp.read_counts() -> (sen, ref)

tp.timing() -> dict

tp.cap_config(
    *,
    sdpa=None,
    snum=None,
    icog=None,
    so=None,
    ssdiv=None,
    auto_ssdiv=True,
) -> None
```

Global / class-level CTSU methods:

```python
TouchPad.cap_global_config(
    *,
    ctsuclk_div=None,   # 1, 2, 4
    prmode=None,        # 0=510, 1=126, 2=62
    prratio=None,       # 0..15
    atune1=None,        # 0 normal, 1 high-current
    noise=None,         # True -> CTSUSOFF=0
    auto_offset=None,
    _sst_debug=None,
) -> None
```

## `read_counts()` rules

`read_counts()` is a metrology snapshot, not a touch decision.

It must:

- perform a normal CTSU scan
- wait for a valid read moment
- return raw `(sen, ref)`
- avoid threshold logic
- avoid baseline logic
- avoid implicit `offset_tune()`

Most important implementation rule:

- do not reread `R_CTSU->CTSUSC` / `R_CTSU->CTSURC` after FSP already captured
  them
- return the raw snapshot captured at the correct `CTSURD` moment

For RA4M2 CTSU1, the existing FSP raw self buffer already stores:

- `p_self_raw[i].sen`
- `p_self_raw[i].ref`

That is the correct source for `read_counts()`.

## `timing()` payload

`timing()` should return integer-friendly values:

```python
{
    "pclkb_hz": ...,
    "ctsuclk_hz": ...,
    "base_hz": ...,
    "base_cycle_ns": ...,
    "base_pulses": ...,
    "measurement_pulses": ...,
    "groups": ...,
    "group_ns": ...,
    "gate_ns": ...,
    "stabilize_ns": ...,
}
```

Useful formulas:

- `ctsuclk_hz = pclkb_hz / ctsuclk_div`
- `base_hz = ctsuclk_hz / (2 * (sdpa + 1))`
- `measurement_pulses = base_pulses * prratio + 1`
- `group_ns = ((measurement_pulses + base_pulses - 2) * base_cycle_ns) / 4`
- `gate_ns = group_ns * (snum + 1)`

## Overflow rule

Low-level raw counts should record overflow as:

```c
overflow =
    (sen == 0xFFFF) ||
    (ref == 0xFFFF) ||
    CTSUST.CTSUSOVF ||
    CTSUST.CTSUROVF;
```

## Global vs per-channel ownership

Global:

- `ctsuclk_div`
- `prmode`
- `prratio`
- `atune1`
- `noise`
- `auto_offset`
- `_sst_debug` only as escape hatch

Per-channel:

- `sdpa`
- `snum`
- `icog`
- `so`
- `ssdiv`
- `auto_ssdiv`

## Cap-meter guardrails

- No implicit `offset_tune()` inside `cap_config()`.
- No implicit `offset_tune()` inside `read_counts()`.
- No implicit `offset_tune()` inside future baseline or pF paths.
- Keep `CTSUSST = 0x10` by default.
- Changing global or per-channel cap settings invalidates any future baseline
  or calibration state.

## First patch scope

Only implement:

1. `tp.read_counts()`
2. `tp.timing()`
3. `tp.cap_config(...)`
4. `TouchPad.cap_global_config(...)`

Do not implement yet:

- `zero()`
- `read_delta()`
- `calibrate_pf()`
- `read_pf()`

Reason:

First we need raw `(sen, ref)` sweeps over real capacitors, for example:

- open
- `1 pF`
- `5 pF`
- `10 pF`
- `22 pF`
- `47 pF`
- `100 pF`

Then we can choose the best metrology metric and interpolation model.
