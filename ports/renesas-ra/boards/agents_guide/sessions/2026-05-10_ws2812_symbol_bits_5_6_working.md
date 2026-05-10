# VK_RA4M2 WS2812 symbol_bits session result

Date: 2026-05-10

## Result

WS2812 driver works with explicit 6-bit symbol encoding on VK_RA4M2.

Confirmed by user: "OK works" after changing the test script to:

- `symbol_bits=6`
- `baudrate=0` / omitted explicit baudrate, using driver auto baudrate
- `P500` kept HIGH for WS2812 module power/enable
- `P112` used as WS2812 DIN/data line

## Driver behavior implemented

`machine.WS2812(..., symbol_bits=5 or 6, baudrate=0)`:

- `symbol_bits=5` selects:
  - encoding `0 -> 11000`, `1 -> 11100`
  - auto target baudrate `4166667`
  - expected SCI cell about `240 ns`
- `symbol_bits=6` selects:
  - encoding `0 -> 110000`, `1 -> 111000`
  - auto target baudrate `5000000`
  - expected SCI cell about `200 ns`

Anti-artifact stream protections preserved:

- raw zero prefix before payload
- raw zero latch suffix calculated from `latch_us` and selected baudrate
- DTC streaming path unchanged
- `sync()` still waits for end, disables SCI TX, and drives pin LOW

## Timing target for 6-bit mode

With `0 -> 110000`, `1 -> 111000`, SCI bit about `200 ns`:

- `T0H` about `400 ns`
- `T0L` about `800 ns`
- `T1H` about `600 ns`
- `T1L` about `600 ns`

This matched the user test well enough to work on real hardware.

## Test example changed

File:

`ports/renesas-ra/boards/VK_RA4M2/examples/ws2812_5_leds.py`

Important line:

`strip = WS2812(pixel_count=PIXEL_COUNT, pin=Pin(DATA_PIN), channels=3, symbol_bits=SYMBOL_BITS)`

where:

`SYMBOL_BITS = 6`

## Build note

Parallel fresh builds can fail with undeclared `MP_QSTR_*` in new QSTR-heavy files such as `lorawan/mod_lorawan.c`.
This is a known QSTR race, not a WS2812 driver failure.

Correct practice:

1. run a serial/warm-up QSTR generation pass (`-j1`) first if needed
2. then run normal parallel build (`-j4`/`-j8`)

Do not misdiagnose this as a LoRaWAN source bug unless it still fails after QSTR warm-up.
