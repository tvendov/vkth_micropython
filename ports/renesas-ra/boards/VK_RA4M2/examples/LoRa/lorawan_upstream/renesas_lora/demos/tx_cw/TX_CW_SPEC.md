# TX_CW_SPEC — SX1262 Continuous-Wave Transmitter
**Target:** VK_RA4M2 + Wio-SX1262 daughterboard
**Date:** 2026-05-07  **Author:** architect-mpy-ra

---

## 1. Summary

`tx_cw.py` puts the SX1262 into continuous-wave (unmodulated carrier) mode at
868.1 MHz for indoor RF coverage measurement. It is a **pure RF tool**: no MAC
layer, no LoRaWAN stack, no `lorawan` C module, no OTAA, no server interaction.
The chip is driven entirely via raw SPI opcodes. A single push-button (P014)
cycles power levels (short press) or toggles TX on/off (long press). An SSD1306
128×32 OLED shows state. Transmission is hard-stopped 30 minutes after the first
TX start; only a hardware RESET recovers from STOP. The script **never** transmits
until the user explicitly long-presses the button.

---

## 2. Hardware Pin Map

| Signal          | RA4M2 Pin | Direction  | Notes                                      |
|-----------------|-----------|------------|--------------------------------------------|
| SPI3 SCK        | P111      | Output     | SCI9 simple SPI, 8 MHz, CPOL=0 CPHA=0     |
| SPI3 MOSI       | P109      | Output     |                                            |
| SPI3 MISO       | P110      | Input      |                                            |
| NSS (chip-sel)  | P206      | Output     | Manual GPIO toggle; active-low             |
| BUSY            | P002      | Input      | No pull. Poll low before issuing next cmd  |
| DIO1            | P015      | Input      | Not used in CW path; configure input only  |
| RST             | P001      | Output     | Active-low pulse ≥ 100 µs to reset chip    |
| RF switch EN    | P100      | Output     | Wio-SX1262 board RF chain enable; set high |
| OLED SCL        | P301      | Open-drain | SoftI2C, `Pin.IN` mode, 400 kHz           |
| OLED SDA        | P302      | Open-drain | SoftI2C, `Pin.IN` mode                    |
| Button          | P014      | Input      | Active-low, internal pull-up               |

**DIO2** is an internal SX1262 signal — routed to the Wio-SX1262 RF switch via
`SetDio2AsRfSwitchCtrl`. No separate GPIO needed.

---

## 3. Init Sequence

Perform after hardware reset (RST low ≥ 100 µs, then high, then wait BUSY low).
All commands use manual CS toggle: CS low → wait BUSY low → SPI write → CS high.
Commands marked **BUSY-wait before** require polling BUSY=0 before the write.
All commands require BUSY=0 **after** as well before the next command.

| Step | Command                 | Opcode | Payload (hex bytes)              | BUSY-wait before? |
|------|-------------------------|--------|----------------------------------|--------------------|
| 1    | SetStandby(STDBY_RC)    | 0x80   | 00                               | Yes                |
| 2    | SetRegulatorMode(DCDC)  | 0x96   | 01                               | Yes                |
| 3    | SetDio3AsTcxoCtrl       | 0x97   | 02 00 01 40                      | Yes                |
|      | *(1.8 V, 320 ticks=5 ms)*|       | *(voltage=0x02, timeout BE)*     |                    |
| 4    | *(wait 10 ms for TCXO)* | —      | —                                | —                  |
| 5    | Calibrate(all blocks)   | 0x89   | 7F                               | Yes                |
| 6    | *(wait BUSY low — calibration takes up to 3.5 ms)* | — | — | —         |
| 7    | ClearDeviceErrors       | 0x07   | 00 00                            | Yes                |
| 8    | SetDio2AsRfSwitchCtrl   | 0x9D   | 01                               | Yes                |
| 9    | SetPacketType(LoRa)     | 0x8A   | 01                               | Yes                |
| 10   | SetRfFrequency(868.1 MHz)| 0x86  | 36 41 99 9A                      | Yes                |
|      | *(fword = 868100000 × 2^25 / 32000000 = 0x3641999A)* | | |                |
| 11   | SetPaConfig(HP PA)      | 0x95   | 04 07 00 01                      | Yes                |
|      | *(paDutyCycle=4, hpMax=7, deviceSel=0, paLut=1)* | | |                     |
| 12   | SetTxParams(+14 dBm)    | 0x8E   | 0E 02                            | Yes                |
|      | *(power byte = dBm value; ramp=0x02 = 40 µs)*     | | |                     |
| 13   | SetTxContinuousWave     | 0xD1   | *(no payload)*                   | Yes                |

**Power-level byte mapping** (two's-complement, per SX1262 DS §13.4.4):

| dBm | Power byte (hex) |
|-----|-----------------|
| -1  | FF              |
|  +5 | 05              |
| +10 | 0A              |
| +14 | 0E  *(default)* |
| +17 | 11              |
| +20 | 14              |
| +22 | 16              |

Power cycle (short press while in TX): re-issue only step 12 (`SetTxParams`) then
step 13 (`SetTxContinuousWave`). No need to repeat steps 1–11.

Entry into TX from PAUSE: re-issue full sequence steps 1–13.

---

## 4. State Machine

```
                        ┌─────────────────────────────────────────────┐
                        │              BOOT                           │
                        │  hardware reset + init (steps 1–11)        │
                        │  chip in STDBY_RC, PA off                  │
                        └─────────────────┬───────────────────────────┘
                                          │ init done
                                          ▼
          ┌─────────────────────────── IDLE ──────────────────────────┐
          │  Entry: SetStandby(STDBY_RC). P100 may stay high (no TX). │
          │  Display: line1 "IDLE  +14 dBm"                           │
          │           line2 "868.1 MHz  CW"                           │
          │           line3 "hold P014 -> TX"                         │
          └───────────────────────────────────────────────────────────┘
                        │ long-press P014
                        ▼
          ┌──────────────── TX ───────────────────────────────────────┐
          │  Entry: steps 12+13 (SetTxParams + SetTxContinuousWave)   │
          │         record tx_start = ticks_ms()                      │
          │  Display: line1 ">TX   +XX dBm"                           │
          │           line2 "868.1 MHz  CW"                           │
          │           line3 "MM:SS  uptime"                           │
          │  Short-press: steps 12+13 with new power (stay in TX)     │
          └───────────────────────────────────────────────────────────┘
              │ long-press              │ 30 min elapsed (any state)
              ▼                         ▼
          ┌─────────── PAUSE ──┐   ┌──────────── STOP ─────────────────┐
          │ Entry: step 1      │   │ Entry: step 1 (STDBY_RC), P100=0  │
          │ (SetStandby RC)    │   │ Terminal — no transitions out.     │
          │ Display:           │   │ Display: line1 "STOP  30 min HW"  │
          │  "PAUSE +XX dBm"   │   │          line2 "safety auto-off"  │
          │  "hold P014 resume"│   │          line3 "RESET to restart" │
          │  "MM:SS"           │   └───────────────────────────────────┘
          └─────────────┬──────┘
                        │ long-press P014
                        │ (re-issue full sequence steps 1–13)
                        ▼
                       TX
```

**Button semantics:**

| Press duration | IDLE | TX | PAUSE | STOP |
|----------------|------|----|-------|------|
| Short (< 1 s)  | cycle power display only | cycle power + re-TX | — | — |
| Long (≥ 1 s)   | → TX | → PAUSE | → TX | — |

Short-press in IDLE cycles the power-level display but does NOT start TX.
No button action escapes STOP — only hardware RESET.

**Hard-stop trigger:** `ticks_diff(now, tx_start) >= 30*60*1000`. Check on every
loop iteration once `tx_start` has been set. Fires regardless of current state
(TX or PAUSE).

---

## 5. OLED Layouts (128×32, 8×8 font, 3 lines at y=0/11/22)

### IDLE
```
IDLE  +14 dBm   # y=0  (16 chars max)
868.1 MHz  CW   # y=11
hold P014 -> TX # y=22
```

### TX (MM:SS = elapsed since tx_start)
```
>TX   +14 dBm   # y=0
868.1 MHz  CW   # y=11
MM:SS  uptime   # y=22  e.g. "01:23  uptime"
```

### PAUSE
```
PAUSE +14 dBm   # y=0
hold P014 resume# y=11
MM:SS           # y=22
```

### STOP
```
STOP  30 min HW # y=0
safety auto-off # y=11
RESET to restart# y=22
```

Format power as `%+3d dBm` (e.g., `+14 dBm`, ` -1 dBm`). Format elapsed as
`"%02d:%02d" % (el_s // 60, el_s % 60)`.

---

## 6. Pseudo-code Skeleton

```python
# ---- Hardware objects (proven pattern from rssi_scanner.py) ----
spi   = SPI(3, baudrate=8000000, polarity=0, phase=0,
            sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))
nss   = Pin('P206', Pin.OUT, value=1)
busy  = Pin('P002', Pin.IN)
dio1  = Pin('P015', Pin.IN)          # configured, not polled in CW path
rst   = Pin('P001', Pin.OUT, value=1)
rf_en = Pin('P100', Pin.OUT, value=1)
btn   = Pin('P014', Pin.IN, Pin.PULL_UP)
i2c   = SoftI2C(scl=Pin('P301', Pin.IN), sda=Pin('P302', Pin.IN), freq=400_000)
oled  = SSD1306(i2c)                 # same class as rssi_scanner.py

# ---- Low-level SPI helpers ----
def wait_busy(): ...                 # poll busy.value() == 0
def cmd(opcode, payload=b''):
    nss(0); wait_busy()
    spi.write(bytes([opcode]) + payload)
    nss(1)

# ---- SX1262 init ----
def chip_reset(): ...                # RST low 100 µs, high, wait_busy()
def radio_init(): ...                # steps 1–11 from §3
def tx_start_cw(power_dbm): ...     # steps 12–13; records tx_start

# ---- State ----
POWERS = (-1, 5, 10, 14, 17, 20, 22)
pwr_idx  = 3        # default +14 dBm
state    = 'IDLE'   # 'IDLE' | 'TX' | 'PAUSE' | 'STOP'
tx_start = None     # ticks_ms() when first TX began

btn_prev = 1
press_t  = 0
LONG_MS  = 1000

# ---- Render (called every loop iteration) ----
def render(): ...   # draws correct layout per state, calls oled.show()

# ---- Main ----
chip_reset()
radio_init()
render()

while True:
    now = ticks_ms()

    # Hard-stop check
    if tx_start is not None and state != 'STOP':
        if ticks_diff(now, tx_start) >= 30 * 60 * 1000:
            cmd(0x80, b'\x00')      # SetStandby STDBY_RC
            state = 'STOP'
            render()

    if state != 'STOP':
        # Button edge detect
        bc = btn.value()
        if btn_prev == 1 and bc == 0:
            press_t = now
        elif btn_prev == 0 and bc == 1:
            held = ticks_diff(now, press_t)
            sleep_ms(80)            # debounce
            if held < LONG_MS:      # short press
                if state in ('IDLE', 'TX'):
                    pwr_idx = (pwr_idx + 1) % len(POWERS)
                    if state == 'TX':
                        tx_start_cw(POWERS[pwr_idx])
            else:                   # long press
                if state == 'IDLE':
                    if tx_start is None:
                        tx_start = ticks_ms()
                    tx_start_cw(POWERS[pwr_idx])
                    state = 'TX'
                elif state == 'TX':
                    cmd(0x80, b'\x00')   # STDBY_RC
                    state = 'PAUSE'
                elif state == 'PAUSE':
                    radio_init()
                    tx_start_cw(POWERS[pwr_idx])
                    state = 'TX'
        btn_prev = bc
        render()

    sleep_ms(50)
```

---

## 7. Test Plan

The implementer MUST verify the following three checks after writing the code.
No creative interpretation allowed.

### T1 — Chip status after init = 0x2A (STDBY_RC mode, command OK)

After `chip_reset()` + `radio_init()`, read chip status:

```python
nss(0); wait_busy()
buf = bytearray(2)
spi.write_readinto(bytes([0xC0, 0x00]), buf)   # GetStatus opcode 0xC0
nss(1)
# buf[1] bits [6:4] = chip mode; bits [3:1] = command status
# Expected: buf[1] == 0x2A  (chip mode=STDBY_RC=2, status=OK=5  => 0b00101010)
assert buf[1] == 0x2A, "init failed: status=0x%02X" % buf[1]
```

### T2 — Short-press cycles power on OLED without entering TX

1. Boot to IDLE (`state == 'IDLE'`).
2. Short-press P014 three times. Expected: `pwr_idx` increments each press
   (`14 → 17 → 20 → 22 dBm` on display).
3. Verify `state` remains `'IDLE'` throughout — no `SetTxContinuousWave` issued.
4. Visual check: OLED line1 shows updated power value after each press.

### T3 — Long-press from IDLE → TX; chip status = 0x6X (chip mode=TX=6)

1. Boot to IDLE.
2. Long-press P014 (hold ≥ 1 s, release).
3. Verify `state == 'TX'` and OLED line1 starts with `>TX`.
4. Read chip status immediately after transition:

```python
nss(0); wait_busy()
buf = bytearray(2)
spi.write_readinto(bytes([0xC0, 0x00]), buf)
nss(1)
chip_mode = (buf[1] >> 4) & 0x7    # bits [6:4]
assert chip_mode == 6, "expected TX mode (6), got %d" % chip_mode
```

---

## 8. Risk List

| Risk | Severity | Mitigation |
|------|----------|------------|
| **Open antenna / VSWR mismatch at +22 dBm** | High | Always connect a 50 Ω load or antenna before enabling TX. The SX1262 HP PA can be damaged by high reflected power. The 30-min hard-stop does not protect against this. |
| **OCP (over-current) trip on USB power at high PA** | Medium | +22 dBm draws ~120 mA from VDDPA; USB 2.0 port limit is 500 mA total. Use a powered hub or bench supply if running extended CW at max power. |
| **USB power glitch on PA enable** | Medium | `SetTxContinuousWave` causes a step load. Add 100 µF bulk capacitor on the Wio-SX1262 VDDPA rail if brown-out resets are observed. |
| **HP PA without dummy load during indoor test** | Low–Medium | Antenna gain + cable routing can create standing waves. Keep actual conducted power below +20 dBm if antenna impedance is unknown. |
| **BUSY pin not polled → corrupted SPI frame** | High | Every `cmd()` call MUST poll BUSY=0 before asserting CS. Skipping this on any command will corrupt the chip's internal state machine. |
| **TCXO stabilisation too short** | Low | Step 4 sleeps 10 ms; the TCXO spec is 5 ms. Do not reduce below 6 ms or frequency accuracy degrades. |
| **Re-issuing full init from PAUSE → TX** | Low | `Calibrate(0x7F)` takes up to 3.5 ms and momentarily draws more current. This is expected; do not shorten the post-calibrate BUSY wait. |
