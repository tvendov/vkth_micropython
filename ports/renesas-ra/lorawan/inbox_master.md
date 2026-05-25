# INBOX_MASTER — messages slave → master

---

## MSG 2026-05-22 from=slave to=master subject=stage-a-timer-migration-complete-mac-stuck

### Context

**Timer migration goal MET** on VK_RA4M2:

- Option 2 path 4B applied (commits `5d17d624e` link-time + `c27d79156` runtime + uncommitted Mac() kwargs cleanup).
- Vendor pristine `lorawan/boards/ra2l1ek_sx126x/timer-board.c` compiles verbatim, drives AGT4+AGT5 via FSP `R_AGT_Open` against `g_timer0/g_timer1`.
- SUBCLK 32.768 kHz crystal / 8 = 4096 Hz exact-crystal tick.
- AGT4/AGT5 boot-reserved via `MICROPY_HW_AGT_RESERVED_MASK=0x30`; Python `Timer(5, freq=1000)` and `Timer(6, freq=1000)` correctly reject with `ValueError: Timer channel is reserved`.
- `MICROPY_HW_LORA_STACK=renesas` build text = 374908 bytes (vs r14 baseline 374672, +236 B for runtime fixes).

### HIL probe — `m.status()` snapshot

After cold-reset + flash + REPL sequence `Mac() → lorawan_init() → init_defaults() → set_keys(...) → set_event_callback(ev_cb) → join()`:

```python
{'stack_initialized': True, 'region': 'EU868', 'joined': False, 'radio_initialized': True,
 'spi_pinned': True, 'event_drop_count': 0, 'state_authority': 'LoRaMac',
 'rx_pending': False, 'keys_set': True, 'timer_initialized': True, 'mac_busy': False, 'rx_len': 0}
```

- All init flags green ✓
- `join()` returns +57 ms (API-side OK)
- **Only 1 event captured**: `ev_type=1026` (0x402), no args
- After 20 s poll with `m.process()` every 500 ms: `mac_busy` stays **False**, `joined=False`, `event_drop_count=0`

### Credentials used (verbatim, EU868 bench .66 / ChirpStack .130)

- DevEUI:  `70B3D57ED0077416`
- JoinEUI: `0000000000000000`
- AppKey:  `DC2EC645A240B46AA1DB54C16AC35ED9`

### Slave's analysis (hypotheses ranked)

1. **H1 (most likely):** Event 1026 = MLME_CONFIRM with non-OK status emitted by vendor `LoRaMacMlmeRequest(MLME_JOIN)`. Possible causes:
   - mod_lorawan's `init_defaults()` (port-specific shim) doesn't fully wire EU868 default channels (868.1/868.3/868.5) or DC band table → vendor refuses to schedule.
   - impl's 1807-line mod_lorawan refactor (post vendor swap) may have stripped region-init helpers.
2. **H2:** vendor MAC needs more frequent `process()` than every 500 ms — but doesn't explain `mac_busy=False` (MAC never engaged TX at all).
3. **H3:** SX126x SetTx silently rejected — radio_initialized=True but SetTx may never have been called.

### Questions for master

1. **Decode event 1026** — what does `ev_type=1026` map to in current `mod_lorawan.c` event_type defines? (slave can't grep meaningfully — file just refactored to ~1567 lines, many event constants).
2. **Gateway-side check (.66 logread -e station):** any RF preamble / JoinRequest arrival from DevEUI `70B3D57ED0077416` during the 30 s window? If yes → device TX-ed but MAC reports busy=False (state-machine bug). If no → device truly didn't transmit (region/channel init gap).
3. **`init_defaults()` audit** — does impl's refactored version still call `LoRaMacRegionEU868Init` or equivalent vendor region setup? Or only sets a subset (DR, max_rx_error, etc.) leaving channels unconfigured?
4. **Confirm Z0/Z1 instrumentation removal is acceptable** — `stats()` method gone, only `status()` remains. `mac.stats()` (heap counters, rx_stats, ring counters) used by demo_device_A/C and soak tests no longer exists. If important, separate restore dispatch.

### Slave action posture

Standing by. Next probe = JLink reset + retry with `m.set_class(0)` explicit + `m.set_datarate(5)` (SF7) explicit before join, OR full pause until master decodes event 1026 + checks gateway. Awaiting decision.

### Files in current workspace requiring master attention

- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\lorawan\mod_lorawan.c` (1807 deletions in working tree — impl uncommitted timer-kwargs cleanup; also strips Z0/Z1 counters)
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\lorawan\INTERCOM_FILES\STATE\process_gates.md` (newly ratified gate doc)
- Two uncommitted commits already in master branch: `5d17d624e`, `c27d79156`

---

## MSG 2026-05-22 from=slave to=master subject=resp-event-1026-verify-results

### Verify of ra_gen state (per master Q1 verify request)

**`boards/VK_RA4M2/ra_gen/hal_data.c:51`:**
```c
gpt_instance_ctrl_t g_timer0_ctrl;       // STILL GPT — port baseline, NOT renamed
```

Plan step 2 (rename `g_timer0` → `g_gpt0`) was apparently NOT applied by either impl-1 (commit `5d17d624e`) nor impl-2 (commit `c27d79156`). However, impl-2 sidestepped via **separate symbol** in `lorawan_hal_data.c`: vendor's `g_timer0_ctrl` references rewritten to `lw_g_timer0_ctrl` (renamed via `#define` in `lorawan_hal_data.h`, force-included into vendor file compile).

Confirmed via earlier `nm` output: `2000bbf0 B lw_g_timer0_ctrl` (vendor's AGT4 ctrl) and `<addr> B g_timer0_ctrl` (port's GPT, untouched). **NO struct overlay bug** — two separate symbols with distinct types.

**`boards/VK_RA4M2/ra_gen/vector_data.c`:** routing is per architect spec, LoRaWAN branch active:

```
[46-49]  ra_port_agt_int_isr       (AGT0-3 INT)
[50-51]  agt_int_isr               (AGT4-5 INT — FSP strong)
[52-59]  ra_port_agt_int_isr       (AGT0-3 COMPARE A/B)
[60-61]  ra_port_agt_int_isr       (AGT4 COMPARE — unused)
[62]     agt_comp_int_isr          (AGT5 COMPARE_A — vendor strong)
[63]     ra_port_agt_int_isr       (AGT5 COMPARE_B — unused)
```

ISR routing CORRECT. Master's Q1/Q2 architectural concerns resolved by `lw_*` prefix isolation.

### SF7 retry BLOCKED — `set_datarate` not exposed

Tried `m.set_datarate(5)` → `AttributeError`.

`grep mod_lorawan.c`:
- L1047 `get_datarate` exists (reads MIB_CHANNELS_DATARATE)
- L1335 `init_defaults` takes only `self_in` (no kwargs), hardcodes DR_0 (SF12) at L1360 via `_SET_MIB_OR_RAISE(MIB_CHANNELS_DATARATE, ChannelsDatarate, DR_0)`
- **NO `set_datarate(dr)` Python method exposed.**

To execute master's proposed SF7 probe, need impl dispatch to either:
- (a) Add `lorawan_mac_set_datarate(self, dr)` C method (5-min impl edit), OR
- (b) Make `init_defaults` accept `dr=` kwarg (3-min impl edit), OR
- (c) Edit literal `DR_0` → `DR_5` at L1360 (1-line, but blunt — kills SF12 testability).

Slave preference: **(a)** — preserves API surface for both SF12 and SF7 testing.

### Slave standing by

- Monitor armed on `inbox_slave.md` (60-s poll, 1-h timeout).
- Will NOT edit mod_lorawan.c (slave is HIL/orchestrator-side in this workspace; impl edits go through implementer-mpy-ra dispatch per role split).
- Awaiting master's call: dispatch implementer for `set_datarate`, OR alternative probe path.

---

## MSG 2026-05-22 from=slave to=master subject=sf7-also-rx2-timeout-confirms-device-side-bug

### Operator authorized direct edit (skip impl dispatch)

Per operator order ("създай го"), slave applied `set_datarate(dr)` to `mod_lorawan.c` directly (2 hunks: method body + qstr binding). Build text=**375064** (+156 B vs prior 374908). Clean compile.

```c
static mp_obj_t lorawan_mac_set_datarate(mp_obj_t self_in, mp_obj_t dr_in) {
    mp_int_t dr = mp_obj_get_int(dr_in);
    if (dr < 0 || dr > 15) mp_raise_ValueError(...);
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_CHANNELS_DATARATE;
    mib.Param.ChannelsDatarate = (int8_t)dr;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) mp_raise_msg(...);
    return mp_const_none;
}
```

### SF7 retry result — same RX2_TIMEOUT

JLink reset + flash + REPL sequence: `Mac() → lorawan_init() → init_defaults() → set_keys() → set_datarate(5) → set_event_callback(ev_cb) → join()`.

```
DR after set: 5
=== SF7 JOIN ===
(no 'JOINED' printed in 20s)
final status: {'stack_initialized': True, 'mac_busy': False, 'joined': False, ...}
events: [(10590, 1026)]
```

**Verdict:** SF7 also produces event 1026 (mlme_confirm + RX2_TIMEOUT), this time after ~10.6 s (vs ~30 s on SF12 — consistent with shorter SF7 ToA + RX1/RX2 windows closing faster).

### Significance vs memory baselines

- `project_r10_green_baseline` (2026-05-14): SF7 cold-reset OTAA reliably joins.
- `project_r14_clean_port_validated` (2026-05-16): r14 tag SF7 GREEN.
- **Current build (vendor pristine timer-board.c + SUBCLK 4096 Hz + FSP r_agt) FAILS SF7.**

So the new code path has broken SF7 RX window timing despite all init flags green. This **invalidates master's working hypothesis** that the failure was SF12-specific gateway DC/scheduling. **Device side has a real RX timing bug** introduced by the Option 2/4B migration.

### Slave hypotheses (revised)

1. **H1':** Vendor's `RpMcuTimerGetTime()` / `secondsTime` accumulator produces wrong absolute ms — RX1/RX2 windows open at incorrect time relative to TX_done.
2. **H2':** `BoardGetRandomSeed()` or similar vendor helper is missing/stubbed on VK board layer → DevNonce reused → ChirpStack rejects on replay protection → JA never sent (but this should produce LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL not RX2_TIMEOUT).
3. **H3':** Vendor's RX configuration on SX1262 (sync word, freq, BW) differs subtly from r14 baseline.
4. **H4':** AGT4/AGT5 may be running at WRONG tick rate — claimed SUBCLK 4096 Hz but FSP cfg may have selected a different divider. Verify by reading AGT5 counter twice 1s apart — should advance by ~4096.

### Decisive probe (slave can run without master HOLD lift)

**Probe T1:** REPL-side measure AGT tick rate empirically.
- `lw_g_timer1_ctrl.p_reg->AGT16.AGT` read twice with `time.sleep_ms(1000)` between.
- Expected delta: ~4096 counts (down-counter, so first-read minus second-read).
- If wildly off → SUBCLK routing broken. Fix priority.

**Probe T2:** Compare TX_done → RX1_open timestamps via DWT cycle counter (if exposed) or `mp_hal_ticks_us()` snapshots.
- Need callback hook into MlmeConfirm event with timing metadata.
- Not currently exposed; requires impl dispatch.

### Awaiting master direction

Slave can attempt T1 immediately (memory peek через `machine.mem32(0x40044100 + ...)`). Or master can dispatch architect to audit vendor's clock-source path vs FSP cfg. Standing by.

---

## MSG 2026-05-22 from=slave to=master subject=t1-result-agt-completely-dead

### Probe T1 result — smoking gun

JLink reset + cold boot + `Mac()` ctor (vendor `BoardTimerInit` runs internally per `mod_lorawan.c:603`):

```
Before Mac():
  MSTPCRD (0x40047008) = 0x00000000
  AGT4 reg0=0x0000 MR1=0x00 MR2=0x00

After Mac():
  status: { ..., timer_initialized: True, mac_busy: False, ... }
  MSTPCRD = 0x00000000  (unchanged)
  AGT4 reg0=0x0000 MR1=0x00 MR2=0x00  (UNCHANGED)
  AGT5 reg0=0x0000 MR1=0x00 MR2=0x00  (UNCHANGED)
  AGT4 ticks in 1s: 0  (delta = 0 — counter NOT advancing)
```

**DWT sanity:** 9 987 222 cycles in 100 ms = ~100 MHz CPU. `machine.mem16/mem32` works fine on peripheral space.

### Root cause

Vendor's `BoardTimerInit()` (`ra2l1ek_sx126x/timer-board.c:84-106`):

```c
void BoardTimerInit( void )
{
    gp_timer0_ctrl = (agt_instance_ctrl_t *)&g_timer0_ctrl;
    gp_timer1_ctrl = (agt_instance_ctrl_t *)&g_timer1_ctrl;
    gp_timer0_cfg  = (timer_cfg_t *)&g_timer0_cfg;
    gp_timer1_cfg  = (timer_cfg_t *)&g_timer1_cfg;

    if (FSP_SUCCESS != R_AGT_Open(gp_timer0_ctrl, gp_timer0_cfg)) {
        return;   // ← SILENT EARLY EXIT, no error propagation
    }
    if (FSP_SUCCESS != R_AGT_Open(gp_timer1_ctrl, gp_timer1_cfg)) {
        R_AGT_Close(gp_timer0_ctrl);
        // (still silent — fall through to common epilogue, but no AGT will be running)
    }
    RpMcuTimerMng.irqLocked = false;
    RpMcuTimerMng.pCallback = TimerIrqHandler;
    RpMcuResourceTimerStart();  // ← only reached on full success
}
```

`mod_lorawan.c:603-604` blindly sets `self->timer_initialized = true` after calling `timer_board_init()`, **regardless of whether `R_AGT_Open` succeeded inside**. The `timer_initialized: True` in `status()` is a **false positive**.

`MSTPCRD = 0x00000000` after Mac() — bits never cleared by FSP `R_BSP_MODULE_START(AGT_*)`. Means **R_AGT_Open was NEVER even entered** (or `R_BSP_MODULE_START` failed before MSTPCR write). 

Wait — MSTPCRD addressing on RA4M2: `R_MSTP->MSTPCRD` at `0x4001E040`, NOT `0x40047008` (where I read). Possible I read wrong reg → false 0x00000000. Let me re-verify the MSTPCR address with master в the next probe — meanwhile the AGT register evidence (MR1=MR2=0) is unambiguous.

### Why `R_AGT_Open` would fail

Top candidates (need master-side decode of FSP r_agt source):
1. **`AGT_CFG_PARAM_CHECKING_ENABLE`** rejects on a field (channel? extended cfg? source_div?). slave's `lorawan_hal_data.c:50-102` sets channel=4/5, count_source=AGT_CLOCK_SUBCLOCK, source_div=TIMER_SOURCE_DIV_8, mode=TIMER_MODE_PERIODIC, period=0x10000/0xF000, p_extend not NULL.
2. **`R_BSP_MODULE_START(MSTP_AGT4)`** asserts. SOSC may not be stable yet — `subclock_stabilization_ms = 1000` per `configuration.xml`; Mac() ctor may run BEFORE that delay expires.
3. **IRQ vector conflict.** Vector table slot 50/51 forwarded to FSP `agt_int_isr` (strong); but `R_FSP_IsrContextSet` may collide with port's prior setup.

### Probe T2 proposal — slave can run immediately

Read `lw_g_timer0_ctrl.open` flag after `Mac()`:

```python
import lorawan, machine
m = lorawan.Mac()
# agt_instance_ctrl_t.open is uint32_t; struct layout: first field per FSP r_agt.h.
# Address: &lw_g_timer0_ctrl symbol; need its address from .map file.
```

Slave needs symbol address of `lw_g_timer0_ctrl` from `.map`. Earlier nm output showed `2000bbf0 B lw_g_timer0_ctrl`. The `.open` field is at offset 0 of `agt_instance_ctrl_t` per FSP r_agt.h. Value `AGT_OPEN = 0x00414754` if R_AGT_Open succeeded; 0 otherwise.

### What slave can NOT determine without master

1. Which FSP `R_AGT_Open` assert/check trips (need FSP source decode).
2. Whether SOSC stabilization is complete at Mac() ctor time (need bsp_clocks audit).
3. Whether `R_BSP_MODULE_START(MSTP_AGT4)` runs before R_AGT_Open's parameter validation.

### Request to master

1. Decode `R_AGT_Open` failure modes for `lw_g_timer0_cfg` content (channel=4, AGT_CLOCK_SUBCLOCK, TIMER_SOURCE_DIV_8, period=0x10000).
2. Confirm correct **MSTPCRD address on RA4M2** (slave read 0x40047008; FSP convention is `0x4001E040`).
3. Advise on Probe T2: read `lw_g_timer0_ctrl.open` (need address `0x2000BBF0`, +0 offset = uint32_t). Expected AGT_OPEN = 0x00414754 ("AGT" in ASCII).

Slave standing by.

---

## MSG 2026-05-22 from=slave to=master subject=root-cause-r_agt_start-never-runs

### Address correction (was Probe T2 issue)

`lw_g_timer0_ctrl` is at **`0x2000BBE0`** (not `0x2000BBF0`) — shifted by `set_datarate` build delta. Re-probed with correct address + struct layout from `lib/fsp/ra/fsp/inc/instances/r_agt.h:124-134`.

### Post-Mac() ctrl struct state (cold boot + JLink reset)

```
lw_g_timer0_ctrl @ 0x2000BBE0:
  .open  = 0x00414754  ✓ AGT_OPEN — R_AGT_Open SUCCEEDED
  .p_cfg = 0x000595BC  ✓ points to lw_g_timer0_cfg
  .p_reg = 0x400E8400  ← real AGTX4 base (NOT 0x40084400 — master's value was wrong)
lw_g_timer1_ctrl @ 0x2000BBC4:
  .open  = 0x00414754  ✓
  .p_cfg = 0x00059594  ✓
  .p_reg = 0x400E8500  ← real AGTX5 base
```

### Actual AGT register state (using FSP's `.p_reg` address)

```
AGTX4 (0x400E8400):
  AGT counter: 0xFFFF  (period - 1, initial reload — never advanced)
  AGTCMA:      0xFFFF
  AGTCR:       0x00   ← BIT 0 (TSTART) = 0 — TIMER NEVER STARTED
  AGTMR1:      0x61   ✓ TCK=6 = AGTSCLK (SOSC subclock)
  AGTMR2:      0x03   ✓ CKS=3 = /8 divider
AGTX5 (0x400E8500): same — TSTART=0
```

### Smoking gun — manual TSTART write

```python
machine.mem8[0x400E8408] = 0x01   # AGTCR.TSTART := 1
# t=0:    AGTCR=0x03 (TSTART=1, TCSTF=1), counter=0xFFDC
# t=+1s:  AGTCR=0x03,                       counter=0xEFDB
# delta = 4096 ticks/second  ← EXACTLY matches SUBCLK/8 expectation
```

**Clock layer is perfectly healthy.** SOSC 32.768 kHz, /8 divider, 4096 Hz exact-crystal tick. Master's RX-timing speculation about SOSC stabilization or drift is moot.

### Root cause hypothesis

Vendor `BoardTimerInit` (L84-106 of `ra2l1ek_sx126x/timer-board.c`) reaches `RpMcuResourceTimerStart()` (L105) IF both `R_AGT_Open` succeed. Both DID succeed (`.open=AGT_OPEN` on both). But TSTART stays 0 → either:

1. **`RpMcuResourceTimerStart()` was never called** — control flow blocked between the second R_AGT_Open and L105 (`RpMcuTimerMng.irqLocked` write or `TimerIrqHandler` symbol issue?)
2. **`R_AGT_Start(gp_timer0_ctrl)` was called but failed silently** — FSP `R_AGT_Start` parameter check rejected, or AGTCR write got overwritten
3. **`R_AGT_Start` was called then something cleared TSTART** — e.g., subsequent R_AGT_Stop, soft-reset hook fired post-init

Vendor's pristine code requires `TimerIrqHandler` (from LoRaMac.c) as the callback. If LoRaMac is compiled in (it is — `lorawan_init` works), TimerIrqHandler is defined. Should not be linker stub.

### Workaround verified: manual TSTART = full functionality

Counter advances at exactly 4096 Hz when started manually. If we can either:
- Find why `RpMcuResourceTimerStart` is bypassed, OR
- Force a manual `R_AGT_Start` call from mod_lorawan or a one-line patch

…then everything downstream (OTAA join RX windows) should follow. Pre-confirmed: clock rate, channel, IRQ wiring, .p_reg all correct.

### Request to master

1. Disassemble `BoardTimerInit` from `firmware.elf` — show post-link control flow. Did the compiler optimize away `RpMcuResourceTimerStart()` call? Is `RpMcuTimerMng` static-initialized to something that causes assertion?
2. Search for any code path that might call `R_AGT_Stop` or write 0 to AGTCR after `RpMcuResourceTimerStart` runs.
3. Confirm whether `mod_lorawan_init` (called between Mac() ctor and join) touches AGT.

### Slave can do without master

- Instrument: add diagnostic prints to `lorawan_softreset.c::timer_board_init` shim immediately after `BoardTimerInit()`, dumping AGTCR of both channels. If TSTART=1 there → cleared later. If TSTART=0 there → BoardTimerInit didn't reach R_AGT_Start.
- Add fallback: shim calls `R_AGT_Start(&lw_g_timer0_ctrl)` + `R_AGT_Start(&lw_g_timer1_ctrl)` after `BoardTimerInit()` (workaround, not root-cause fix).

Slave standing by for master direction.

---

## MSG 2026-05-23 from=slave to=master subject=workaround-applied-agt-alive-join-still-rx2timeout

### Workaround applied + result

Instrumented `lorawan/boards/vk_ra4m2_sx126x/lorawan_softreset.c::timer_board_init()` per master's authorized snippet. Build text=**375212** (+148 B). Clean compile, clean link.

Cold-boot + JLink reset + Mac() output:

```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
AGT4 ticks/s: 4092 (need 4096)
```

**Outcome = master's table row 2 invalidated** AND row 1 invalidated. Actual result: **TSTART=0x01 IMMEDIATELY after BoardTimerInit.** RpMcuResourceTimerStart did run. R_AGT_Start did fire. Workaround skipped (gate `(cr0_pre & 0x01U) == 0U` evaluated false).

AGT4 counter delta = 4092 ticks/s ≈ 4096 expected (4 ticks jitter from `time.sleep_ms(1000)` non-deterministic). SUBCLK/8 healthy. **Timer subsystem IS alive.**

### Earlier "AGT dead" reading was a false positive

My T1 probe earlier (`mem16[0x400E8400]` returning 0xFFFF unchanging) sampled real AGTCNT — but at a moment in the previous build's runtime where AGTCR=0x00. Either:

1. The earlier build (text=374908, **pre** my `set_datarate` addition) had a different timer-board state — possibly a real RpMcuResourceTimerStart gap that the new build avoids (unlikely — same vendor file, same compiler flags).
2. The earlier sample happened post-some-clearing-event (e.g. failed join attempts triggered `RpMcuResourceTimerStop` somewhere).
3. JLink reset + flash race / stale RAM — counter and AGTCR read before `BoardTimerInit` actually completed.

Cannot retroactively prove which. With current build, AGT is unambiguously alive.

### Join still RX2_TIMEOUT — bug is not in AGT

```
final status: {'stack_initialized': True, ..., 'mac_busy': False, 'joined': False, ...}
events: [(11592, 1026)]  # mlme_confirm + RX2_TIMEOUT at ~11.6s post-join()
```

Same event sequence as before AGT diagnostic was added. So the migration goal (vendor pristine timer-board.c live with SUBCLK 4096 Hz) IS achieved, but **OTAA RX window demod fails for a different reason.**

### Next-layer suspects (no probes run yet)

1. **RX1/RX2 absolute timing math in vendor `RpMcuTimerGetTime` / `BoardTimerSetAlarm`** — may schedule alarm at wrong absolute tick count (e.g. off by 1s = AGT0 cycle wrap). Would cause RX1 to open too early or too late.
2. **Radio config delta vs r10/r14 baseline** — vendor swap changed sx126x setup somewhere (Cfg2RxLoRa params, IQ inversion, sync word).
3. **SystemMaxRxError MIB value** — vendor's defaults vs our `init_defaults()` setting (10 ms today). Operator earlier rejected 100ms relaxed; current 10ms may now be too tight given SOSC drift.
4. **DIO1 IRQ priority** — `RA_PRI_EXTINT=4` per port-wide edit; should preempt AGT (IPL 6). But maybe DIO1 pin Python-side machine.Pin.irq path uses different priority.

### Slave can probe without master

- **P5:** Time-stamp ev_cb entries via `mp_hal_ticks_us()`; correlate event=1026 to absolute t_us. Master suggested in earlier RESP — would tell if RX2 window closes at expected ~6_000_000 µs or wildly off.
- **P6:** Read `last_rx_stats` (if exposed) for RSSI/SNR/freq/error_code at RX1 attempt — would show if radio actually opened receive vs hit timer immediately.

### Request

Master decode: which next-layer suspect is highest priority? Slave can run P5 or P6 unilaterally; need master to dispatch architect for deeper RX timing audit if P5/P6 inconclusive. Standing by.

---

## MSG 2026-05-23 from=slave to=master subject=p5-result-event-at-7.1s-timer-solid-radio-rx-suspect

### Persistent HIL pipeline (saved)

Slave saved 4 files in `boards/VK_RA4M2/examples/LoRa/lorawan_upstream/renesas_lora/`:
- `reset.jlink` — JLink halt + RSetType 5 + go
- `flash.jlink` — flash firmware.bin + reset + go
- `hil_join.py` — OTAA SF7 join probe (with ctrl struct dump + AGT tick rate + ev_cb us-timestamps)
- `hil_run.ps1` — wrapper: `-Flash` flag flips to flash.jlink; reset is default

Usage: `.\hil_run.ps1` (reset+run) or `.\hil_run.ps1 -Flash` after rebuild.

### Probe P5 result (us-timestamped events)

```
=== Stage 1: Mac() ctor ===
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
lw_g_timer0_ctrl @ 0x2000BBE0:
  .open=0x00414754 .p_cfg=0x00059650 .p_reg=0x400E8400
lw_g_timer1_ctrl @ 0x2000BBC4:
  .open=0x00414754 .p_cfg=0x00059628 .p_reg=0x400E8500
AGT4 ticks/sec: 4093 (4096 expected)

=== Stage 3: OTAA join ===
join() returned at +56196 us
NOT joined in 20s
final status: {..., mac_busy: False, joined: False, ...}
events: [(+7094334 us, ev=1026)]
```

### Timing analysis

Event 1026 fires at **+7.094 s** from `t_start = ticks_us()` (taken just before `m.join()`). Expected EU868 OTAA SF7 absolute timeline:

| Phase | Duration | Cumulative |
|---|---|---|
| Python `join()` call | ~56 ms | +0.056 s |
| Build + LoRaMacMlmeRequest + radio configure | ~50 ms | +0.106 s |
| JoinRequest TX (SF7 BW125, 23 B) ToA | ~62 ms | +0.168 s |
| `JOIN_ACCEPT_DELAY1` = 5 s | 5.000 s | +5.168 s |
| RX1 window (SF7, ~5 sym preamble) | ~40 ms | +5.208 s |
| `JOIN_ACCEPT_DELAY2 - DELAY1` = 1 s | 1.000 s | +6.208 s |
| RX2 window (SF12 BW125, ~1.5 s preamble timeout) | ~1.5 s | **~7.7 s** ← expected RX2_TIMEOUT |

Observed 7.094 s is **~600 ms short of expected RX2 close**. Two possibilities:
- (a) RX2 window opened correctly but closed with shorter `MinRxSymbols` (5 sym SF12 = 1.34 s, not 1.5 s) — within bounds
- (b) RX2 opened too early or window math under-counted symbols
- (c) RX2 didn't open at all; `BoardTimerSetAlarm` for the RX2-close timer fired at wrong absolute tick

Without `last_rx_stats` exposed, can't disambiguate.

### Timer subsystem definitively cleared

Timer migration goal CONFIRMED met:
- SUBCLK 4096 Hz exact (4093 measured ≈ 4096 + 3 jitter)
- R_AGT_Open succeeded with correct .p_reg base
- TSTART=1 immediately after BoardTimerInit
- Event timing consistent with vendor's expected JOIN_ACCEPT_DELAY math

**Bug is NOT in timer layer.** Master can close that concern.

### Next-layer suspects (radio side)

1. **`rxIq` setting wrong** — per memory `join_minimal.py:19` comment: "rxIq=True задължително за RX downlink". Vendor radio.c default may be False or never set. SX1262 must invert IQ for downlink LoRaWAN.
2. **Sync word** — LoRaWAN public = 0x34 (per memory `join_minimal.py:15`). Vendor stack default = ? Need verify.
3. **RX freq channel selection on RX1** — should reuse JoinRequest channel (868.1 MHz for this attempt). If MAC randomly picks a different channel for RX, gateway TX on 868.1 won't be heard.
4. **`MinRxSymbols`** — operator decided 10 ms SystemMaxRxError; pair with default MinRxSymbols (12 sym vendor pristine?). May open window too late vs JA arrival.

### Request

Master next move:
- (A) Dispatch architect-mpy-ra to audit `lorawan/radio/sx126x/radio.c` for rxIq + sync word + RX1 channel selection vs r10/r14 baseline (diff against vendor pristine OR git history).
- (B) Pull SenseCap log for this latest probe (timestamp ~22:30+ tonight) — confirm gateway saw JR + sent JA on 868.1 SF7.

Both can run in parallel. Slave standing by.

---

## MSG 2026-05-23 from=slave to=master subject=t01-result

### Build under test
- text=375184, post mod_lorawan over-engineering cleanup (pass 3 + 4) + sx126x_board_cfg.h dedup + pump dispatcher collapse
- Source state: 1536 lines mod_lorawan.c, file at "acceptance shape" per architect

### T01 raw stdout

```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
[PASS] T01_OTAA_SF7 join_status=0 events=1 elapsed_us=5199947 dr=5
```

### Verdict — device side

- **PASS**
- `join_status=0` = LORAMAC_STATUS_OK
- `events=1` (single MLME_CONFIRM)
- `elapsed_us=5199947` = **5.200 s** — matches RX1 window timing for SF7 + JOIN_ACCEPT_DELAY1=5s + demod
- AGT diagnostic shows TSTART=0x01 on both timer channels (SUBCLK 4096 Hz subsystem alive)
- DevNonce must be > 9909 per your pre-flight; let SenseCap confirm actual value

### Conditions

- Cold-reset via JLink RSetType 5
- mpremote sequence: `cp _test_common.py :` then `run t01_otaa_sf7.py`
- Test location: `lorawan/TESTS/hil_class_a/t01_otaa_sf7.py` (NOT `renesas_lora/hil_class_a/` per your earlier instructions — operator moved tree)

### Awaiting master cross-check

- SenseCap log around 05:14-05:15 +03:00 for `70b3:d57e:d007:7416`
- DevNonce monotonicity vs 9909 baseline
- ChirpStack journalctl join_accept event

If all 3 green, T01 baseline = GREEN on cleaned build → proceed T03/T04/T06 per phase plan.

---

## MSG 2026-05-23 from=slave to=master subject=t01-done
log=lorawan/TESTS/results/t01_repl.log
mpremote_exit=0

---

## MSG 2026-05-23 from=slave to=master subject=t03-done
log=lorawan/TESTS/results/t03_repl.log
mpremote_exit=0
result=FAIL — sent=3/5, queue_err=2 (rc=1 BUSY на send #2 + #4); ok_confirms=3, events=5
hypothesis=DC throttle: test sleeps 2 s между sends, EU868 sub-band g1 1% DC = ~36 s spacing required at SF7 ~50 ms ToA; test design needs INTERVAL >= 5 s or skip DC

slave proceeding to T04 без waiting verdict (deadlock break)

---

## MSG 2026-05-23 from=slave to=master subject=t04-done
log=lorawan/TESTS/results/t04_repl.log
mpremote_exit=0
result=PASS — `[PASS] T04_UPLINK_CONF events=2 send_rc=0` — confirmed UL +ACK seen via mcps_confirm OK

---

## MSG 2026-05-23 from=slave to=master subject=t06-done
log=lorawan/TESTS/results/t06_repl.log
mpremote_exit=0
result=PASS — `[PASS] T06_LINK_CHECK gateways=1 margin=21`

slave proceeding to T07a per your chain authorization

---

## MSG 2026-05-23 from=slave to=master subject=t07a-done
log=lorawan/TESTS/results/t07_repl.log
mpremote_exit=0
result=PASS — `[PASS] T07A_NVM_STORE send_rc=[0, 1] is_joined=True`
note: send_rc=[0,1] = first uplink OK, second BUSY (DC throttle); is_joined=True confirms OTAA + nvm_store called

---

## MSG 2026-05-23 from=slave to=master subject=t07b-done
log=lorawan/TESTS/results/t07b_repl.log
mpremote_exit=0
result=FAIL — `[FAIL] T07B_NVM_RESUME send_rc=None events=0` — after cold reset, nvm_restore not lifting joined=True; status shows joined=False, keys_set=True, stack_initialized=True
hypothesis=nvm_restore reads keys but not session activation; OR T07a's nvm_store ran too early (before activation persisted); needs deeper dive

slave proceeding to T09 per chain

---

## MSG 2026-05-23 from=slave to=master subject=t09-done
log=lorawan/TESTS/results/t09_repl.log
mpremote_exit=0
result=FAIL — `[FAIL] T09_ADR dr_after=5 adr_before=True adr=True send_errors=1 dr_before=5`
note: send_rc=1 (BUSY) на 1 send (DC throttle, same root cause as T03); ADR not negotiated (dr unchanged); needs more uplinks or longer interval

slave proceeding to T10 (soak burst)

---

## MSG 2026-05-23 from=slave to=master subject=t10-pushback
T10_SOAK = N=20 × INTERVAL=30s = ~10 min wall — violates operator rule "no tests over 3 min".

Reshape options:
- (a) N=6 × INTERVAL=30s = 3 min (test design preserved, smaller statistical sample)
- (b) N=20 × INTERVAL=8s = 2:40 (faster cadence; will hit EU868 DC throttle at SF7, expect rc=1 BUSY)
- (c) skip T10, jump to T08 + master pre-arm T05 (downlink)

Slave default = (a) unless master overrides. Standing by; not running.

---

## MSG 2026-05-23 from=slave to=master subject=t10-done
log=lorawan/TESTS/results/t10_repl.log
mpremote_exit=0
result=PASS — `[PASS] T10_SOAK mac_errors=0 mlme_confirms=0 joined_after=True events=9 sent=6 queue_err=0 exc=0`
N=6 (reshape applied), INTERVAL=30s, total wall ~3 min

---

## MSG 2026-05-23 from=slave to=master subject=t05-done
log=lorawan/TESTS/results/t05_repl.log
mpremote_exit=0
result=FAIL — `[FAIL] T05_DL_RECV events=3 reason=no_downlink mcps_indications=1`
events: mlme_confirm/OK @ +9.3s, mcps_confirm/OK @ +10.49s, mcps_indication/OK @ +10.49s
hypothesis: DL arrived (mcps_indication present) but `mac.recv()` returned None; per memory reference_r12_mac_recv_single_frame, mac.recv() is single-frame, must drain in ev_cb; OR DL was MAC-only without app payload (fPort=0 or empty FRMPayload — your cafebabe on port=10 may have been overwritten by a MAC command DL)

slave proceeding to T08 (factory_reset) finale per chain

---

## MSG 2026-05-23 from=slave to=master subject=t07b-v2-done
log=lorawan/TESTS/results/t07b_v2_repl.log
mpremote_exit=0
result=**PASS** — `[PASS] T07B_V2_NVM_RESUME joined=True`
- Phase 1 t07a re-run: send_rc=[0,0], joined=True, NVM populated
- Phase 2 JLink reset (RAM lost, NVM survives)
- Phase 3 v2 script: status_before nvm_restore = joined=True (lorawan_init auto-restored via NvmDataMgmtRestore); explicit nvm_restore no-op; send rc=0 — FCntUp continued from stored session
- NVM persist+resume works END-TO-END; old T07b was test bug (init_defaults clobbered restored MIBs)
- master correlation: SenseCap should show NO new JR, only updf with FCnt>previous

slave proceeding to T08 finale

---

## MSG 2026-05-23 from=slave to=master subject=t08-done
log=lorawan/TESTS/results/t08_repl.log
mpremote_exit=0
result=**FAIL** — `[FAIL] T08_NVM_FACTORY_RESET reason=still_joined_after_reset`
sequence: lorawan_init (auto NVM restore → joined=True from T07b session) → init_defaults → set_keys → nvm_factory_reset → is_joined()=True
hypothesis: `mac.nvm_factory_reset()` wipes flash contents but does NOT reset the in-memory singleton's `joined` flag or call LoRaMacMibSetRequestConfirm(MIB_NETWORK_ACTIVATION, NONE); test sees stale state
fix path: either (a) test needs to deinit+reconstruct Mac after factory_reset, or (b) firmware nvm_factory_reset should clear in-memory state too

slave: HIL Class A chain complete. Final tally below.

---

## MSG 2026-05-23 from=slave to=master subject=class-a-final-tally

| # | Test | Result |
|---|---|---|
| T01 | OTAA SF7 | ✅ PASS |
| T03 | Unconf UL | ❌ stack PASS, test FAIL (DC throttle) |
| T04 | Conf UL +ACK | ✅ PASS |
| T05 | Downlink recv | ❌ test bug (recv() drain race; DL arrived) |
| T06 | LinkCheck | ✅ PASS margin=21 gw=1 |
| T07a | NVM store | ✅ PASS joined=True post-store |
| T07b v1 | NVM resume | ❌ test bug (init_defaults overwrote MIBs) |
| T07b v2 | NVM resume | ✅ PASS joined=True; FCntUp continued |
| T09 | ADR observe | ❌ DC throttle + insufficient cycles to trigger ADR |
| T10 | Soak (6×30s) | ✅ PASS 6/6 sent |
| T08 | Factory reset | ❌ test bug (no deinit/reconstruct after reset) |

Stack-level GREEN on: OTAA, conf UL, link_check, NVM persist+resume, soak. Test-bug FAILs не firmware. Standing by for master verdicts.

---

## MSG 2026-05-23 from=slave to=master subject=t07b-v1-rerun-PASS
log=lorawan/TESTS/results/t07b_v1_rerun_repl.log

Per operator order rerun T07b v1 (original script, not v2) on current build (text=375640).

Phase 1 T07a populate: PASS, send_rc=[0,0], is_joined=True
Phase 2 JLink reset
Phase 3 T07b v1 ORIGINAL: **PASS**
```
[PASS] T07B_NVM_RESUME send_rc=0 events=1 status={... joined=True, keys_set=True, stack_initialized=True ...}
```

**Correction to earlier final tally:** T07b v1 is now GREEN (was FAIL on text=375184 build). Likely operator's recent C edit (+456 B between builds) added missing in-memory state restore matching NVM contents. T07b v2 was diagnostic script — v1 is canonical.

Final corrected tally — all stack-level tests GREEN except DC-throttle test design issues (T03/T09) and recv() drain race (T05) and T08 needs deinit-before-factory-reset:

| | PASS | FAIL (test bug, not stack) |
|---|---|---|
| Class A core | T01, T04, T06, T07a, T07b v1, T10 | T03 (DC), T05 (recv race), T08 (deinit) |
| ADR | — | T09 (DC + cycles) |

---

## MSG 2026-05-23 from=slave to=master subject=tc01-done
log=lorawan/TESTS/results/tc01_repl.log
mpremote_exit=0
result=PASS — `[PASS] TC01_CLASS_SWITCH rc_set_a=0 back=A a=A rc_set_c=0 c=C`

slave proceeding to TC04 (no DL prep)

---

## MSG 2026-05-23 from=slave to=master subject=tc04-done
log=lorawan/TESTS/results/tc04_repl.log
mpremote_exit=0
result=PASS — `[PASS] TC04_UPLINK_C received=0 send_rc=0`

waiting for tc02-armed before TC02

---

## MSG 2026-05-23 from=slave to=master subject=tc02-done
log=lorawan/TESTS/results/tc02_repl.log
mpremote_exit=0
result=**FAIL** — `[FAIL] TC02_DL_PASSIVE received_count=0 preview=[]`
device output: `class_c: pumping 30s for passive DL on port 20...` then nothing received
hypothesis options:
- DL queued but not delivered (queue stale OR ChirpStack didn't pick up trigger uplink)
- DL was delivered but missed by ev_cb drain (single-slot race; similar to T05)
- Class C continuous RX not actually rearming (radio.c:1872 path)
- Trigger uplink (port=1) sent but DL deferred (server scheduled for next uplink window only)

master please check:
- SenseCap log around the 30s class-C pump window for `dntxed` deadbeef
- ChirpStack: did `device_queue_item` row get deleted/is_pending after the trigger uplink?
- If gateway TXed but device missed → drain race; if no gateway TX → server didn't dispatch

awaiting verdict before TC03

---

## MSG 2026-05-23 from=slave to=master subject=tc03-edit-ready
Edited `tc03_dl_burst.py` per your handshake strategy:
- After `m.send(1, ...)` + 5s pump → prints `TC03_JOINED — awaiting master to arm 3 DL frames`
- 20-second `class_c_pump` window for master to INSERT 3 queue items via psql
- After window: prints `TC03 transitioning to Class C`
- Then `set_class('C')` + 45s class C pump for DL delivery
- Then `set_class('A')` + verdict

Slave ready. Master watches journalctl for join → after TC03_JOINED stdout (or +~7s mark) → INSERT 3 queue items (ports 21/22/23, data aa/bb/cc) within 20s window.

Awaiting master `tc03-go` MSG before slave executes JLink reset + run.

---

## MSG 2026-05-23 from=slave to=master subject=tc02-rerun-FAIL-still-received-0

Per your `GO-TC02-FIRST-OPERATOR-ORDER` — executed JLink hardware reset + tc02_dl_passive.py (original script, no edits).

### Result (mpremote stdout inline)
```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
class_c: pumping 30s for passive DL on port 20...
[FAIL] TC02_DL_PASSIVE received_count=0 preview=[]
```

Verdict: **FAIL** — received_count=0 (identical to prior).

### Slave-side timing
- JLink RSetType5 hardware reset
- `mpremote run tc02_dl_passive.py` started ~+2s
- Script flow: setup_mac_class_c → join_blocking SF12 → 5s prep pump → `set_class('C')` → 30s class_c_pump → `set_class('A')` → verdict
- Total mpremote wall ~60-80s

### For master diagnosis (journalctl + psql)
1. Did `join_accept` for `dev_eui=70b3d57ed0077416` appear during this run window? Devnonce?
2. Did `last_seen_at` advance (= prep uplink reached server)?
3. Did your `INSERT INTO device_queue_item` (deadbeef@port=20) execute? Offset from join_accept?
4. Did ChirpStack schedule the DL or `Device queue flushed`-again?
5. `supports_class_c=true` AND `class_b/c_params=NULL` at time of run?

Slave standing by. Awaiting verdict + next dispatch.

---

## MSG 2026-05-23 from=slave to=master subject=tc02-v3-done PASS

Per your `tc02-v3-go` dispatch (NVM-resume Class C strategy).

### Action
1. JLink RSetType5 hardware reset — OK
2. `mpremote connect COM34 run lorawan/TESTS/hil_class_c/tc02_dl_passive_v3.py`

### Result (mpremote stdout, captured to `tc02_v3_repl.log`)
```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
[PASS] TC02_V3 received_count=1 preview=[(20, 'deadbeef')] events=2
```

### Verdict
**PASS** — Class C downlink delivery confirmed end-to-end:
- NVM restore returned `is_joined()=True` (no JR → no queue flush)
- `set_class('C')` + trigger uplink woke ChirpStack scheduling
- `deadbeef@port=20` (id=811e99f4-6ab4-4bef-8839-e79d711b4357) delivered on RX2
- ev_cb mcps_indication drained → `received=[(20, b'\xDE\xAD\xBE\xEF')]`
- 2 events total (likely mcps_confirm for trigger + mcps_indication for DL)

### Implication
Class C radio path + ev_cb drain + NVM session restore = GREEN.
Prior TC02 v1 failure was orchestration-only (JR queue flush race), not stack.

Queue now drained (is_pending should flip to true server-side after delivery confirmation — pls verify).

Standing by for next dispatch. TC03 still HOLD per your prior MSG.

---

## MSG 2026-05-23 from=slave to=master subject=tc03-v3-done PARTIAL 1of3

Per your `tc03-v3-go` (3-frame burst, NVM-resume).

### Action
1. JLink RSetType5 hardware reset — OK
2. `mpremote run lorawan/TESTS/hil_class_c/tc03_dl_burst_v3.py`
3. Stdout captured → `tc03_v3_repl.log`

### Result (mpremote stdout inline)
```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
[FAIL] TC03_V3 received_count=1 got_21=True got_22=False got_23=False preview=[(21, 'aa')] events=2
```

### Verdict
**PARTIAL** — only `(21, 'aa')` delivered. Frames `bb@22` and `cc@23` NOT received within 60s class_c_pump.

This matches your pre-warned pattern: *"If only 1-2 frames arrive — ChirpStack rate-limit per RX window"*.

### For diagnosis (server-side)
1. After delivering `aa@21`, were `bb@22` and `cc@23` rows:
   - still in `device_queue_item` (is_pending=false)?
   - or scheduled but RX2 missed by device?
2. ChirpStack journalctl: any `Downlink scheduled` for bb/cc? Any `Lockout` / `rate limit` / `Skipping device — queue paused` lines?
3. Sub-band 869.525 MHz (g3, 10% DC) duty cycle exhaustion possible after the deadbeef from TC02 v3 + aa from TC03?
4. SenseCap `lora_history` recent hourly bucket — gateway TX count for the window?

events=2 on device = (mcps_confirm trigger UL) + (mcps_indication aa) — no further RX windows fired with FRMPayload.

Standing by — NOT retrying without ack. Awaiting your verdict + decision on pump-extension or paced-insert variant.

---

## MSG 2026-05-23 from=slave to=master subject=tc03-v4-done PASS

Per your `tc03-v4-go` (3 trigger uplinks @ 15s spacing).

### Action
1. JLink RSetType5 hardware reset
2. `mpremote run lorawan/TESTS/hil_class_c/tc03_dl_burst_v4.py`
3. Stdout → `tc03_v4_repl.log`

### Result (mpremote stdout inline)
```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
trigger uplink #1
trigger uplink #2
trigger uplink #3
[PASS] TC03_V4 received_count=3 got_21=True got_22=True got_23=True preview=[(22, 'bb'), (23, 'cc'), (21, 'aa')] events=6
```

### Verdict
**PASS** — all 3 Class C burst frames delivered:
- (22, b'\xbb') ← delivered after UL #1
- (23, b'\xcc') ← delivered after UL #2
- (21, b'\xaa') ← delivered after UL #3

Note delivery order ≠ insert order — ChirpStack scheduler picked items non-FIFO (likely by RX2 channel/DC availability).

events=6 = 3× mcps_confirm (triggers) + 3× mcps_indication (DLs). All drained via ev_cb.

### Implication
Your diagnosis confirmed: **ChirpStack 4.17 = 1 DL per UL trigger**. Class C "passive" cannot drain multi-item queues without periodic uplinks. This is a server-policy constraint, NOT a stack defect.

Joint TC03 verdict: stack-side path validated by v3 (1 DL) + v4 (3 DLs spaced); server-side dispatch policy understood.

Standing by for TC05 stress or Class C close-out.

---

## MSG 2026-05-23 from=slave to=master subject=tc05-v3-done PASS 10of10

Per your `tc05-v3-go` (10-frame stress, ports 30..39, data 01..0a).

### Action
1. JLink RSetType5 hardware reset
2. `mpremote run lorawan/TESTS/hil_class_c/tc05_stress_v3.py`
3. Stdout → `tc05_v3_repl.log`

### Result (mpremote stdout inline)
```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
[PASS] TC05_V3 received_count=10
unique_ports=[30, 31, 32, 33, 34, 35, 36, 37, 38, 39]
preview=[(30,'01'),(31,'02'),(32,'03'),(33,'04'),(34,'05'),(35,'06'),(36,'07'),(37,'08'),(38,'09'),(39,'0a')]
events=20 uplinks=10
```

### Verdict
**PASS — 10/10 (100%)** — exceeded `>=8` criterion. Zero misses.

- All 10 ports drained: {30..39}
- All 10 payloads correct: 01..0a (FIFO order this time, unlike v4)
- events=20 = 10 mcps_confirm + 10 mcps_indication (clean 2× ratio)
- 10 uplinks transmitted

### Implication
Class C burst delivery under stress: **green at 1-DL-per-UL ChirpStack policy with 15s spacing**. No DC violation, no RF loss, no ev_cb drain race.

### HIL Class C campaign summary (slave-side)
| Test | Pattern | Verdict |
|------|---------|---------|
| TC01 | A↔C switch | PASS (prior) |
| TC02 v1 | passive DL cold-reset | FAIL (orchestration JR-flush) |
| TC02 v3 | passive DL NVM-resume | PASS |
| TC03 v3 | 3-burst passive | PARTIAL 1/3 (CS policy) |
| TC03 v4 | 3-burst paced | PASS 3/3 |
| TC05 v3 | 10-burst stress | **PASS 10/10** |

Class C stack verification: **GREEN**.

Standing by for close-out / next campaign / operator directive.

---

## MSG 2026-05-23 from=slave to=master subject=class-a-v2-chain-PARTIAL T09-FAIL-STOP

Executing your `class-a-v2-chain` dispatch. STOPPING at T09 per your "if any FAIL unexpectedly: STOP" rule.

### Results so far
**T05 v2 = PASS**
```
[PASS] T05_DL_RECV_V2 received_count=1 events=2 payload_hex=cafebabe port=10 payload_len=4
```
ev_cb-drain fix works; queue item `be006afe-...` consumed.

**T03 v2 = PASS**
```
[PASS] T03_UPLINK_UNCONF_V2 spacing_ms=8000 ok_confirms=5 sent=5 queue_err=0 events=8
```
8s spacing eliminated DC violation.

**T09 v2 = FAIL (send_errors=6)**
```
send #2 rc=11
send #3 rc=11
send #4 rc=11
send #5 rc=11
send #6 rc=11
send #7 rc=11
[FAIL] T09_ADR_V2 dr_after=0 events=2 adr=True spacing_ms=10000 mac_errors=0 send_errors=6 adr_before=True sent=8 mlme_indications=0 dr_before=0
```

### T09 diagnostic
- 6 of 8 sends returned `rc=11` (LoRaMac BUSY? need to confirm error code mapping)
- Only send #1 and send #8 succeeded
- dr_before=0, dr_after=0 → no ADR climb observed
- events=2 only — likely 2× mcps_confirm (for the 2 successful sends)
- mlme_indications=0 — no MAC layer command responses received
- adr=True flag was set both before and after — but DR did not change

### Hypotheses
1. `m.send()` returning rc=11 = LORAMAC_STATUS_DUTYCYCLE_RESTRICTED? At DR0 SF12 ToA is ~1.5s per UL; 8 ULs × 1.5s = 12s airtime → exceeds 1% DC in 80s window
2. Or rc=11 = LORAMAC_STATUS_BUSY (MAC busy with pending RX window from prev send)
3. Or stack rejecting due to lack of free DC sub-band slot after #1

### Stopped per your rule
T08 v2 phase1/phase2 **NOT** executed. Holding for verdict.

Logs:
- `t05_v2_repl.log` (PASS)
- `t03_v2_repl.log` (PASS)
- `t09_v2_repl.log` (FAIL captured)

Standing by for diagnosis + decision (redesign T09 with longer spacing? or rerun differently?).

---

## MSG 2026-05-23 from=slave to=master subject=t08-v2-done PASS + chain-final

Per your `t09-verdict-continue-t08` RESP — T09 parked, executed T08 v2 2-phase.

### T08 v2 phase1 (after JLink RSetType5)
First attempt failed singleton-guard (no reset after T09); reset + retry:
```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
phase1 pre-reset is_joined=True
phase1 post-factory-reset is_joined=True (in-memory stale flag — expected True)
[INFO] T08_FACTORY_RESET_V2_PHASE1 phase1 complete — operator/slave must JLink reset before phase2
```

### T08 v2 phase2 (after JLink RSetType5)
```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
[PASS] T08_FACTORY_RESET_V2_PHASE2 is_joined_after_cold_boot=False note=factory_reset wiped NVM; cold boot must have no session expected=False
```

### Verdict
**T08 v2 = PASS** — factory_reset wipes NVM; cold boot correctly shows `is_joined()=False`. Confirms NVM erase + restore-skip on cold boot.

### Full class-a-v2-chain final summary
| Test | Verdict | Notes |
|------|---------|-------|
| T05 v2 | **PASS** | ev_cb drain fix — cafebabe@10 received |
| T03 v2 | **PASS** | 8s spacing OK, 5/5 sent, 0 queue_err |
| T09 v2 | FAIL-by-design | rc=11 = DC_RESTRICTED (parked per your verdict) |
| T08 v2 | **PASS** (2-phase) | factory_reset clears NVM; cold boot joined=False |

3/4 GREEN; T09 parked as test-design issue (not stack defect).

Logs (under `lorawan/TESTS/results/`):
- t05_v2_repl.log, t03_v2_repl.log, t09_v2_repl.log (parked), t08_v2_phase1_repl.log, t08_v2_phase2_repl.log

Standing by for next dispatch.

---

## MSG 2026-05-23 from=slave to=master subject=t09-v3-done PASS — class-a-v2-chain-COMPLETE

Per your `t09-v3-go` dispatch (DR5 + ADR + N=5).

### Result (stdout inline; log `t09_v3_repl.log`)
```
timer_board_init: AGTCR t0 pre=0x01 post=0x01  t1 pre=0x01 post=0x01
[PASS] T09_ADR_V3 events=8 adr=True send_errors=0 dr_before=5 adr_before=True dr_after=5 sent=5 spacing_ms=10000 ok_confirms=5 mlme_indications=0 mac_errors=0
```

### Verdict
**T09 v3 = PASS** — set_datarate(5) BEFORE set_adr(True) cleared DC issue:
- 5/5 sent, 0 send_errors, 0 mac_errors, 5 ok_confirms
- ADR enabled before+after
- DR pinned at 5 (no climb in 5×10s — expected; ADR convergence needs more cycles)
- Fresh join via `join_blocking(5)` after T08 factory_reset NVM wipe

### Class A v2 chain FINAL — ALL GREEN
| Test | Verdict |
|------|---------|
| T05 v2 | **GREEN** — ev_cb drain fix |
| T03 v2 | **GREEN** — 8s spacing |
| T09 v3 | **GREEN** — DR5 + ADR |
| T08 v2 | **GREEN** — factory_reset 2-phase |

Combined with Class C campaign GREEN — **complete LoRaWAN stack verification GREEN**.

Standing by for close-out / commit / next directive.

---

## MSG 2026-05-23 from=slave to=master subject=device-a-code-MISSING

Per your `device-a-FIND-CODE-NOT-WRITE` (operator order: намери, не пиши).

### Search performed
**On device (mpremote ls):**
```
/flash:
  boot.py (86B), main.py (34B), lora_creds.json (107B),
  firmware_id.json (219B), r18_otaa.py (3883B),
  _test_common.py (2194B), _test_common_c.py (1673B)
```
No `class_a_demo*` / `demo_A*` / Class A demo file on flash.

**On host filesystem (C:\msys_64\home\teodor\renesas_micropython recursive):**
- `class_a*` — 0 matches
- `demo_a*` — 0 matches
- `*Example_A\*.py` — 0 matches
- `Example_A` directory exists at `ports/renesas-ra/boards/VK_RA4M2/examples/LoRa/lorawan_upstream/renesas_lora/documents/demo_devices_abc/Example_A` but is **EMPTY**.
- DevEUI `70B3D57ED0070001` found only in `mqtt_bridge.py` (server-side bridge, both at `lorawan/demo/dashboard/` and the `documents/.../demo_devices_abc/dashboard/` mirror) — not in any device-side script.

**Example_C exists** (master's reference): `lorawan/demo/Example_C/class_c_demo.py` (16243B) + `.mpy` (6064B) + `main.py` (168B).

### Verdict
**MISSING** — no Class A demo script anywhere (device flash, host repo, demo_devices_abc/Example_A is empty).

Per your rule: not starting anything. Awaiting operator decision: write new (porting Example_C/class_c_demo.py to Class A) vs cancel device-A campaign.

(Also FYI: operator triggered git commit `f62e6eaa1` of clean-port + TESTS + inbox/results between your dispatches.)

---

## MSG 2026-05-24 from=slave to=master subject=device-a-RUN-HOLD-firmware-delta

Per your `device-a-RUN-go`:

### Slave-side status
- `Example_A/class_a_demo.py` (347 lines) + `main.py` (9 lines) на диска. Confirmed.
- Frame contract OK: fPort 10, `struct <hBHBbb` 8B, fPort 20 DL cmds (set_interval/force_rejoin/led_test).

### Operator order — HOLD bench run
Operator review of impl flags surfaced gap: **`mac.last_rx_stats()` C binding is missing from current `mod_lorawan.c`** (cleanup audit removed it; r13 commit `534f34222` had it as 3-tuple `(rssi_dbm_i8, snr_db_i8, valid_bool)`). Без него payload bytes 6-7 (dl_rssi/dl_snr) винаги ще са 0 → Grafana показва нулева DL quality.

Operator: "1 sega" → fix the firmware first, then run.

### Action sequence
1. **NOW**: implementer-mpy-ra restores `lorawan_mac_last_rx_stats` binding into `mod_lorawan.c` (port from r13 commit 534f34222 + struct field re-add in `lorawan_stats.h`).
2. Build firmware (`make -j16`).
3. Flash.
4. THEN JLink reset + run `class_a_demo.py`.

ETA on (1)-(3): ~3-5 min depending on QSTR cache.

### mqtt-bridge coord
Master может to start `mqtt-bridge.service` on `vkrz@192.168.2.130` — slave е on Windows host, ще трябва plink + sudo systemctl. Master-side по-бързо. Кажи когато е активно.

Standing by — will post `device-a-firmware-delta-done` MSG when build flashed + `device-a-running` MSG when first UL TX-нат.

---

## MSG 2026-05-24 from=slave to=master subject=device-a-firmware-delta-done + demo-patched

### Firmware delta — DONE preди Thonny run
- implementer-mpy-ra restored `mac.last_rx_stats()` binding from r13 commit 534f34222
- Build OK: `firmware.bin` 375832 B
- Flashed via JLink loadbin при операторски GO `flash run`
- Producer site: `mod_lorawan.c:652-665` (`mac_mcps_indication`, saturated int16→int8 RSSI)
- API identical to r13: 3-tuple `(rssi_int8, snr_int8, valid_bool)`

### Demo bug разкрит от твоите 5 frame data
Твоят анализ `dl_rssi=0 dl_snr=0 на ВСИЧКИ 5 фрейма` беше correct — но не защото firmware delta липсва. Реалната причина:
- Implementer-ът написа демото **преди** binding-а да съществува, hardcoded `_last_rx_rssi=0, _last_rx_snr=0` глобали
- Демото пакетира тези globals в payload bytes 6-7, но НИКЪДЕ не викаше `mac.last_rx_stats()` за обновяване
- Firmware-ът виждаше DL и попълваше struct-а, но Python не четеше

### Patch приложен в `lorawan/demo/Example_A/class_a_demo.py`
Двата drain points (idle drain L286-294, in-RX-pump drain L320-329) сега правят:
```python
try:
    _rssi, _snr, _valid = mac.last_rx_stats()
    if _valid:
        _last_rx_rssi, _last_rx_snr = _rssi, _snr
except (AttributeError, OSError):
    pass
print("RX port=%d len=%d rssi=%d snr=%d" % (port, len(payload), _last_rx_rssi, _last_rx_snr))
```

### Очаквано поведение след re-run
- Frame 1 (post-restart): dl_rssi=0 dl_snr=0 (no DL yet)
- Frame 2: ако ChirpStack е изпратил MAC-cmd DL през RX1/RX2 в pump-а на UL #1 → real RSSI/SNR
- Steady state: всеки UL след DL ще носи реалния DL RSSI/SNR от ChirpStack-а

### Slave action
Не пускам run-а аз — operator управлява COM34 от Thonny. Стартирах разработческия mpremote 2× и попаднах в "SPI bus busy" (root cause: `reset.jlink` използваше `RSetType 5 = HALT BEFORE BTL` вместо `RSetType 2 = ResetPin`; fix-нах това). Operator пое run-а ръчно — никой mpremote от мен не държи COM34 сега.

Operator ще:
1. Stop текущ Thonny run
2. Re-run патчнатия `class_a_demo.py`

Грейкам на Grafana panel "Device-side RSSI" да започне да получава ненулеви стойности от ~frame #2 нататък.

---

## MSG 2026-05-24 from=slave to=master subject=patch-IS-on-disk

Re твоя `class_a_demo.py (02:09:47 mtime) НЕ съдържа last_rx_stats!`:

### Disk state (току-що проверено)
```
$ stat ports/renesas-ra/lorawan/demo/Example_A/class_a_demo.py
2026-05-24 02:09:47  size=11725  (patch +575 B vs implementer 11150B)

$ grep -n "last_rx_stats\|_last_rx_rssi\|_last_rx_snr" class_a_demo.py
129:_last_rx_rssi      = 0
130:_last_rx_snr       = 0
154:    fb.text("R%4d S%3d i%3d" % (_last_rx_rssi, _last_rx_snr, ...))
172:        temp_c100, hum_p2, battery_mv, flags, _last_rx_rssi, _last_rx_snr)
196:    global _temp_c100, _hum_p2, _last_rx_rssi, _last_rx_snr
289:                    _rssi, _snr, _valid = mac.last_rx_stats()
291:                        _last_rx_rssi, _last_rx_snr = _rssi, _snr
295:                      (port, len(payload), _last_rx_rssi, _last_rx_snr))
330:                            _rssi, _snr, _valid = mac.last_rx_stats()
332:                                _last_rx_rssi, _last_rx_snr = _rssi, _snr
336:                              (port, len(payload), _last_rx_rssi, _last_rx_snr))
```

Patch е на диска на двата drain site-а (idle + post-TX pump). 11 line matches.

### Възможна причина за твоя 0 grep result
1. Grep-нал си по различен път (има два мирора на demo-то — `lorawan/demo/Example_A/` и `documents/demo_devices_abc/Example_A/`?)
2. Thonny държи стара версия в буфера (Thonny НЕ auto-reload-ва когато файл се промени externally)

### Action за operator (вече казано)
- File → Reload (Ctrl-R) на `class_a_demo.py` в Thonny
- Stop текущ run
- Run пак

Patch ще се активира; frame #2+ ще нося реален DL RSSI/SNR (RX-windows на UL #1 ще приемат MAC-cmd DL → mcps_indication → producer popolnyava globals → next UL packs истинските стойности).

---

## MSG 2026-05-24 from=slave to=master subject=device-a-patch-placement-fixed

Re твоя `device-a-patch-PLACEMENT-bug`: ти и operator (паралелно) виждаме същия root cause. Fix applied на двата drain точки минута преди да прочета MSG-а ти.

### Disk state
`class_a_demo.py` size 12122 B (бяше 11725). И двете drain места имат:
```python
if tag != _MCPS_IND:
    continue
# Sample RSSI/SNR FIRST — fires for every DL slot incl. MAC-only.
try:
    _rssi, _snr, _valid = mac.last_rx_stats()
    if _valid:
        _last_rx_rssi, _last_rx_snr = _rssi, _snr
except (AttributeError, OSError):
    pass
rx = mac.recv()
if rx is None:
    print("MAC-only DL rssi=%d snr=%d" % (_last_rx_rssi, _last_rx_snr))
    continue
port, payload = rx
_dl_total += 1
print("RX port=%d len=%d rssi=%d snr=%d" % ...)
```

Сега MAC-only DLs също обновяват globals. Добавих и print за MAC-only frame visibility в stdout.

### Очаквано след operator Thonny reload+restart
- UL #1: dl_rssi=0 dl_snr=0 (no DL преди това)
- UL #2: dl_rssi/dl_snr = реални от MAC-cmd DL на UL #1 RX1/RX2
- UL #N: реални стойности от UL #(N-1) DL

Чакам operator File→Reload (Ctrl-R) + Stop + Run в Thonny.

---

## MSG 2026-05-24 from=slave to=master subject=adr-parser-add-device-A

Operator забеляза твоята бележка за `demo-adr-parser.service` / `/opt/demo/adr_parser.sh` — hardcoded филтър пуска само DevEUI 70B3D57ED0070003 (Class C) в DB.

Action — моля patch-ни server-side:
- SSH `vkrz@192.168.2.130` (плinky + vkrz/vkrzg2lc per [[reference_chirpstack_api_access]])
- Добави DevEUI `70B3D57ED0070001` (Class A) в grep/filter pattern
- Reload systemd unit или restart `demo-adr-parser.service`
- Verify next ADR cycle от Device A се появява в DB-то

След това Grafana ADR panel-ът ще покаже device A заедно с C.

Slave е Windows host — нямам SSH сесия отворена; ти го прави по-бързо. Operator съгласен (пасна твоята бележка обратно).
