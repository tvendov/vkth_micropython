# RA4W1 BLE - DECISIONS (single source of truth)

Last update: 2026-01-10 (IRQ8 external IRQ wrapper fix + build)

## Current Goal
- Make EK_RA4W1 MicroPython port provide real BLE functionality using Renesas FSP BLE stack (not just building).

## Confirmed Decisions
- Board: EK_RA4W1
- BLE stack choice: Native Renesas FSP `r_ble` (not NimBLE/HCI)
- Build integration: `MICROPY_HW_ENABLE_BLE=1` enables RA4W1 BLE sources and links `-lr_ble` (compact cm4_gcc) per `ports/renesas-ra/Makefile`.
- Logging discipline (user rule):
  - Keep full chat transcript at: `agents_guide/RA4W1_BLE/RA4W1 chat history ble project.txt`
  - Keep this decision log updated each session.
  - For each session, store results under a session directory in `agents_guide/RA4W1_BLE/`.

- **IRQ8 interrupt integration decision (2026-01-10):** Do NOT use FSP `g_external_irq_on_icu` for BLE IRQ8.
  Instead, expose `g_ble_external_irq` as an `external_irq_instance_t` backed by MicroPython's `ra_icu` dispatcher.
  Reason: pulling in FSP `r_icu.c` brings an ISR (`r_icu_isr`) that expects an FSP context pointer and can crash when the BSP context is NULL.

## Implementation Progress (2026-01-09 stub session)
✅ **DONE** - TODO stubs in ra_ble.c are now implemented:
- `ra_ble_gap_disconnect()` → calls `R_BLE_GAP_Disconnect(conn_hdl, 0x13)`
- `ra_ble_gatts_notify()` → calls `R_BLE_GATTS_Notification()`
- `ra_ble_gatts_indicate()` → calls `R_BLE_GATTS_Indication()`
- `ra_ble_gap_set_device_name()` → stores locally (FSP Compact has no SetDevName API)
- GAP callback now handles `BLE_GAP_EVENT_CONN_IND` and `BLE_GAP_EVENT_DISCONN_IND`

## Remaining Work
1. **Adv payload builder** - Use `ra_ble_gap_get_device_name()` to include name in advertising data
2. **GATTS callback** - Register with `R_BLE_GATTS_RegisterCb` for write events
3. ✅ **Build verification** - `make BOARD=EK_RA4W1 MICROPY_HW_ENABLE_BLE=1` succeeds.
4. **Hardware test** - Flash to board and test with phone BLE scanner
5. ✅ **Wire BLE RF interrupt (required)** - `g_ble_external_irq` implemented using `ra_icu` wrapper; builds successfully.

## Observations
- FSP Compact library does NOT have `R_BLE_GAP_SetDevName()` - workaround is local storage
- Confirmed: RA4W1 reserves ICU IRQ8 for BLE middleware (`ELC_EVENT_ICU_IRQ8`). EK_RA4W1 has an IRQ8 vector entry (`r_icu_isr`) and we now bind a callback via `ra_icu_set_callback(8, ...)`.
- Confirmed: `libr_ble.a` requires `g_ble_external_irq` (referenced from `rf_icu.o`); the port now provides it (ra_icu-backed wrapper).

- Bugfix: `ra_icu_priority_irq_no()` previously indexed `idx_to_irq_vec` using `irq_no` (wrong when IRQ lines are sparse), which can go out-of-bounds. It now uses `irq_no_to_irq_vec`.

## Rejected / Do Not Suggest Again
- (none recorded in this session)

## Need from user (keep short)
- Build test and hardware verification needed
- Next priority: adv payload with name, or GATTS write callback?

## Session Notes
- `agents_guide/RA4W1_BLE/session_2026-01-09_consistency_check/README.md`
- `agents_guide/RA4W1_BLE/session_2026-01-09_stub_impl/README.md` ← current session

New session:
- `agents_guide/RA4W1_BLE/session_2026-01-10_irq8_ble_interrupt/README.md`

New session:
- `agents_guide/RA4W1_BLE/session_2026-01-10_irq8_external_irq_wrapper_fix/README.md`

