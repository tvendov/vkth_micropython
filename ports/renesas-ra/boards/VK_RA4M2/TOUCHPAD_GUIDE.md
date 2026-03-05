# VK_RA4M2 TouchPad Guide

This board uses the RA4M2 CTSU peripheral through `machine.TouchPad`.

The pin mapping below is based on the local RA4M2 datasheet for `R7FA4M2AC3CFP`.

## Valid touch pins

- `P205` = `TS01`
- `P206` = `TS02`
- `P407` = `TS03`
- `P408` = `TS04`
- `P409` = `TS05`
- `P410` = `TS06`
- `P411` = `TS07`
- `P412` = `TS08`
- `P413` = `TS09`
- `P414` = `TS10`
- `P415` = `TS11`
- `P708` = `TS12`

Do not use `P207` as a touch input. It is `TSCAP`, not `TSxx`.

## Basic use

```python
from machine import Pin, TouchPad

tp = TouchPad(Pin("P205"))
tp.config(600)

raw = tp.read()
pressed = tp.value()
print(raw, pressed)
```

- `read()` performs a blocking scan and returns the raw CTSU count.
- `value()` performs a blocking scan and compares the count against the configured threshold.
- `config(threshold)` sets the threshold for this `TouchPad`.

## Choosing a threshold

Start by printing raw values without touch, then while touching the pad.

```python
from machine import Pin, TouchPad
import time

tp = TouchPad(Pin("P205"))

for _ in range(20):
    print(tp.read())
    time.sleep_ms(100)
```

Pick a threshold between the idle and touched ranges, then apply it:

```python
tp.config(600)
```

## Cooperative mode

The CTSU wrapper also supports cached, non-blocking reads for cooperative code such as `uasyncio`.

```python
from machine import Pin, TouchPad

TouchPad.sample_rate(50)   # 50 full CTSU scans per second, global

tp = TouchPad(Pin("P205"))
tp.config(600)
```

Useful methods:

- `TouchPad.sample_rate([hz])`
  - get or set the global background sampling rate
  - `0` disables the background sampler
- `TouchPad.service()`
  - advances the sampler once from VM context
  - useful if you do not want the background timer
- `tp.start()`
  - starts one non-blocking scan if none is active
- `tp.ready()`
  - `True` after at least one cached sample is available
- `tp.read_cached()`
  - returns the last cached raw value
  - returns `None` if no sample is available yet
- `tp.value_cached()`
  - returns `0` or `1` from the last cached sample
  - returns `None` if no sample is available yet
- `tp.age_ms()`
  - age of the cached sample in milliseconds

## `uasyncio` example

```python
from machine import Pin, TouchPad
import uasyncio as asyncio

TouchPad.sample_rate(50)

tp = TouchPad(Pin("P205"))
tp.config(600)

async def touch_task():
    while True:
        if tp.ready() and tp.value_cached():
            print("touch", tp.read_cached(), tp.age_ms())
        await asyncio.sleep_ms(20)

asyncio.run(touch_task())
```

## Manual service loop

Use this if you want full control and no background timer:

```python
from machine import Pin, TouchPad
import uasyncio as asyncio

tp = TouchPad(Pin("P205"))
tp.config(600)

async def touch_task():
    while True:
        TouchPad.service()
        if tp.ready() and tp.value_cached():
            print("touch", tp.read_cached())
        await asyncio.sleep_ms(10)

asyncio.run(touch_task())
```

## Important notes

- `sample_rate` is global for the CTSU engine, not per pin.
- Only the touch channels you actually configure are activated at runtime.
- Do not use the same pin for touch and another peripheral at the same time.
- `read_cached()` and `value_cached()` depend on either `sample_rate(...)`, `service()`, or `start()`.
- `read()` and `value()` remain blocking and are kept for simple scripts and backward compatibility.
