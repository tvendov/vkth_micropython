# TX_LORA_PKT — Spec

**Demo**: VK_RA4M2 + Wio-SX1262 transmits a LoRa packet every ~1 s.
Payload = incrementing 32-bit counter (big-endian). Counterpart RX to be the
rssi_scanner extended with packet decode (separate spec).

---

## 1. Files

```
demos/tx_lora_pkt/
    tx_lora_pkt.py   # all logic — mirroring rssi_scanner.py structure
    main.py          # boot shim: import tx_lora_pkt (mirrors tx_cw main.py)
```

`main.py`:
```python
import gc
gc.collect()
try:
    import tx_lora_pkt
except Exception as e:
    print('BOOT FAIL:', e)
```

---

## 2. Pin Table

| Signal  | Pin  | Mode               | Notes                          |
|---------|------|--------------------|--------------------------------|
| SCK     | P111 | SPI(3) alt-func    | SCI9 simple-SPI, proven        |
| MOSI    | P109 | SPI(3) alt-func    |                                |
| MISO    | P110 | SPI(3) alt-func    |                                |
| NSS     | P206 | OUT, value=1       | SW CS                          |
| BUSY    | P002 | IN                 | SX1262 busy line               |
| DIO1    | P015 | IN                 | TxDone / RxDone IRQ            |
| RST     | P001 | OUT, value=0       | held low at boot, async reset  |
| RF_EN   | P100 | OUT, value=0       | deferred 2 s into main()       |
| BTN     | P014 | IN, PULL_UP        | short=cycle pwr, long=TX on/off|
| OLED SCL| P301 | SoftI2C IN         | SSD1306 128x32 @ 0x3C          |
| OLED SDA| P302 | SoftI2C IN         |                                |

SPI: `SPI(3, baudrate=8_000_000, polarity=0, phase=0)`
I2C: `SoftI2C(scl=Pin('P301', Pin.IN), sda=Pin('P302', Pin.IN), freq=400_000)`

Note: `SPI(1)` does not work on this board — use `SPI(3)` only.

---

## 3. SX1262 Init Sequence (`radio_init`)

All opcodes identical to rssi_scanner except steps 8–14 are TX-specific.
Every `cmd()` is `async`; every wait yields (`await asyncio.sleep_ms`).

| Step | Opcode | Payload (hex)         | Description                          |
|------|--------|-----------------------|--------------------------------------|
| 1    | 0x80   | 00                    | SetStandby(STDBY_RC)                 |
| 2    | 0x96   | 00                    | SetRegulatorMode(LDO) — Wio has LDO, not DCDC |
| 3    | 0x97   | 02 00 01 40           | SetDio3AsTcxoCtrl(1.8 V, 320 ticks ≈ 9.8 ms) |
| 4    | —      | `await sleep_ms(10)`  | TCXO stabilisation ≥ 5 ms            |
| 5    | 0x89   | 7F                    | Calibrate(all blocks)                |
| 6    | —      | `await sleep_ms(5)` + `wait_busy` | Calibration up to 3.5 ms  |
| 7    | 0x07   | 00 00                 | ClearDeviceErrors                    |
| 8    | 0x9D   | 01                    | SetDio2AsRfSwitchCtrl                |
| 9    | 0x8A   | 01                    | SetPacketType(LoRa)                  |
| 10   | 0x86   | 36 41 99 9A           | SetRfFrequency(868.1 MHz) [note A]   |
| 11   | 0x95   | 04 07 00 01           | SetPaConfig(HP PA, 22 dBm, SX1262)  [note B] |
| 12   | 0x8E   | `pwr` 02              | SetTxParams(power_dbm, ramp=40 µs)  |
| 13   | 0x8B   | 07 04 01 00           | SetModulationParams(SF7, BW125, CR4/5, LDRO=0) [note C] |
| 14   | 0x8C   | 00 08 00 04 01 00     | SetPacketParams [note D]             |
| 15   | 0x8F   | 00 00                 | SetBufferBaseAddress(TX=0x00, RX=0x00)|
| 16   | 0x0D   | 07 40 34              | WriteRegister(0x0740, 0x34) sync MSB |
| 17   | 0x0D   | 07 41 44              | WriteRegister(0x0741, 0x44) sync LSB |
| 18   | 0x08   | 00 02 00 00 02 01 FF FF | SetDioIrqParams [note E]           |

**Notes:**

**A** — `fword = round((868.1e6 / 32e6) * 2**25) = 0x3641999A`.
Confirmed in rssi_scanner CHANNELS[5].

**B** — SetPaConfig bytes: `[paDutyCycle=0x04, hpMax=0x07, deviceSel=0x00 (SX1262), paLut=0x01]`.
Per SX1262 DS §13.1.14. hpMax=0x07 enables up to +22 dBm.

**C** — SetModulationParams bytes: `[SF=0x07, BW=0x04 (125 kHz), CR=0x01 (4/5), LDRO=0x00]`.
Per SX1262 DS §13.4.5. LDRO must be 0 for SF7 BW125 (symbol time 1.024 ms < 16.38 ms threshold).

**D** — SetPacketParams 6 bytes: `[preambleLen=0x0008, headerType=0x00 (explicit), payloadLen=0x04, crcType=0x01 (CRC on), invertIQ=0x00 (standard)]`.
Per SX1262 DS §13.4.6. PayloadLen fixed at 4 (counter always 4 bytes).

**E** — SetDioIrqParams 8 bytes: `[irqMask_H=0x00, irqMask_L=0x02, DIO1_H=0x00, DIO1_L=0x02, DIO2_H=0x00, DIO2_L=0x01, DIO3_H=0xFF, DIO3_L=0xFF]`.
Enables TxDone (bit 1) on DIO1. Per SX1262 DS §13.3.6.
`irqMask=0x0002`, `dio1Mask=0x0002`, `dio2Mask=0x0001 (reserved)`, `dio3Mask=0xFFFF`.

**Sync word 0x3444** — plain LoRa public network sync (not LoRaWAN 0x3444 / private 0x1424).
Written as two separate `WriteRegister` calls; the `cmd()` wrapper handles NSS/BUSY.

---

## 4. Per-Packet TX Flow

Called each iteration of the TX loop while `state == 'TX'`.

```
Step 1  Build payload
        payload = counter.to_bytes(4, 'big')   # uint32, wraps at 0xFFFFFFFF

Step 2  WriteBuffer(baseAddr=0x00, data)
        cmd(0x0E, bytes([0x00]) + payload)      # opcode 0x0E per SX1262 DS §13.2.3

Step 3  SetPacketParams (redundant but safe; payload length is always 4)
        cmd(0x8C, b'\x00\x08\x00\x04\x01\x00')

Step 4  SetTx — single-shot, no timeout
        cmd(0x83, b'\x00\x00\x00')             # timeout=0 → no auto-timeout

Step 5  Wait DIO1 high (TxDone IRQ), max 200 ms
        t0 = ticks_ms()
        while not dio1.value():
            if ticks_diff(ticks_ms(), t0) >= 200:
                print("TX timeout — missed TxDone")
                break
            await asyncio.sleep_ms(2)

Step 6  ClearIrqStatus
        cmd(0x02, b'\xFF\xFF')                  # opcode 0x02 per SX1262 DS §13.3.5

Step 7  SetStandby(STDBY_RC)
        cmd(0x80, b'\x00')

Step 8  counter = (counter + 1) & 0xFFFFFFFF

Step 9  Sleep remainder of 1 s window
        airtime_ms ≈ 36                         # SF7/BW125/4-byte payload/8 preamble/CRC
        elapsed_ms = ticks_diff(ticks_ms(), pkt_start)
        gap = max(0, 1000 - elapsed_ms)
        await asyncio.sleep_ms(gap)
```

**Airtime derivation** (SF7, BW125, CR4/5, explicit header, 4-byte payload, 8 preamble, CRC):

```
Tsym     = 2^SF / BW = 128 / 125000 = 1.024 ms
Tpreamble = (8 + 4.25) * 1.024 = 12.544 * 1.024 ≈ 12.8 ms
payloadSymbNb = 8 + ceil((8*4 - 4*7 + 28 + 16) / (4*4)) * (4+1)
              = 8 + ceil(12/16) * 5 = 8 + 5 = 13   (LDRO=0, explicit header)
Tpayload  = 13 * 1.024 ≈ 13.3 ms
Tpkt      ≈ 12.8 + 13.3 ≈ 26 ms
```

Conservative budget in spec: **36 ms** (covers BUSY overhead, TCXO startup jitter).
Sleep target: `1000 - elapsed_ms` measured from `pkt_start = ticks_ms()` before Step 2.

---

## 5. State Machine

```
         long-press
  IDLE ────────────► TX ──────────► IDLE
    ▲                │  long-press
    │                │
    │                ▼ (30 min elapsed since first TX)
    └──────────── STOP (no recovery without HW reset)
```

| State | RF     | Counter | OLED line 1       | Entry action               |
|-------|--------|---------|-------------------|----------------------------|
| IDLE  | off    | frozen  | `IDLE +14 dBm`    | SetStandby(STDBY_RC)       |
| TX    | active | running | `TX   +14 dBm`    | radio_init() + start loop  |
| STOP  | off    | frozen  | `STOP 30min HW`   | SetStandby, rf_en(0)       |

Transition IDLE → TX: if `T_first_tx_ms is None`, record `ticks_ms()` as epoch for 30-min timer.

---

## 6. Async Task Layout

```
asyncio.run(main())
    │
    ├── asyncio.create_task(button_task())   # long-lived coroutine
    └── tx_task()                            # runs in main() directly (mirrors rssi_scanner)
```

`button_task()` — polls `btn` every 20 ms, 80 ms debounce (same as rssi_scanner).
- Short press: cycle `pwr_idx` through `POWERS`; if state==TX, call `radio_init()` then resume.
- Long press: toggle IDLE ↔ TX; STOP ignores all button input.

`tx_task()` — inner `while True`:
```
if state == 'TX':
    pkt_start = ticks_ms()
    await tx_once()          # Steps 1–9 above
    render(force=True)
    gc.collect()
elif state == 'IDLE':
    await asyncio.sleep_ms(100)
elif state == 'STOP':
    await asyncio.sleep_ms(500)
```

Hard-stop check at top of `tx_task()` loop (same position as tx_cw_demo):
```python
if T_first_tx_ms is not None and state != 'STOP':
    if ticks_diff(ticks_ms(), T_first_tx_ms) >= 30 * 60 * 1000:
        await cmd(0x80, b'\x00')
        rf_en(0)
        state = 'STOP'
        render(force=True)
```

---

## 7. OLED Display

128×32, 3 lines (y=0, y=11, y=22), 8-pixel font. Same SSD1306 class verbatim from rssi_scanner.py.

| State | Line 0 (y=0)      | Line 1 (y=11)     | Line 2 (y=22)          |
|-------|-------------------|-------------------|------------------------|
| IDLE  | `IDLE +14 dBm`   | `868.1 MHz LoRa`  | `hold P014 -> TX`      |
| TX    | `TX   +14 dBm`   | `pkt #1234567`    | `36ms  02:15`          |
| STOP  | `STOP 30min HW`  | `safety auto-off` | `RESET to restart`     |

Line 2 in TX: `"%dms  %02d:%02d" % (airtime_ms, uptime_s//60, uptime_s%60)`.
`airtime_ms` = measured `ticks_diff` from WriteBuffer call to TxDone assertion.
`uptime_s` = seconds since TX state entered.

Power string: `"%+d dBm" % POWERS[pwr_idx]` — same formatting as tx_cw_demo.

---

## 8. Power Levels

```python
POWERS  = (-1, 5, 10, 14, 17, 20, 22)   # dBm
pwr_idx = 3                              # default +14 dBm
```

Short press cycles `pwr_idx = (pwr_idx + 1) % len(POWERS)`.
If state==TX, the new power takes effect on the next packet (re-issues SetTxParams via `radio_init()`
which is re-called on power change — see button_task).

Alternative: call only `cmd(0x8E, ...)` for in-flight power change without full re-init.
Decision: full `radio_init()` is simpler and safe because there is always ≥960 ms gap between packets.

---

## 9. Boot Sequence

```python
async def main():
    render_idle()                    # wipe stale display
    await asyncio.sleep_ms(2000)    # USB CDC enumeration window
    rf_en(1)
    await asyncio.sleep_ms(50)
    await chip_reset()              # RST low 1 ms, then high, wait_busy
    # radio_init() called on first IDLE→TX transition, not at boot
    print("tx_lora_pkt: IDLE — long-press P014 to start TX")
    asyncio.create_task(button_task())
    await tx_task()
```

`radio_init()` is **not** called at boot — it is deferred to the first IDLE→TX transition
(same as tx_cw_demo calling `radio_init()` inside the long-press handler).
This avoids RF emission before the user explicitly requests TX.

---

## 10. Acceptance Criteria

### Gate A — Boot / IDLE
- OLED shows `IDLE +14 dBm` within 3 s of power-on.
- No RF emission (spectrum analyser at 868.1 MHz shows noise floor).
- USB CDC REPL responsive within 3 s.

### Gate B — Active TX
- After long-press: OLED line 0 changes to `TX +14 dBm`, counter starts at 0 and
  increments visibly (at ~1 Hz).
- rssi_scanner on second board shows elevated RSSI on 868.1 MHz column matching
  CW RSSI measurement at the same dBm (within ±3 dBm).
- When scanner RX decode extension is available: received counter values are
  monotonically increasing, no gaps > 1 under benign conditions.
- Short press changes power; OLED updates within one packet cycle.

### Gate C — Hard Stop
- Exactly 30 min after first TX entry (not from boot), state transitions to STOP.
- OLED shows `STOP 30min HW`.
- RF ceases; rf_en held low.
- Button input is ignored.
- Only hardware reset or power cycle recovers the board.

---

## 11. Known Constraints / Deltas vs tx_cw_demo

| Item                  | tx_cw_demo            | tx_lora_pkt                        |
|-----------------------|-----------------------|------------------------------------|
| RF mode               | CW (SetTxContinuousWave) | LoRa packet (SetTx single-shot) |
| DIO1 usage            | unused                | TxDone IRQ — polled in loop        |
| SetPacketType         | not called (no LoRa)  | 0x8A 01 in radio_init              |
| SetModulationParams   | not called            | 0x8B in radio_init                 |
| SetPacketParams       | not called            | 0x8C in radio_init and per-packet  |
| Sync word registers   | not written           | 0x0740=0x34, 0x0741=0x44           |
| SetPaConfig           | same step 11          | same                               |
| SetRegulatorMode      | DCDC (0x01)           | LDO (0x00) — Wio-SX1262 has LDO   |
| Power change          | hot (steps 12+13 only)| re-init (full radio_init safe in gap)|
| PAUSE state           | present               | absent (simpler: only IDLE/TX/STOP)|
| asyncio               | no (blocking loop)    | yes — mirrors rssi_scanner         |

**LDO vs DCDC**: tx_cw_demo uses `0x01` (DCDC). The Wio-SX1262 module has an LDO regulator,
so `0x00` (LDO) is correct here. The scanner already uses `0x00`. tx_cw_demo's `0x01` works
in practice because the SX1262 falls back gracefully, but this spec is explicit.
