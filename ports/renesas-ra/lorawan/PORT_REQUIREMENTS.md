# LoRaWAN Port Requirements — VK_RA4M2 + Wio-SX1262

**Version:** 1.0
**Date:** 2026-05-09
**Status:** Approved for implementation
**Scope:** `ports/renesas-ra/lorawan/glue/` only

---

## 1. Purpose

Define functional and non-functional requirements for the renesas-ra port of LoRaMac-node so that OTAA Class A/C operation works against the local ChirpStack 4.17 + SenseCAP M2 (BasicStation) infrastructure with deterministic, repeatable success.

Constrains the work to the **glue/porting layer**. Upstream LoRaMac stack code is treated as read-only.

---

## 2. Functional Requirements

### FR-1 — OTAA Join Succeeds

**Statement:** `mac.join()` produces `mac.is_joined() == True` within 12 seconds for a properly provisioned EU868 device.

**Verification:** 5/5 consecutive runs pass after `mac.nvm_factory_reset()`. Test fixture `LORAWAN_TESTS/join_test.py`.

### FR-2 — Single MlmeConfirm Status

**Statement:** Successful join emits exactly one `('mlme_confirm', 0)` event. No `mlme_confirm 3` (RX1_TIMEOUT) precedes it under nominal RF conditions.

**Verification:** Event log inspection.

### FR-3 — Class C Downlink Latency

**Statement:** After successful join + `mac.set_class('C')`, server-initiated downlinks reach the device with end-to-end latency < 1.5s (server tx queue → board `mac.recv()`).

**Verification:** ChirpStack REST queue → board callback timestamp diff. 10/10 downlinks pass.

### FR-4 — Repeatability

**Statement:** Across power-cycle and `nvm_factory_reset` boundaries, FR-1 through FR-3 hold without manual intervention or parameter tuning.

---

## 3. Non-Functional Requirements

### NFR-1 — No Calibration Constants

**Statement:** Solution shall not introduce magic-number compensation (timing offsets, "safety margins", `chain_latency_ms` constants, `WindowOffset` overrides). The fix must be architectural, not tuned.

**Rationale:** Constants break across SF/BW changes, polling cadence variations, and clock-source drift. Fragility is unacceptable.

### NFR-2 — Single-File-Class Touch

**Statement:** Modifications restricted to `lorawan/glue/`. The following are **read-only**:
- `lorawan/mac/LoRaMac.c`, `lorawan/mac/LoRaMac.h`
- `lorawan/radio/radio.c`, `lorawan/radio/sx126x.c`
- `lorawan/region/RegionEU868.c` (and all Region\*.c)
- `lib/fsp/` submodule

### NFR-3 — Hardware Untouched

**Statement:** No hardware modification. Existing VK_RA4M2 + Wio-SX1262 daughter board confirmed operational.

### NFR-4 — Upstream Compatibility

**Statement:** Glue layer shall conform to the upstream LoRaMac-node `BoardSupport` API (`TimerEvent_t`, `Radio_s`, `SX126xHal*`). No upstream API changes.

### NFR-5 — Python ABI Stability

**Statement:** No change to `lorawan.Mac` Python-facing API. Existing user code using `mac.join`, `mac.set_class`, `mac.send`, `mac.recv` continues to work unchanged.

---

## 4. Constraints

### C-1 — Existing Architecture (Current State)

Timer dispatch in `glue/timer_board.c` uses `mp_sched_schedule_node` for callback deferral. Effect: HW IRQ → Python scheduler → flag set → next `mac.process()` call → RxWindowSetup. End-to-end latency ~67 ms (vs ~5 ms target).

SPI commands to SX1262 in `glue/sx126x_board.c` use byte-by-byte `ra_sci_spi_transfer`. A typical `SetRxConfig` (~10 chip commands) costs ~30 ms. Per-byte FSP overhead dominates.

### C-2 — LoRaWAN RX Window Tolerance

EU868 RX1 at +5.000 s, RX2 at +6.000 s after JoinRequest TX-done. Gateway preamble at SF7/BW125 is 8.2 ms wide. Board RX window must overlap preamble.

### C-3 — MicroPython Scheduler Semantics

`mp_handle_pending` runs at bytecode boundaries, sleep checkpoints, and inside HAL delay loops. Latency from `mp_sched_schedule_node` to dispatched callback is non-zero and non-deterministic (0..N ms).

---

## 5. Architecture Decisions

### AD-1 — Hard ISR Timer Dispatch

Timer ISR (`agt_tick_isr`, `agt5_oneshot_isr`) shall invoke `evt->Callback()` directly, removing the `dispatch_post → mp_sched_schedule_node → timer_dispatch_cb` indirection.

**Justification:** The LoRaMac timer callbacks `OnRxWindow1TimerEvent` (LoRaMac.c:2128) and `OnRxWindow2TimerEvent` (LoRaMac.c:2153) write a single `volatile` flag word. ISR-safe by inspection. Indirection adds ~5 ms with zero functional benefit.

### AD-2 — ISR-Driven MAC Process Trigger

After timer-event flag is set in ISR, dispatch a single `mp_sched_schedule_node` to invoke `LoRaMacProcess()`. This drives the MAC state machine without dependency on Python `mac.process()` polling cadence.

**Justification:** Removes the 0..20 ms Python polling latency from the critical path. Python polling becomes a watchdog/keepalive, not the timing source.

### AD-3 — Block-Streamed SPI for SX1262 Commands (atomic DMA burst)

**Principle:** *One SPI transfer = one DMA burst.* SX1262 commands have a fixed format (1 opcode + N param bytes). They shall be issued as an atomic block transaction.

**Pattern:**

```c
sx126x_spi_xfer(buf, len) {
    wait_busy_low();              // single check before CS
    nss_low();
    fsp_spi_write_dma(buf, len);  // single DMAC transfer; CPU free during clock-out
    nss_high();
    wait_busy_low();              // single check after CS (skip for SET_SLEEP-class)
}
```

**Forbidden in hot path:**
- Per-byte SPI ops (`spi_write_one_byte` loops)
- `interbyte_us` delays (legacy debug knob — default 0; declared deprecated in production)
- `mp_handle_pending(true)` — ISR/PendSV context cannot yield to MicroPython VM

**Justification:** For a 9-byte `SetRxConfig` command at 8 MHz SCK:
- Theoretical SPI clock-out: 9 µs
- DMA setup overhead: ~1 µs
- Total: ~10 µs per command (vs current ~ms-class with byte-loop + interbyte + handle_pending)

Per-command cost drops from ~3 ms to **~10–50 µs** (oscilloscope-measured CS-edge to CS-edge). Critical-path SPI cost in `RxWindowSetup` drops from ~30 ms to **~150 µs total** (Standby + SetPacketType + SetModulationParams + SetPacketParams + SetRxConfig + Rx).

### AD-4 — Reuse Existing DMAC Infrastructure

`glue/dma_board.c` already provides DMAC channel reservation/management primitives for this port. AD-3 shall use this API.

- DMAC channel(s) for SX1262 SPI3 TX/RX statically assigned at `radio_init`.
- ELC link from SCI3 RX request to DMAC trigger (rx-direction reads).
- Channel allocation visible in build `.map`; verified no collision with DAC, AudioADC, or other peripherals on VK_RA4M2.

### AD-5 — DMAC Completion via Flag, Not VM Yield

DMAC_END IRQ sets a completion flag. PendSV pump (or ISR-side wait) reads the flag and proceeds. No `mp_sched_schedule_node` call between SPI fire and SPI completion — the path stays in NVIC priority space.

**Justification:** If SPI completes before the next LoRaMac action would otherwise occur, zero extra latency. The Python VM is *never* on the timing-critical path.

---

## 6. Implementation Scope

### S-1 — `glue/timer_board.c`

- Replace `dispatch_post(e)` with direct `e->Callback()` in `agt_tick_isr` and `agt5_oneshot_isr`.
- Add `s_macproc_node` static `mp_sched_node_t`.
- Add `lora_mac_process_dispatch(mp_sched_node_t *)` static function calling `LoRaMacProcess()`.
- After timer-list walk in ISR, schedule the macproc node iff `LoRaMacTimerEvents.Events.Value != 0`.

### S-2 — `glue/sx126x_board.c`

- Refactor `SX126xWriteCommand_e`, `SX126xReadCommand`, `SX126xWriteRegister`, `SX126xReadRegister` to issue **one** DMAC transfer per opcode payload.
- Acquire DMAC TX channel (and RX channel for read commands) at `radio_init` time; release on deinit.
- Bidirectional commands use TX dummy / RX capture pattern (DMAC fed dummy bytes for clocking, RX DMAC captures incoming).
- Preserve existing BUSY pin polling guard before each transaction (exactly **2** checks per transfer: pre-CS-low and post-CS-high; skipped post-CS for SET_SLEEP-class commands).
- Remove all `mp_handle_pending(true)` calls from SPI hot path (legacy `interbyte_us` becomes deprecated debug knob, defaults to 0).
- DMAC_END IRQ sets a single completion flag; pump waits on flag, no VM yield.

### S-3 — Verification fixture

- Reuse `LORAWAN_TESTS/join_test.py` as primary acceptance test.
- Add bounded-loop variant `LORAWAN_TESTS/join_repeatability.py` running FR-4 (5 consecutive `nvm_factory_reset` + join cycles).

---

## 7. Test Plan

| Test | Source | Pass Criteria |
|---|---|---|
| T-1: Single OTAA join | `join_test.py` | `is_joined=True`, events `[(_, 0)]`, elapsed < 12s |
| T-2: Repeatability | `join_repeatability.py` | T-1 passes 5/5 |
| T-3: Class C downlink latency | `testH_classc_hardcoded.py` + ChirpStack REST queue | latency < 1.5s, 10/10 |
| T-4: No regression — Class A uplink | `test1_confirmed.py` | confirmed uplink → ack within 12s |
| T-5: ISR safety — long Python computation | Custom: queue 5s of pure-Python work, then check timer accuracy | RX windows still hit; no missed flags |
| T-6: SPI command cost (oscilloscope) | CS_falling → CS_rising for `SetRxConfig` | < 50 µs |
| T-7: RxWindowSetup total time | trace from RxWindowTimer1 fire to chip-RX-active | < 200 µs |
| T-8: RX1 timing accuracy | board RX-active timestamp − expected +5000 ms | within ±2 ms (stretch: ±0.5 ms) |
| T-9: Hot-path source check | `grep -c 'interbyte\|mp_handle_pending' lorawan/glue/sx126x_board.c` (excluding init) | == 0 |
| T-10: DMAC channel collision | `.map` inspection + concurrent run with DAC + AudioADC tests | no allocation conflict |

All tests run on physical VK_RA4M2 board on COM34 against local ChirpStack 4.17 (192.168.1.188) via SenseCAP M2 (192.168.1.187).

---

## 8. Out of Scope

- Class B beacon acquisition (deferred — requires bridge upgrade or Semtech UDP swap)
- FUOTA / multicast support
- Sub-millisecond RX1 timing (35-symbol preamble margin already adequate)
- Region tuning beyond defaults
- LoRaMac.c modifications including `LORAMAC_RADIOWAKEUP_RXWIN` macro patch (rejected — duplicate path; `radio.c:RadioSetModem` PATCH already covers sync re-apply)
- SPI driver replacement beyond glue layer (FSP `r_sci_spi`/`r_spi_b` selection deferred)
- Audio subsystem regressions on RA6M5 (separate effort)

---

## 9. Risks & Mitigations

| ID | Risk | Mitigation |
|---|---|---|
| R-1 | Hard-ISR `e->Callback()` calls a callback that allocates or calls Python | Audit: only `OnRxWindow1TimerEvent`, `OnRxWindow2TimerEvent`, `OnAckTimeoutTimerEvent`, `OnRetransmitTimeoutTimerEvent`, `OnTxDelayedTimerEvent` are valid timer callbacks. All inspected: flag-only. |
| R-2 | DMAC channel collision with audio/SPI master in user code | Reserve DMAC channels at `radio_init` time; document channel IDs in `dma_board.h`. Block conflicting allocation. |
| R-3 | DMAC_END IRQ priority inversion blocks AGT4 1 kHz tick | Configure DMAC_END at lower NVIC priority than AGT4. AGT4 must preempt. |
| R-4 | `LoRaMacProcess()` is not re-entrant | Add `s_in_lmprocess` guard in `lora_mac_process_dispatch` — drop nested invocation. |
| R-5 | Build breaks on RA6M5 sibling port (VK_RA6M5) | Glue files are board-conditional via `MICROPY_HW_LORA_STACK_RENESAS`. Verify RA6M5 build still compiles even if not exercised. |

---

## 10. Acceptance Sign-off

- [ ] FR-1 (OTAA join 5/5)
- [ ] FR-2 (mlme_confirm 0 only)
- [ ] FR-3 (Class C downlink < 1.5s)
- [ ] FR-4 (repeatability across power cycle)
- [ ] NFR-1 (no calibration constants in diff)
- [ ] NFR-2 (diff confined to `glue/`)
- [ ] T-1 .. T-6 pass on COM34 board

---

## 11. References

- `lorawan/glue/timer_board.c` — timer dispatch architecture
- `lorawan/glue/sx126x_board.c` — radio HAL
- `lorawan/glue/dma_board.c` — DMAC primitives
- `lorawan/mac/LoRaMac.c:2126-2154` — timer callback definitions (read-only)
- `LORAWAN_TESTS/join_test.py` — acceptance fixture
- ChirpStack server: `192.168.1.188`
- Gateway: `192.168.1.187` (SenseCAP M2)
- Test device: `class-C-demo`, DevEUI `70b3d57ed0070003`
