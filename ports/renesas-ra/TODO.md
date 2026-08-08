# renesas-ra port — TODO

Open architectural / cleanup items captured during the VK_RA4M2 LoRaWAN clean-port
debugging arc (2026-05-22 / 2026-05-23). Not for immediate implementation —
landing pad for next session(s).

---

## 1. ~~Port-wide soft-reset hook registry~~  *(CLOSED 2026-05-23)*

**Status: CLOSED by `main.c:482` patch — `NVIC_SystemReset()` replaces `goto soft_reset`.**

Soft reset now triggers SYSRESETREQ → Reset_Handler runs → BSS zeroed + .data re-copied.
Every C module sees fresh static state on every soft reset, identical to power-on.
No per-module cleanup hooks needed; no linker-section registry; no main.c surface area.

Caveat: peripheral registers retain prior config (per RA4M2 §6 SYSRESETREQ scope —
does NOT reset AGT/SCI/GPIO blocks). Each module's ctor init path overwrites
relevant regs on next construction → effectively clean for normal use.

Trade-offs accepted:
- Soft reset cost: ~10 ms full CPU reboot (vs prior ~50 ms mp_init re-run) — same UX
- USB CDC REPL bounces (re-enumeration) — minor visible glitch
- All RAM state lost (Python heap, C statics, even `pyb_thread_*` if any)

If any future requirement demands selective preservation across soft reset, revisit
this decision — but the simplicity gain over a hook registry is substantial.

---

## 2. Vendor pristine compliance audit  *(priority: HIGH)*

`feedback_ren_lorawa_pristine_reference` rule (operator 2026-05-23): `lorawan/` must
mirror `ren_lorawa/` pristine Renesas SDK; fixes go in board-layer/binding, never
in vendor pristine.

Edits in vendor files that need cleanup or justification:

- `lorawan/radio/radio.c:786-812` — `RadioSetModem` rewrite + new `RadioApplyLoRaSyncWord*`
  helpers (added during sync-word fix). Currently in vendor file. Decide:
  - (a) Revert to pristine and move sync-word handling to board layer (radio adapter
    function called from binding before each RX)
  - (b) Document why vendor edit is needed; submit upstream patch to Renesas
  - (c) Leave as-is + add `// PORT-EDIT:` markers + delta-doc

- `lorawan/radio/radio.c:1094` — `RadioApplyLoRaSyncWordFromMib()` call in
  `RadioSetRxConfig`. Same decision tree.

- `lorawan/radio/radio.c:669-679` — new static helpers. Same.

- `lorawan/radio/radio.c:1568, 1600` — Class C continuous-RX `SX126xSetRx(0x0)`.
  Per memory `project_r11_classc_baseline`, r10/r11 had `0xFFFFFF` here. Vendor
  pristine reverted. Check if Renesas SDK `ren_lorawa/` has 0x0 or 0xFFFFFF. If
  0x0 (pristine), Class C will silently break in our port; need board-layer workaround.

Action: diff `ports/renesas-ra/lorawan/radio/radio.c` against `ren_lorawa/`-equivalent
pristine file; identify every PORT-EDIT; decide retain/revert/move per item.

---

## 3. `mac.join()` API — synchronous helper or document tight-poll requirement  *(priority: MEDIUM)*

### Problem (confirmed empirically 2026-05-23)

LoRaWAN OTAA join's RX1/RX2 timing budget is **±10 ms** (SystemMaxRxError). Vendor
`LoRaMacProcess()` is the foreground service drain — it must be called frequently
during active RX windows or the chip Rx start gets delayed by the polling gap,
causing RX2_TIMEOUT.

A naive Python script with `time.sleep_ms(500)` between `m.process()` calls fails
OTAA join even though the firmware is correct. A tight `while: m.process()` loop
joins reliably (~5.2 s for SF7 join).

`mac.set_min_rx_symbols(24)` widens the chip RX window enough to absorb ~50 ms of
polling slop — masks the wrapper-side timing bug. NOT a fix for the underlying
LoRaMac requirement.

### Options

- **A** — `mac.join_blocking(timeout_ms=15000)` C binding that drains
  `LoRaMacProcess()` internally until `MlmeConfirm` or timeout. Renesas SDK
  reference patterns use this style.
- **B** — Document tight-poll requirement in `mod_lorawan.c` docstring and
  examples; add `mac.poll_until(predicate, timeout_ms)` Python helper that
  encapsulates the drain pattern.
- **C** — Leave `mac.join()` as fire-and-forget; rely on user awareness +
  `set_min_rx_symbols(24)` board policy in `init_defaults`.

Recommendation: **A** for clean default UX, **B** as explicit-async escape hatch
for advanced users. Both can coexist.

### Open subtask — `init_defaults` policy

Currently `init_defaults` does NOT set `MIB_MIN_RX_SYMBOLS` (Renesas pristine
default = 6). With option A landed, this default is correct; relying on
documented tight-poll architecture. If sticking with C polling cadence below
~50 ms, leaving the default at 6 is fine.

---

## 4. RA4M2-specific `LORAMAC_STACK_PROCTIMEMS_RX1ON / RX2ON`  *(priority: LOW; cosmetic)*

`lorawan/mac/LoRaMacConfig.h:82-91` has chip-specific constants for `__RA0E1__`,
`__RA0E2__`, else (RA2-class). RA4M2 falls into the `else` (9 ms) — not specifically
tuned for Cortex-M33 @ 100 MHz where MAC dispatch is faster (~1 ms measured).

Effect: `WindowOffset -= 9` opens the RX window 8 ms too early on RA4M2. With tight
`mac.join_blocking()` loop (TODO #3) this becomes harmless; with sluggish polling
it compounds slop.

Action: when migrating to clean tight-loop pattern (TODO #3), measure actual
RA4M2 RX1 dispatch latency end-to-end and add `#if defined(__RA4M2__)` branch
in `LoRaMacConfig.h` with the measured value. Requires vendor edit — coordinate
with TODO #2 compliance audit.

Alternative without vendor edit: expose `LoRaMacBoardGetStackProcessTime` weak
hook in vendor stack (1 keyword change in `LoRaMac.c:6445`) and provide strong
override from board layer.

---

## 5. ~~SX1262 chip reset on soft reboot~~  *(STILL OPEN — TODO #1 doesn't fully cover)*

`main.c:482` `NVIC_SystemReset()` resets the RA4M2 CPU but does NOT pulse the
SX1262 NRESET pin (external GPIO line wired to SX1262). The chip retains its
prior operating mode (RX_RUNNING, COLD_SLEEP, BUSY-stuck-high after fault).

After firmware reboot, the next `lorawan.Mac()` ctor calls `sx126x_board_init()`
in `lorawan/boards/vk_ra4m2_sx126x/sx126x-board.c` — that function MUST pulse
NRESET via GPIO to bring the chip to known state. Confirm this is done; if not,
add `SX126xReset()` call to the board-init.

Action: audit `sx126x-board.c::sx126x_board_init()`; ensure NRESET pulse on every
init. Cross-test with hung-chip case (force chip into RX, then `machine.soft_reset()`,
expect clean ctor).

---

## 6. ~~Documentation — soft-reset invariants~~  *(SIMPLIFIED by TODO #1 closure)*

After TODO #1 closure: soft reset behaves as hard reset on this port. The doc
becomes trivial.

Write a short note in `boards/agents_guide/soft_reset_invariants.md` or in port
README:

> On renesas-ra, `machine.soft_reset()` triggers `NVIC_SystemReset()` for full
> CPU reboot (BSS zeroed, .data re-copied via Reset_Handler). Equivalent to
> external NRESET pin pulse except: peripheral registers retain prior config
> (SYSRESETREQ scope per RA4M2 §6), external chips on GPIO lines (SX1262, etc.)
> not pulsed. Module init paths overwrite peripheral regs on next construction.

That's the whole story. No further doc needed.

---

## 7. Cleanup — retire dead code  *(priority: LOW)*

- `lorawan/boards/vk_ra4m2_sx126x/timer-board.c` (port-aware adapter, 2-arg
  `timer_board_init(mp_obj_t, mp_obj_t)`) is **dead code** per `lorawan.mk:11-18`
  ("VK adapter retired"). Source file still on disk; not compiled. Either delete
  or guard with `#if 0` to eliminate naming-collision risk with linked
  `lorawan_softreset.c::timer_board_init(void)`.

- `main.c:301-307` `lorawan_softreset_agt_release()` extern call is now redundant
  (TODO #1 closure makes BSS-zeroed reset trivialize the need). Leave for one
  iteration; remove after confirming HIL still passes without it.

- `INTERCOM_FILES/STATE/` audit docs accumulated during the May session — keep
  the architectural audit docs (`timer_1to1_vendor_reuse_design_audit.md`,
  `timer_board_port_adapter_impl.md`) as reference; retire stale state files.

---

## 8. HIL soak test for OTAA stability  *(priority: HIGH after TODO 3)*

Once `mac.join_blocking()` exists (TODO 3), run:

1. Cold-reset → join → assert joined=True
2. `m.send(...)` Class A uplink with confirmed delivery
3. Repeat 100× cold-reset → join cycles
4. Capture join success rate, average time-to-joined, RX2_TIMEOUT incidence
5. Pass criterion: >95% join success on SF7-SF12, ChirpStack confirms session

Cross-correlate with SenseCap gateway log for any RF anomalies (DC exhaustion,
gateway scheduling delays).

Additionally: test 100× `machine.soft_reset()` cycles with TODO #1 patch in place
to confirm full RAM state hygiene. Pass criterion: no "already constructed"
errors, no orphan FSP open flags, no AGT/DMAC reservation leaks across cycles.

---

## Cross-references to memory entries

- `project_clean_port_otaa_join_2026_05_23` — first OTAA join 2026-05-23 03:00
- `feedback_ren_lorawa_pristine_reference` — vendor pristine policy 2026-05-23
- `project_lorawan_otaa_success` — original v0 root causes (SET_SLEEP, MinRxSymbols)
- `project_r11_classc_baseline` — Class C continuous-RX 0xFFFFFF history
- `reference_vk_ra4m2_sosc_xtal` — SOSC crystal on P214/P215

---

*Captured 2026-05-23 ~03:30 +03:00. Updated 2026-05-23 ~03:50 (TODO #1 closure
via NVIC_SystemReset patch). Update by adding new sections; do not overwrite
without operator authorization.*
