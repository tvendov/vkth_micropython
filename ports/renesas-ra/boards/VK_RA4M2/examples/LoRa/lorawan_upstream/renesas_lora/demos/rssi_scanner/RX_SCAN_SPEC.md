# RX_SCAN_SPEC — SX1262 EU868 RSSI Channel Scanner
**Target:** VK_RA4M2 + Wio-SX1262 daughterboard  
**Date:** 2026-05-07  **Author:** architect-mpy-ra  
**Companion to:** `TX_CW_SPEC.md` (transmit-side probe, same hardware)

---

## 1. Summary

`rx_scan.py` sweeps all 8 EU868 channels (867.1–868.5 MHz) in continuous-RX
mode, sampling instantaneous RSSI on each, and renders results to an SSD1306
128×32 OLED. It is a **pure RF measurement tool**: no MAC layer, no LoRaWAN
stack, no `lorawan` C module, no join, no server. The SX1262 is driven entirely
via raw SPI opcodes. A single push-button (P014) cycles between three display
modes (BAR / NUM / WATERFALL) on short-press, or pauses/resumes sweeping on
long-press. There is no hard-stop timer — RX has no regulatory duty-cycle
budget. This scanner runs on the second board (COM79) while the TX_CW probe
runs on the first (COM21).

---

## 2. Hardware Pin Map

| Signal         | RA4M2 Pin | Direction  | Notes                                      |
|----------------|-----------|------------|--------------------------------------------|
| SPI3 SCK       | P111      | Output     | SCI9 simple SPI, 8 MHz, CPOL=0 CPHA=0     |
| SPI3 MOSI      | P109      | Output     |                                            |
| SPI3 MISO      | P110      | Input      |                                            |
| NSS (chip-sel) | P206      | Output     | Manual GPIO toggle; active-low             |
| BUSY           | P002      | Input      | No pull. Poll low before issuing next cmd  |
| DIO1           | P015      | Input      | Configured input only; not polled in scan  |
| RST            | P001      | Output     | Active-low pulse ≥ 100 µs to reset chip    |
| RF switch EN   | P100      | Output     | Wio-SX1262 board RF chain enable; set high |
| OLED SCL       | P301      | Open-drain | SoftI2C, `Pin.IN` mode, 400 kHz           |
| OLED SDA       | P302      | Open-drain | SoftI2C, `Pin.IN` mode                    |
| Button         | P014      | Input      | Active-low, internal pull-up               |

**DIO2** is internal — routed to the Wio-SX1262 RF switch via
`SetDio2AsRfSwitchCtrl`. No separate GPIO needed.  
**DIO3** drives TCXO at 1.8 V via `SetDio3AsTcxoCtrl`. No separate GPIO needed.

---

## 3. EU868 Channel Table

Sweep order: ascending frequency (index 0 → 7).

| Idx | Freq (MHz) | fword (hex)  | Payload bytes (BE) | Source      |
|-----|------------|--------------|--------------------|-------------|
| 0   | 867.1      | 0x3631999A   | 36 31 99 9A        | CFList      |
| 1   | 867.3      | 0x3634CCCD   | 36 34 CC CD        | CFList      |
| 2   | 867.5      | 0x36380000   | 36 38 00 00        | CFList      |
| 3   | 867.7      | 0x363B3333   | 36 3B 33 33        | CFList      |
| 4   | 867.9      | 0x363E6666   | 36 3E 66 66        | CFList      |
| 5   | 868.1      | 0x3641999A   | 36 41 99 9A        | EU868 ch0   |
| 6   | 868.3      | 0x3644CCCD   | 36 44 CC CD        | EU868 ch1   |
| 7   | 868.5      | 0x36480000   | 36 48 00 00        | EU868 ch2   |

Formula: `fword = round(f_hz * 2^25 / 32_000_000)`, 4 bytes big-endian.  
The WATERFALL fixed channel is index 5 (868.1 MHz, EU868 default).

---

## 4. Init Sequence (one-time at boot)

Perform after hardware reset (RST low ≥ 100 µs, then high, then wait BUSY low).  
`cmd()` protocol: NSS low → poll BUSY=0 → SPI write → NSS high → poll BUSY=0.

| Step | Command                  | Opcode | Payload (hex)   | Notes                                     |
|------|--------------------------|--------|-----------------|-------------------------------------------|
| 1    | SetStandby(STDBY_RC)     | 0x80   | 00              | Baseline before all config                |
| 2    | SetRegulatorMode(DCDC)   | 0x96   | 01              | Enable DC-DC converter                    |
| 3    | SetDio3AsTcxoCtrl        | 0x97   | 02 00 01 40     | 1.8 V, 320 ticks = 5 ms                  |
| 4    | *(wait 10 ms)*           | —      | —               | Allow TCXO to stabilise                   |
| 5    | Calibrate(all)           | 0x89   | 7F              | All blocks; takes up to 3.5 ms            |
| 6    | *(wait BUSY low)*        | —      | —               | Calibration holdoff                       |
| 7    | ClearDeviceErrors        | 0x07   | 00 00           | Clear any POR error flags                 |
| 8    | SetDio2AsRfSwitchCtrl    | 0x9D   | 01              | DIO2 drives Wio-SX1262 internal RF switch |
| 9    | SetPacketType(LoRa)      | 0x8A   | 01              | Must precede modulation params            |
| 10   | SetModulationParams      | 0x8B   | 07 04 01 00     | SF7, BW125, CR4/5, LDRO=0                |
| 11   | SetPacketParams          | 0x8C   | 00 08 00 FF 01 00 | preamble=8, variable hdr, plen=255, CRC on, IQ std |
| 12   | SetBufferBaseAddress     | 0x8F   | 80 00           | TX base=128, RX base=0                    |

**No SetPaConfig, no SetTxParams** — this is RX-only. HP PA is never activated.

The per-channel frequency and RX commands are issued in the sweep loop (§5),
not in init. After step 12 the chip is in STDBY_RC, ready to sweep.

---

## 5. Per-Channel Sweep Procedure

Executed for each channel index 0–7 in order, every sweep iteration.

| Step | Command              | Opcode | Payload          | Notes                                      |
|------|----------------------|--------|------------------|--------------------------------------------|
| A    | SetRfFrequency       | 0x86   | 4 bytes BE       | From channel table (§3)                    |
| B    | SetRx(continuous)    | 0x82   | FF FF FF         | Timeout=0xFFFFFF → continuous RX mode      |
| C    | *(wait ≥ 8 ms)*      | —      | —                | PLL lock + AGC settle (SX1262 DS §14.2)    |
| D    | GetRssiInst ×4       | 0x15   | see §5.1         | Take 4 samples; keep maximum (peak hold)   |
| E    | SetStandby(STDBY_RC) | 0x80   | 00               | PA off RF path before next channel hop     |

Total per-channel time: ~8 ms settle + 4× SPI reads + STDBY overhead ≈ 10 ms.  
Full sweep (8 channels): ~80 ms per iteration.

### 5.1 `read_rssi_inst()` helper

```python
# Sketch — implementer fills body
def read_rssi_inst():
    buf = bytearray(3)
    wait_busy()
    nss(0)
    spi.write_readinto(bytes([0x15, 0x00, 0x00]), buf)
    nss(1)
    # buf[0] = 0x15 (loopback), buf[1] = status, buf[2] = rssi_byte
    return -buf[2] // 2   # dBm, per SX1262 DS §13.5.3
```

Take 4 calls; store `max(sample0, sample1, sample2, sample3)` — peak hold
represents loudest activity in the settle window.

### 5.2 `read_status()` helper (QA use)

```python
def read_status():
    buf = bytearray(2)
    wait_busy()
    nss(0)
    spi.write_readinto(bytes([0xC0, 0x00]), buf)
    nss(1)
    return buf[1]   # bits[6:4]=chip_mode, bits[3:1]=cmd_status
```

---

## 6. State Machine

```
                    ┌────────────────────────────────────┐
                    │              BOOT                  │
                    │  chip_reset() + radio_init()       │
                    │  chip in STDBY_RC                  │
                    └──────────────┬─────────────────────┘
                                   │ init done
                                   ▼
          ┌─────────────────── RUN_BAR ────────────────────────┐
          │  Entry: resume sweep from STDBY_RC                  │
          │  Loop: sweep all 8 channels, update rssi[8]        │
          │  Display: 8-bar graph (see §7)                     │
          └────────────────────────────────────────────────────┘
               │ short-press             │ long-press
               ▼                         ▼
          RUN_NUM                    PAUSE (any mode)
               │ short-press
               ▼
          RUN_WF
               │ short-press
               ▼
          RUN_BAR   (cycle)

          ┌──────────────────── PAUSE ─────────────────────────┐
          │  Entry: SetStandby(STDBY_RC) — chip idle, RX off   │
          │  Display: current mode frozen + "PAUSED" indicator  │
          │  No sweep; rssi[] values unchanged                  │
          │  Exit: long-press → SetRfFrequency + resume sweep  │
          └────────────────────────────────────────────────────┘
```

**Entry / exit actions:**

| State    | Entry action                                  | Exit action                        |
|----------|-----------------------------------------------|------------------------------------|
| RUN_BAR  | Resume sweep (SetRfFrequency + SetRx per ch)  | Nothing — mode var changes         |
| RUN_NUM  | Same sweep loop, different render             | Nothing                            |
| RUN_WF   | Same sweep loop, different render             | Nothing                            |
| PAUSE    | `cmd(0x80, b'\x00')` (SetStandby STDBY_RC)    | Resume sweep on next loop tick     |

PAUSE is **orthogonal** — it can freeze any of the three run modes. The `mode`
variable (0/1/2) is preserved during PAUSE; button short-press is ignored while
paused.

**Button semantics:**

| Press duration | RUN (any mode)                 | PAUSE                      |
|----------------|--------------------------------|----------------------------|
| Short (< 1 s)  | Advance mode: 0→1→2→0          | Ignored                    |
| Long (≥ 1 s)   | → PAUSE (freeze current mode)  | → RUN (resume same mode)   |

Edge-detect logic: record `press_t = ticks_ms()` on falling edge (1→0),
evaluate on rising edge (0→1). Debounce: 80 ms `sleep_ms` after rising edge
before reading held duration. Identical pattern to TX_CW_SPEC.

---

## 7. OLED Layouts (128×32 px, 8×8 font)

### Mode 0 — BAR

Header: 8 px row. Bars: bottom 24 px.  
Bar height mapping: RSSI -120 dBm → 0 px, -50 dBm → 23 px (linear clamp).  
`h = max(0, min(23, int((rssi + 120) * 23 / 70)))`  
Each bar: 14 px wide, 2 px gutter right. Bars drawn bottom-up at x = idx*16.

```
┌────────────────────────────────┐  y=0
│BAR   -120 / -50 dBm           │  (16 chars, header)
│            ██                  │  y=8
│        ██  ██  ██              │
│██  ██  ██  ██  ██  ██  ██  ██ │  y=31 (bottom)
└────────────────────────────────┘
  ch0 ch1 ch2 ch3 ch4 ch5 ch6 ch7
  867.1 ----ascending----> 868.5
```

Bar columns: ch0 → x=0..13, ch1 → x=16..29, ..., ch7 → x=112..125. Gutter at
x=14..15 per bar (2 px, background colour = off).

### Mode 1 — NUM

Top-4 channels by RSSI, sorted descending. 2×2 grid of 64×16 cells.

```
┌───────────────┬───────────────┐  y=0
│ 8.1  │  7.9  │  col 0        │  freq label (3-4 chars), font×2 (16px wide)
│-105  │ -98   │               │  dBm value  (4-5 chars), font×2
├───────────────┼───────────────┤  y=16
│ 8.3  │  7.7  │  col 1        │
│-112  │ -115  │               │
└───────────────┴───────────────┘  y=32
```

Per cell (64×16 px): freq at y_cell, dBm at y_cell+8. Font 8×8 normal.  
Freq label: strip leading `86` → show `7.1` / `8.5` / `8.1` etc. (3 chars).  
dBm label: `"%-4d" % rssi` (sign + digits, left-aligned, 4 chars max `-120`).  
If fewer than 4 channels have valid data, pad with `"---"` / `"----"`.

### Mode 2 — WATERFALL (fixed channel: 868.1 MHz, idx=5)

```
┌────────────────────────────────┐  y=0
│WF 868.1    -105 dBm            │  (header, 16 chars)
│                              * │  y=8  (new point at right)
│                    *   *       │
│*   *   *               *   *  │  y=31
└────────────────────────────────┘
  oldest ←──── history ────→ newest
```

128-element integer ring buffer `wf_buf` holds pixel y positions (y=8..31 →
24 rows). Each sweep iteration: compute y for `rssi[5]`, shift buffer left by
one, append new y at `wf_buf[127]`. Clear display area rows 8–31, then plot
one pixel per column at `wf_buf[i]`.

`wf_y = 31 - max(0, min(23, int((rssi[5] + 120) * 23 / 70)))`

**PAUSE during WATERFALL:** buffer stops updating; last frame frozen on display.

### PAUSE overlay

Append ` [P]` to the header line of the active mode (truncated to 16 chars):

```
BAR   -120/-50 [P]     ← mode 0 paused
WF 868.1  -105 [P]     ← mode 2 paused
```

---

## 8. Pseudo-code Skeleton (do NOT treat as production code)

```python
# ---- Hardware objects ----
from machine import SPI, Pin, SoftI2C
from utime import ticks_ms, ticks_diff, sleep_ms

spi   = SPI(3, baudrate=8_000_000, polarity=0, phase=0,
            sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))
nss   = Pin('P206', Pin.OUT, value=1)
busy  = Pin('P002', Pin.IN)
dio1  = Pin('P015', Pin.IN)
rst   = Pin('P001', Pin.OUT, value=1)
rf_en = Pin('P100', Pin.OUT, value=1)
btn   = Pin('P014', Pin.IN, Pin.PULL_UP)
i2c   = SoftI2C(scl=Pin('P301', Pin.IN), sda=Pin('P302', Pin.IN), freq=400_000)
oled  = SSD1306(i2c)   # same class as rssi_scanner.py / tx_cw.py

# ---- Channel table ----
CHANNELS = [
    (867_100_000, bytes([0x36, 0x31, 0x99, 0x9A])),
    (867_300_000, bytes([0x36, 0x34, 0xCC, 0xCD])),
    (867_500_000, bytes([0x36, 0x38, 0x00, 0x00])),
    (867_700_000, bytes([0x36, 0x3B, 0x33, 0x33])),
    (867_900_000, bytes([0x36, 0x3E, 0x66, 0x66])),
    (868_100_000, bytes([0x36, 0x41, 0x99, 0x9A])),  # idx=5, WF fixed
    (868_300_000, bytes([0x36, 0x44, 0xCC, 0xCD])),
    (868_500_000, bytes([0x36, 0x48, 0x00, 0x00])),
]
WF_CHAN = 5

# ---- SPI helpers ----
def wait_busy(): ...                         # poll busy.value() == 0
def cmd(op, payload=b''):
    wait_busy(); nss(0)
    spi.write(bytes([op]) + bytes(payload))
    nss(1)
def read_rssi_inst(): ...                    # see §5.1
def read_status(): ...                       # see §5.2

# ---- Init ----
def chip_reset(): ...                        # RST low 100 µs, high, wait_busy
def radio_init(): ...                        # steps 1–12 from §4

# ---- State ----
rssi    = [-120] * 8    # current RSSI per channel, dBm
wf_buf  = [31] * 128    # WATERFALL pixel-y history
mode    = 0             # 0=BAR, 1=NUM, 2=WF
paused  = False

btn_prev = 1
press_t  = 0
LONG_MS  = 1000

# ---- Sweep one full pass ----
def sweep():
    for idx, (_, fword) in enumerate(CHANNELS):
        cmd(0x86, fword)              # SetRfFrequency
        cmd(0x82, b'\xFF\xFF\xFF')    # SetRx continuous
        sleep_ms(8)                   # PLL + AGC settle
        samples = [read_rssi_inst() for _ in range(4)]
        rssi[idx] = max(samples)      # peak hold
        cmd(0x80, b'\x00')            # SetStandby STDBY_RC

# ---- Render ----
def render_bar(): ...    # draws header + 8 bars per §7
def render_num(): ...    # sorts rssi[], draws 2×2 grid per §7
def render_wf(): ...     # updates wf_buf, draws waterfall per §7
def render():
    oled.fill(0)
    if   mode == 0: render_bar()
    elif mode == 1: render_num()
    else:           render_wf()
    if paused:      # append [P] to header (already rendered)
        pass        # implementer: overwrite header char 13..15
    oled.show()

# ---- Main ----
chip_reset()
radio_init()
render()

while True:
    now = ticks_ms()

    # Button edge detect
    bc = btn.value()
    if btn_prev == 1 and bc == 0:
        press_t = now
    elif btn_prev == 0 and bc == 1:
        sleep_ms(80)
        held = ticks_diff(now, press_t)
        if not paused:
            if held < LONG_MS:           # short press
                mode = (mode + 1) % 3
            else:                        # long press
                paused = True
                cmd(0x80, b'\x00')       # SetStandby
        else:
            if held >= LONG_MS:          # long press only
                paused = False           # resume; sweep() next tick
    btn_prev = bc

    if not paused:
        sweep()

    render()
    # No sleep_ms here — sweep itself takes ~80 ms per pass
```

---

## 9. Test Plan

The implementer MUST verify the following three gates. No creative
interpretation allowed.

### T1 — Chip status after init = 0x2A (STDBY_RC, command OK)

After `chip_reset()` + `radio_init()`:

```python
s = read_status()
# bits[6:4] = chip_mode; bits[3:1] = cmd_status
# Expected: 0x2A → chip_mode=STDBY_RC=2, cmd_status=OK=5
assert s == 0x2A, "init failed: status=0x%02X" % s
```

### T2 — Per-channel RSSI reads are in plausible range and chip returns to STDBY_RC

Run one `sweep()`. After the call:

```python
for i, r in enumerate(rssi):
    assert -140 <= r <= 0, "ch%d rssi out of range: %d dBm" % (i, r)
s = read_status()
chip_mode = (s >> 4) & 0x7
assert chip_mode == 2, "expected STDBY_RC after sweep, got mode %d" % chip_mode
```

Expected: all 8 values in `-140..0` dBm; chip mode = 2 (STDBY_RC).  
In a quiet RF environment expect values in the `-110..-120 dBm` noise-floor
range. If TX_CW board is active on 868.1 MHz, `rssi[5]` should be distinctly
higher than the others.

### T3 — Long-press enters PAUSE; sweep stops; short-press ignored

1. Boot to RUN_BAR (`paused == False, mode == 0`).
2. Long-press P014 (hold ≥ 1 s, release). Verify `paused == True`.
3. Attempt a short-press. Verify `mode` remains `0`.
4. Read chip status — must be 0x2A (STDBY_RC, not RX mode):

```python
s = read_status()
assert (s >> 4) & 0x7 == 2, "chip must be STDBY_RC during PAUSE"
```

5. Long-press again. Verify `paused == False` and next loop iteration calls
   `sweep()` — confirm `rssi[]` values change (place a finger near antenna to
   raise noise floor on at least one channel).

---

## 10. Risk List

| Risk | Severity | Mitigation |
|------|----------|------------|
| **HP PA accidentally activated (SetPaConfig missing guard)** | High | Init sequence has NO SetPaConfig / SetTxParams — PA bias is never applied. Verify with T1 and T2: chip never enters TX mode (chip_mode=6). |
| **BUSY-poll wedge (BUSY stuck high)** | High | `wait_busy()` must include a timeout (e.g., 5 000 iterations). On timeout: call `chip_reset()` + `radio_init()`. Never spin-wait forever in the main loop. |
| **SetRx continuous locks PLL between channels (PLL not re-locked)** | Medium | Step E (SetStandby STDBY_RC) before Step A (SetRfFrequency) ensures the PLL is re-armed for each new frequency. Do not skip SetStandby between hops. |
| **RSSI samples taken before AGC settles (< 8 ms)** | Medium | Do not reduce the `sleep_ms(8)` in step C. SX1262 PLL lock is ≤ 60 µs (DS §6.1.4) but AGC convergence requires longer; 8 ms is the safe minimum. |
| **SoftI2C bus contention with OLED during SPI BUSY poll** | Low | SoftI2C is synchronous and not interrupt-driven; it cannot pre-empt the SPI transaction. Render is called only after `sweep()` completes. No contention. |
| **`wf_buf` left-shift cost at 128 elements per sweep** | Low | `wf_buf[:-1] = wf_buf[1:]` in MicroPython copies 127 ints per frame (~80 µs at 48 MHz). Acceptable. If profiling shows otherwise, use a ring-buffer index instead. |
| **OLED `show()` latency blocking sweep timing** | Low | SSD1306 SoftI2C transfer at 400 kHz for 512 bytes ≈ 10 ms. This occurs after each sweep (~80 ms), so duty cycle is ~11%. Not a concern for RSSI accuracy. |
| **Single-board firmware shared with TX_CW board (COM21 vs COM79)** | Low | Both boards run the same firmware image. Ensure the correct `.py` script (`rx_scan.py` vs `tx_cw.py`) is placed in `main.py` on the respective board before flashing. |
