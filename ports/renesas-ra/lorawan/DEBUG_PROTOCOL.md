# LoRaWAN Adapter Debug Protocol — VK_RA4M2 + Wio-SX1262

**Date:** 2026-05-09
**Status:** Open — root cause not found
**Scope:** OTAA join failure for `class-C-demo` device through MicroPython LoRaMac C adapter.

---

## 1. Goal

OTAA join + Class C downlink demo working end-to-end against local infrastructure:
- ChirpStack 4.17.0 server
- SenseCAP M2 BasicStation gateway
- VK_RA4M2 board + Wio-SX1262 daughterboard

`mac.is_joined() == True` within 12s, repeatable across power-cycles, no compensation constants in fix.

---

## 2. Infrastructure (verified 2026-05-09)

| Service | Address | Notes |
|---|---|---|
| ChirpStack server | `192.168.2.192:8080` | Web UI |
| Grafana dashboard | `192.168.2.192:3000` | anonymous access |
| MQTT broker | `192.168.2.192:1883` | mosquitto |
| SenseCAP M2 gateway | `192.168.2.195` | MAC `2c:f7:f1:1c:e6:a8`, gateway_id `2cf7f110801003cb` |
| Linux server SSH | `vkrz@192.168.2.192` (`vkrzg2lc`) | host key SHA256:BMG0U+ohV23QgKDjh6WCIn9gbUvNyLeic44prvT9w2U |
| Board REPL | `COM34` | mpremote via PowerShell |

Network has migrated multiple times during session. Verify before each use.

---

## 3. Build invocation (CRITICAL — defaults are wrong)

```bash
make BOARD=VK_RA4M2 \
     BUILD=build-VK_RA4M2-r4 \
     MICROPY_HW_LORA_STACK=renesas \
     LORAWAN_BUILD_PHASE=7 \
     -j8
```

Plus environment: `MSYSTEM=MINGW64`, `CHERE_INVOKING=1` for MSYS2 bash.

Default `make BOARD=VK_RA4M2` → frozen Python LoRa stack (legacy micropySX126X) — `import lorawan` will fail.

Output: `ports/renesas-ra/build-VK_RA4M2-r4/firmware.hex`. Flash from this dir.

---

## 4. Definitive observations

### 4.1 Server + gateway side: 100% clean

- JoinRequest received by ChirpStack ✓
- JoinAccept generated, MIC validated ✓
- Sent to gateway via MQTT ✓
- BasicStation `dntxed` confirms RF emission ✓
- Decoded protobuf shows correct freq+SF+BW for both RX1 (868.3MHz/SF7) and RX2 (869.525MHz/SF12) at correct timing

### 4.2 Board side: chip never demodulates

- `mlme_confirm 3` (RX1_TIMEOUT) only — no `mlme_confirm 4` (RX2_TIMEOUT)
- RSSI register reads -127 dBm (noise floor) — chip never received signal

### 4.3 Direct Python SPI works perfectly

`rssi_scanner.py` reference (`machine.SPI(3)` + manual NSS toggle):
- Chip enters RX mode (status `0xD2`)
- RSSI register reads valid varying values (-30 to -127 dBm)
- DIO1 pulses on real LoRa packets nearby (confirmed by user with scope)
- TCXO/XOSC startup succeeds, errs=0x0000

### 4.4 Our C adapter fails identically same hardware

After `mac.lorawan_init()` + `mac.scan_set_*` chain:
```
status: 0x2a     mode=STBY_RC, cmd=4 (proc error)  — after init
status: 0x22     mode=STBY_RC, cmd=1 (OK)          — after SetChannel
status: 0x22     mode=STBY_RC, cmd=1 (OK)          — after SetRxConfig
status: 0x2A     mode=STBY_RC, cmd=5 (FAILURE)     — after SetRx
errs:   0x0020   XoscStart bit set
```

Chip refuses to enter RX/FS/TX mode. Subsequent SPI commands silently timeout (BUSY stuck high) — scope shows ~3-4 sec SPI activity then nothing.

### 4.5 Both paths use same SPI driver

`machine.SPI(3).write/write_readinto` and our `sx126x_spi_xfer` both call `ra_sci_spi_transfer()` in `ra/ra_sci_spi.c:593`. Confirmed by grep.

---

## 5. Modifications applied to source tree

### 5.1 KEPT modifications (currently in code)

| File | Change | Reason |
|---|---|---|
| `lorawan/glue/sx126x_board.c::sx126x_spi_xfer` | Static buffer assembly + single `ra_sci_spi_transfer` call (block streaming) | Replace per-byte loop with DTC bulk transfer (S-2) |
| `lorawan/glue/sx126x_board.c::sx126x_spi_xfer` | Re-entrancy guard `s_spi_xfer_busy` flag + reset on all return paths | Prevent nested SPI corruption |
| `lorawan/glue/sx126x_board.c::SX126xIoTcxoInit` | `SetRegulatorMode(LDO)` before TCXO setup, 10ms delay after TCXO ctrl, 5ms after Calibrate, ClearDeviceErrors at end | Match Python `rssi_scanner.py` proven order |
| `lorawan/mod_lorawan.c::mac_process_notify` | No-op (was: `mp_sched_schedule_node`) | Eliminate auto-dispatch hazard during SPI |
| `lorawan/mod_lorawan.c` | Added bindings: `scan_standby`, `scan_set_freq`, `scan_set_lora_rx`, `scan_rx_continuous`, `scan_set_rx_raw`, `scan_rssi`, `scan_get_errors`, `disable_dio1_irq` | Adapter diagnostic surface |
| `ra/ra_sci_spi.c::ra_gpio_config` | SCK/MOSI/MISO drive `GPIO_LOW_POWER` → `GPIO_HIGH_POWER` | Improved signal integrity at 12.5 MHz SCK |
| `ra/ra_sci_spi.c::ra_sci_spi_calc_baud` | Empirical formula `B = PCLK/(2×4^N×(BRR+2))` with MDDR fine-tune | **Empirical, NOT validated against datasheet** — see §6 |
| `lorawan/radio/radio.c` | UNCHANGED — pristine Renesas | Per NFR-2 |
| `lorawan/radio/sx126x.c` | EXISTING `RadioSetModem` PATCH preserved (sync word re-apply) | Pre-existing legacy patch |

### 5.2 REVERTED modifications (tried, didn't help, undone)

- `lorawan/mac/LoRaMac.h:3181` — RXWIN macro `Radio.SetPublicNetwork()` add. Reverted (added 360ms delay without functional benefit)
- `lorawan/mac/LoRaMac.c:3575` — `Radio.SetPublicNetwork()` after RegionRxConfig. Reverted (no impact on join)
- `lorawan/glue/timer_board.c` S-1 (direct callback in agt_tick_isr). **REVERTED — caused TimerStop reentrancy crash via `OnAckTimeoutTimerEvent`.** This callback isn't flag-only.
- `lorawan/glue/sx126x_board.c::sx126x_spi_xfer` — read-path padding NOPs (1 NOP, 2 NOP). Reverted — neither matched scope.
- `lorawan/glue/sx126x_board.c::sx126x_spi_xfer` — status byte at `s_rx_buf[0]` for writes. Reverted (broke everything — returns 0xFF for all reads).
- `lorawan/glue/sx126x_board.c::sx126x_phase1_get_status` — BUSY poll before cs_low. Reverted (broke status reads).
- Removing `mp_handle_pending` from BUSY-polls. Reverted (BUSY stuck → SPI never fires).
- TCXO init order ClearDeviceErrors **before** Calibrate (per Renesas RA2E1 reference). Reverted — Python reference does it after, scope confirms.

---

## 6. Open issues

### 6.1 SCK rate mismatch with datasheet — UNRESOLVED

Per RA4M2 datasheet Table 27.6:
```
B = PCLK / (8 × 2^(2n-1) × (BRR+1))     for clock-synchronous simple SPI
B = PCLK / (4 × (BRR+1))                  for n=0 (CKS=00)
```

PCLK = 50 MHz (board override via `MICROPY_HW_MCU_PCLK`).

Scope readings vs datasheet:

| Configured baud | BRR (computed) | Datasheet expected | Scope measured | Δ |
|---|---|---|---|---|
| 8 MHz (OLD code) | 0 | 12.5 MHz | 12.5 MHz | ✓ |
| 4 MHz (OLD code) | 2 | 4.17 MHz | 6.25 MHz | **1.5×** |
| 4 MHz (NEW +2 fix) | 3 | 3.125 MHz | 5 MHz | **1.6×** |
| 1 MHz (NEW +2 fix) | 23 | 0.521 MHz | 0.833 MHz | **1.6×** |

Only BRR=0 matches datasheet. BRR>0 systematically reads ~1.6× higher than predicted. **Cannot derive consistent formula.** Possibilities:
- Missing register configuration (some ABCS/BGDM/SPMR bit affecting CSPI rate that datasheet doesn't list)
- SCI peripheral has undocumented behavior
- Scope frequency counter has systematic error at low BRR
- **Need:** register dump (read BRR/CKS/MDDR/SEMR.BRME after init) to ground-truth what hardware sees

### 6.2 XoscStart error specifically at SetRx — ROOT CAUSE UNRESOLVED

After successful `lorawan_init()` (status 0x2A) + `Radio.SetChannel()` (OK) + `Radio.SetRxConfig()` (OK) — chip status is 0x22 (STBY_RC, OK, no errors).

But `Radio.Rx(0)` (or `mac.scan_set_rx_raw(0xFFFFFF)`) → cmd_status=5 (FAILURE TO EXECUTE), errs=0x0020 (XoscStart).

Calibrate during init succeeded (uses XOSC). So XOSC works at init time. But fails at SetRx time.

**Some command between init-time Calibrate and SetRx leaves chip in a state where TCXO/XOSC re-startup fails.** Investigation paths tried:
- TCXO STAB_TIME (5ms) — same as Python ref, can't be issue
- Regulator mode order — moved before TCXO, no effect
- ClearDeviceErrors order — both before-Calibrate and after-Calibrate tried
- DIO1 IRQ re-entrancy — eliminated via guard + no-op auto-dispatch — no effect

### 6.3 DIO1 stays 0V in adapter test (CONSEQUENCE of 6.2, not cause)

Chip never enters RX → no events → no DIO1 pulse. In `rssi_scanner.py` (working) DIO1 DOES pulse on actual LoRa packets — confirms IRQ chain works at hardware level.

---

## 7. Hypotheses tried (all DISPROVEN or INCONCLUSIVE)

| Hypothesis | Test | Outcome |
|---|---|---|
| Sync word fix (PUBLIC vs PRIVATE) | LoRaMac.h:3181 macro patch | No effect on join |
| Sync word in RxWindowSetup | LoRaMac.c:3575 SetPublicNetwork add | No effect, +180ms tax (reverted) |
| RX1 timing latency from Python polling | S-1 hard ISR direct callback | Crashed via OnAckTimeoutTimerEvent reentrancy (reverted) |
| SPI byte-by-byte → block streaming | sx126x_spi_xfer refactor (S-2) | Different failure mode (TX_TIMEOUT instead of RX_TIMEOUT) |
| Multi-byte read alignment | Padding NOPs in read path | Different junk values, no improvement |
| Status byte at s_rx_buf[0] | Architect-suggested fix | Returns all 0xFF, broke chip access (reverted) |
| BUSY-poll order before CS | Move BUSY check before cs_low | Status reads broke (reverted) |
| Re-entrancy from DIO1 IRQ | Re-entrancy guard added | Helped logically but didn't fix XoscStart |
| Re-entrancy from auto-dispatch | mac_process_notify → no-op | Same XoscStart symptom |
| SCK rate too high | GPIO HIGH_POWER drive | Made XoscStart intermittent (sometimes 0x0000) |
| TCXO init order | Calibrate ↔ ClearDeviceErrors swap | No fundamental change |
| Baud rate over-target | calc_baud fix attempts | Reduced 1.5625× → 1.25× → 1.6× — NOT exact |
| 1-byte DTC race for zero-payload writes | Architect hypothesis | Not applicable to scan_set_freq (4-byte payload) |
| MDDR clamping | Various MDDR formulas | None matched scope at BRR>0 |

---

## 8. CRITICAL data points to keep in mind

1. **Same hardware, same SPI driver (`ra_sci_spi_transfer`), different chip behavior** depending on caller (Python direct works, adapter doesn't). The difference must be in:
   - Buffer composition (memcpy → static buffer)
   - CS/BUSY handling timing (pre-CS BUSY-poll with mp_handle_pending)
   - State machine of LoRaMac stack on top
   - Or some register state we're not aware of

2. **Scope shows 1.6× over-target SCK** at all BRR>0 — driver doesn't honor own config. Whether this matters for chip operation is unclear.

3. **GetStatus (1-byte exchange) ALWAYS works** consistently across all configurations. Multi-byte commands appear to be received by chip (status byte returns OK) but their EFFECT doesn't always persist (e.g. SetDio3AsTcxoCtrl).

4. **Renesas reference `sx126x-board.c`** at `samples/project/src/boards/ra2e1fpb_sx126x/` differs from ours — they use FSP `R_SCI_SPI_Open`/`R_SCI_SPI_Write` directly. Our port uses `ra_sci_spi` (custom MicroPython port driver around SCI). Per NFR-2 and user mandate, we don't switch to FSP r_sci_spi.

---

## 9. Recommended next steps (for next session)

1. **Diagnose register state directly** — add binding `mac.dbg_sci_regs()` that reads `BRR`, `SMR.CKS`, `MDDR`, `SEMR.BRME`, `SPMR` after init and dumps them. Compare with `calc_baud` output. Settles §6.1.

2. **Trace TCXO config persistence** — add binding to read SX126x register 0x0911-0x0913 (TCXO control regs per chip datasheet) after each command in init+config chain. Identify which command corrupts. Settles §6.2.

3. **STDBY_XOSC discriminator** — add binding `mac.scan_set_standby_xosc()` calling `SX126xSetStandby(STDBY_XOSC)`. After init, before SetRx, force chip to STDBY_XOSC and read errs. If XoscStart appears here → TCXO/XOSC zone is the problem. If clean → mode transition into RX is broken.

4. **Revert ra_sci_spi_calc_baud to OLD (buggy) formula** — at BRR=0 (8 MHz config), scope showed 12.5 MHz and chip was responsive enough to pass init. Maybe 12.5 MHz works around chip's tolerance windows in a way 5 MHz doesn't. Quick A/B test.

5. **Side-by-side comparison test** — run Python direct init (rssi_scanner.py pattern through `dbg_xchg`/raw SPI), then call `mac.scan_set_rx_raw(0xFFFFFF)` through adapter. If chip in good state from Python init, does adapter SetRx succeed? Determines if bug is config commands OR final SetRx.

---

## 10. Constraint reminders (per user mandate)

- **NO compensation constants** (timing offsets, magic numbers, "safety margins")
- **NO bypass workarounds** (don't wrap around the bug; find root cause)
- **NO hardware blame** — VK_RA4M2 + Wio-SX1262 verified working with Python direct
- **NO FSP r_sci_spi switch** — stay within ra_sci_spi-based architecture
- **NO modifications** to `lorawan/mac/`, `lorawan/radio/` (Renesas pristine)
- **READ datasheet, don't speculate** — empirical formulas without spec backing are forbidden

---

## 11. Files to reference

- This protocol: `ports/renesas-ra/lorawan/DEBUG_PROTOCOL.md`
- Port requirements: `ports/renesas-ra/lorawan/PORT_REQUIREMENTS.md`
- RA4M2 datasheet: `ports/renesas-ra/boards/VK_RA4M2/r01uh0892ej0140-ra4m2.pdf` (Section 27 = SCI)
- Working Python reference: `ports/renesas-ra/boards/VK_RA4M2/examples/LoRa/lorawan_upstream/renesas_lora/demos/rssi_scanner/rssi_scanner.py`
- Adapter under debug: `ports/renesas-ra/lorawan/glue/sx126x_board.c`
- SPI driver: `ports/renesas-ra/ra/ra_sci_spi.c`
- Board glue: `ports/renesas-ra/lorawan/glue/timer_board.c`
- Tests created during session:
  - `boards/VK_RA4M2/examples/LoRa/.../demos/rssi_scanner/rssi_via_adapter.py` (adapter scan)
  - `boards/VK_RA4M2/examples/LoRa/.../demos/rssi_scanner/repl_chip_diag.py` (direct Python diag)
  - `boards/VK_RA4M2/examples/LoRa/.../demos/rssi_scanner/repl_rssi_live.py` (live RSSI monitor)
  - `Desktop/temp1/spi_burst_*b.py` (1/2/3/4 byte SPI burst for scope)

---

## 12. Memory references

Saved feedback in `~/.claude/projects/.../memory/`:
- `feedback_no_initiatives.md` — wait for explicit instructions
- `feedback_no_hw_blame.md` — hardware verified OK
- `feedback_no_workarounds.md` — architectural fixes only, no compensation
- `reference_lorawan_build.md` — exact build invocation
- `reference_lorawan_infra.md` — server/gateway addresses

---

**Session ended without root cause identified. Protocol exists for handoff or next-session continuation.**
