# PKT Mode Extension — rssi_scanner.py

Adds a second mode (PKT) to the existing RSSI scanner.  
Modes: **SCAN** (8-channel sweep, existing) | **PKT** (camp on 868.1 MHz, decode TX packets).

---

## 1. Mode State Variable

```python
# Add after existing state vars (line ~122)
app_mode   = 'SCAN'   # 'SCAN' | 'PKT'

# PKT-mode runtime state (reset on each entry)
pkt_counter_last = None   # int or None
pkt_miss_count   = 0      # cumulative missed packets
pkt_last_rx_ms   = 0      # ticks_ms() of last RxDone
pkt_last_rssi    = -120
pkt_last_snr     = 0
```

---

## 2. Button Behaviour (replace `button_task`)

| Press | SCAN mode | PKT mode |
|-------|-----------|----------|
| Short | BST/STD gain toggle (unchanged) | reset `pkt_counter_last`, `pkt_miss_count` |
| Long  | → enter PKT | → return to SCAN |

```python
# Skeleton — replace existing long-press branch:
if held >= LONG_MS:
    if app_mode == 'SCAN':
        await enter_pkt_mode()
    else:
        await enter_scan_mode()
else:
    if app_mode == 'SCAN':
        rx_boosted = not rx_boosted
        await apply_rx_gain()
    else:
        # short press in PKT: reset counters
        pkt_counter_last = None
        pkt_miss_count   = 0
```

---

## 3. Mode Transition Helpers

### `enter_pkt_mode()`

```python
async def enter_pkt_mode():
    global app_mode, pkt_counter_last, pkt_miss_count
    # 1. Go to standby (sweep may have left chip in RX)
    await cmd(0x80, b'\x00')                          # SetStandby(STDBY_RC)
    # 2. Set 868.1 MHz
    await cmd(0x86, b'\x36\x41\x99\x9A')             # SetRfFrequency
    # 3. Modem params — must match TX exactly
    await cmd(0x8B, b'\x07\x04\x01\x00')             # SetModulationParams SF7/BW125/CR4_5/LDRO=0
    await cmd(0x8C, b'\x00\x08\x00\xFF\x01\x00')     # SetPacketParams (explicit hdr, CRC on, std IQ)
    #    preamble=8(0x0008), headerType=0x00, payloadLen=0xFF(any), crcType=0x01, invertIQ=0x00
    # 4. Public sync word 0x3444 — high byte first (TX spec §3 steps 16-17)
    await cmd(0x0D, b'\x07\x40\x34')                 # WriteRegister(0x0740, 0x34)
    await cmd(0x0D, b'\x07\x41\x44')                 # WriteRegister(0x0741, 0x44)
    # 5. IRQ: RxDone(bit0) + CrcErr(bit6) on DIO1; clear all first
    await cmd(0x02, b'\xFF\xFF')                      # ClearIrqStatus
    await cmd(0x08, b'\x00\x41\x00\x41\x00\x00\xFF\xFF')
    #    irqMask=0x0041(RxDone|CrcErr), DIO1=0x0041, DIO2=0x0000, DIO3=0xFFFF
    # 6. Continuous RX (no timeout)
    await cmd(0x82, b'\xFF\xFF\xFF')                  # SetRx(0xFFFFFF)
    # 7. Apply boosted gain (persists from SCAN, re-apply after freq change)
    await apply_rx_gain()
    # 8. Reset PKT state
    pkt_counter_last = None
    pkt_miss_count   = 0
    app_mode = 'SCAN'        # set last so sweep_task doesn't grab the chip
    app_mode = 'PKT'         # then flip — atomic enough under cooperative scheduler
    await render()
```

> **Tricky**: set `app_mode = 'PKT'` AFTER all SPI ops are done. The sweep task checks
> `app_mode` at its loop top; flipping early lets it call `sweep_once()` mid-transition.

### `enter_scan_mode()`

```python
async def enter_scan_mode():
    global app_mode
    await cmd(0x80, b'\x00')   # SetStandby — stops continuous RX
    # Sweep task will call radio_init() on first pass? No — radio_init()
    # is called once at boot. Sweep just calls SetRfFrequency + SetRx each channel.
    # Sync word doesn't matter for RSSI reads (chip is in RX, no decode).
    app_mode = 'SCAN'
    await render()
```

---

## 4. PKT RxDone Handler

```python
async def pkt_rx_once():
    """Called when DIO1 is high in PKT mode. Reads one packet and updates state."""
    global pkt_counter_last, pkt_miss_count, pkt_last_rx_ms, pkt_last_rssi, pkt_last_snr

    # Step 1 — GetIrqStatus (opcode 0x12), response: status + irq_H + irq_L
    buf = bytearray(4)
    nss(0); spi.write_readinto(b'\x12\x00\x00\x00', buf); nss(1)
    await wait_busy("GetIrqStatus")
    irq = (buf[2] << 8) | buf[3]

    # Step 2 — ClearIrqStatus immediately (before any further reads)
    await cmd(0x02, b'\xFF\xFF')

    crc_err = bool(irq & 0x0040)    # bit 6

    # Step 3 — GetRxBufferStatus (opcode 0x13): status + payloadLen + rxStartAddr
    buf3 = bytearray(4)
    nss(0); spi.write_readinto(b'\x13\x00\x00\x00', buf3); nss(1)
    await wait_busy("GetRxBufferStatus")
    payload_len   = buf3[2]
    rx_start_addr = buf3[3]

    # Step 4 — GetPacketStatus (opcode 0x14): status + rssiPkt + snrPkt + signalRssiPkt
    buf4 = bytearray(5)
    nss(0); spi.write_readinto(b'\x14\x00\x00\x00\x00', buf4); nss(1)
    await wait_busy("GetPacketStatus")
    pkt_last_rssi = -(buf4[2] >> 1)         # rssiPkt: -value/2 dBm
    pkt_last_snr  = buf4[3] if buf4[3] < 128 else buf4[3] - 256  # signed int8, /4 for dB
    pkt_last_snr  = pkt_last_snr >> 2       # convert to whole dB (truncate)

    # Step 5 — ReadBuffer (opcode 0x1E): NOP byte + start addr + data
    if crc_err or payload_len < 4:
        print("PKT: crc_err=%s len=%d — skip" % (crc_err, payload_len))
        return

    rbuf = bytearray(2 + payload_len)
    nss(0)
    spi.write(bytes([0x1E, rx_start_addr]))
    spi.readinto(rbuf)
    nss(1)
    await wait_busy("ReadBuffer")
    # First byte of rbuf is NOP status; payload starts at rbuf[1]
    # Actually ReadBuffer: send [0x1E, offset, NOP], read payload bytes
    # Correct sequence: write [0x1E, offset], then read 1 status + N data bytes

    payload = rbuf[1:1 + payload_len]
    counter = int.from_bytes(payload[:4], 'big')

    # Step 6 — Update miss tracking
    if pkt_counter_last is None:
        delta = 1
    else:
        delta = (counter - pkt_counter_last) & 0xFFFFFFFF
        if delta == 0:
            delta = 1   # duplicate (shouldn't happen with TX); treat as no-miss
        pkt_miss_count += max(0, delta - 1)

    pkt_counter_last = counter
    pkt_last_rx_ms   = time.ticks_ms()
    print("PKT #%d rssi=%d snr=%d miss=%d" %
          (counter, pkt_last_rssi, pkt_last_snr, pkt_miss_count))
```

> **IRQ clear order**: clear BEFORE ReadBuffer, not after. If you clear after, a second
> packet arriving during the read will assert DIO1 again and the clear wipes it — you
> lose that RxDone. Clearing early means the chip re-asserts DIO1 for the next packet
> normally. The continuous RX mode re-arms automatically; no `SetRx` call needed.

> **ReadBuffer SPI sequence**: `[0x1E, offset]` written, then read `(1 status NOP + N data)` bytes.
> The first returned byte during the `0x1E` clock is a NOP/status — skip it.
> Adjust rbuf indexing accordingly (see implementer note in step 5 above).

---

## 5. PKT Render

```python
def render_pkt():
    oled.fb.fill(0)
    if pkt_counter_last is None:
        oled.fb.text("PKT 868.1 MHz",    0,  0, 1)
        oled.fb.text("awaiting...",      0, 11, 1)
        # line 2 blank
    else:
        now_ms = time.ticks_ms()
        age_ms = time.ticks_diff(now_ms, pkt_last_rx_ms)
        delta  = 1 if pkt_counter_last is None else 1  # shown after first pkt only
        # Header
        hdr = "PKT #%d %ddBm" % (pkt_counter_last, pkt_last_rssi)
        oled.fb.text(hdr[:16],           0,  0, 1)
        # Line 1: seq delta + miss
        seq_line = "seq%d miss%d" % (
            1,              # always 1 unless we track per-render delta separately
            pkt_miss_count
        )
        oled.fb.text(seq_line[:16],      0, 11, 1)
        # Line 2: age + SNR
        age_line = "last%dms SNR%d" % (age_ms, pkt_last_snr)
        oled.fb.text(age_line[:16],      0, 22, 1)
    oled.show()
```

> **`seq` field**: the TX sends packets 1 Hz; `seq` = last received delta (counter minus
> previous counter). Store `pkt_last_delta` in `pkt_rx_once` and display it in `render_pkt`.
> Add `pkt_last_delta = 1` to the global state block and update it inside `pkt_rx_once`.

---

## 6. PKT Task (new coroutine, runs alongside sweep_task)

```python
async def pkt_task():
    while True:
        if app_mode != 'PKT':
            await asyncio.sleep_ms(50)
            continue
        # Poll DIO1 every 5 ms — avoids busy spin, yields USB CDC
        if dio1.value():
            await pkt_rx_once()
            await render_pkt_async()    # thin wrapper: fill+show+yield
        else:
            await asyncio.sleep_ms(5)
```

```python
async def render_pkt_async():
    render_pkt()
    await asyncio.sleep_ms(0)   # yield to scheduler
```

---

## 7. Sweep Task — Gate on Mode

Add a mode check at the top of `sweep_task`:

```python
async def sweep_task():
    while True:
        if app_mode == 'PKT':
            await asyncio.sleep_ms(100)   # idle — pkt_task owns the chip
            continue
        if not paused:
            await sweep_once()
            ...
```

---

## 8. Modified `render()` (dispatcher)

```python
async def render():
    oled.fb.fill(0)
    if app_mode == 'PKT':
        render_pkt()
    elif mode == 0: render_bar()
    elif mode == 1: render_num()
    else:           render_wf()
    oled.show()
    await asyncio.sleep_ms(0)
```

---

## 9. Boot — add `pkt_task` to task list

```python
async def main():
    await render()
    await asyncio.sleep_ms(2000)
    rf_en(1)
    await asyncio.sleep_ms(50)
    await chip_reset()
    await radio_init()
    asyncio.create_task(button_task())
    asyncio.create_task(pkt_task())    # NEW
    await sweep_task()                 # runs forever; pkt_task runs concurrently
```

---

## 10. Modem Parameter Cross-Check

| Parameter       | TX spec (TX_LORA_PKT_SPEC §3) | RX config here     | Match |
|-----------------|-------------------------------|--------------------|-------|
| Frequency       | 868.1 MHz (0x3641999A)        | same fword         | yes   |
| SF              | 0x07 (SF7)                    | 0x07               | yes   |
| BW              | 0x04 (125 kHz)                | 0x04               | yes   |
| CR              | 0x01 (4/5)                    | 0x01               | yes   |
| LDRO            | 0x00                          | 0x00               | yes   |
| Preamble        | 8 symbols                     | 8 (0x0008)         | yes   |
| Header type     | 0x00 (explicit)               | 0x00               | yes   |
| CRC             | 0x01 (on)                     | 0x01               | yes   |
| IQ              | 0x00 (standard)               | 0x00               | yes   |
| Sync word       | 0x3444 (public)               | 0x0740=0x34, 0x0741=0x44 | yes |
| Payload length  | 4 bytes (counter)             | 0xFF (any) in RX   | yes   |

---

## 11. Acceptance Gates

| Gate | Test | Pass condition |
|------|------|----------------|
| A | Boot, no press | SCAN mode title visible; sweep runs; no change to existing behavior |
| B | Long-press | Title switches to `PKT 868.1 MHz` + `awaiting...`; chip stays at 868.1 continuous RX |
| C | TX at 1 Hz | `PKT #N -46dBm` updates ~1 Hz; `seq1 miss0` continuously; `last Xms ago` cycles 0–1000 |
| D | Stop TX, wait >5 s | Last received values still displayed; `last 5000+ms ago` keeps growing |
| E | Long-press again | Returns to SCAN; sweep resumes with existing channel data |

---

## 12. Tricky Points Summary

1. **Sync word write order**: MSB (0x34) to 0x0740 first, LSB (0x44) to 0x0741 second.  
   Reversed order leaves chip with wrong sync word — packets are silently missed.

2. **IRQ clear before ReadBuffer**: clear IRQ status (opcode 0x02) before reading buffer  
   so a back-to-back packet arriving during the read still sets DIO1 correctly.

3. **`app_mode` flip after SPI ops**: flip `app_mode = 'PKT'` only after all init commands  
   complete, or `sweep_task` may issue a frequency hop mid-transition.

4. **Continuous RX auto-rearms**: after RxDone in continuous mode (`SetRx 0xFFFFFF`), the  
   chip returns to RX automatically — no `SetRx` call needed per packet.

5. **ReadBuffer first byte**: the byte clocked out during the `0x1E` opcode transfer is a  
   status NOP — skip index [0], payload begins at index [1] of the read buffer.

6. **SNR sign**: `snrPkt` from GetPacketStatus is a signed int8 in units of 0.25 dB.  
   Convert: `snr = byte - 256 if byte >= 128 else byte`, then `snr //= 4` for whole dB.

7. **DIO1 pin shared with TX demo**: TX uses P015 for TxDone; RX uses the same pin for  
   RxDone. No conflict — they run on separate boards. No wiring change needed.
