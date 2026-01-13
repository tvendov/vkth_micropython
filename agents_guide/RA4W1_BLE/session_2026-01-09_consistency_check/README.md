# Session 2026-01-09 - RA4W1 BLE integration consistency check

## Goal
Check if the RA4W1 BLE integration is internally consistent across:
- C layer: ports/renesas-ra/ble/ra_ble.c (+ ra_ble.h)
- MicroPython module: ports/renesas-ra/modble_renesas.c (import renesas_ble)
- Build/flags: MICROPY_HW_ENABLE_BLE=1 and RA4W1-only selection
- FSP config: boards/EK_RA4W1/ra_cfg/fsp_cfg/r_ble_cfg.h

## What is consistent (OK)
1) Build gating is consistent
- ports/renesas-ra/Makefile enables native BLE only when:
  - CMSIS_MCU == RA4W1, and
  - MICROPY_HW_ENABLE_BLE == 1
- When enabled it adds:
  - ports/renesas-ra/ble/ra_ble.c
  - ports/renesas-ra/ble/ra_ble_events.c
  - ports/renesas-ra/modble_renesas.c
  - links the prebuilt FSP BLE library: lib/fsp/ra/fsp/lib/r_ble/cm4_gcc/compact/-lr_ble

2) Preprocessor flag path is consistent
- boards/EK_RA4W1/mpconfigboard.h provides a default:
  - MICROPY_HW_ENABLE_BLE = 0 (unless already defined)
- Makefile passes -DMICROPY_HW_ENABLE_BLE=1 when the feature is enabled, so the board default does not override it.

3) The module name and registration are consistent
- ports/renesas-ra/modble_renesas.c is compiled only when BLE is enabled (Makefile), and also guarded by:
  - #if MICROPY_HW_ENABLE_BLE
- It registers the module:
  - MP_REGISTER_MODULE(MP_QSTR_renesas_ble, ...)
- So Python side uses:
  - import renesas_ble

4) Event pumping hook is consistent
- ports/renesas-ra/mpconfigport.h installs MICROPY_INTERNAL_EVENT_HOOK to call:
  - modble_renesas_process_events()
  when MICROPY_HW_ENABLE_BLE is true.
- modble_renesas_process_events() calls ra_ble_process_events(), which calls R_BLE_Execute().
  This means the FSP host state machine can run from the MicroPython idle hook.

5) FSP library type config matches the linked library (NOW consistent)
- boards/EK_RA4W1/ra_cfg/fsp_cfg/r_ble_cfg.h sets:
  - BLE_CFG_LIBRARY_TYPE (2)
- lib/fsp/ra/fsp/inc/api/r_ble_api.h defines:
  - BLE_LIB_COMPACT (0x02)
- Makefile links the compact prebuilt library directory.

## What is NOT consistent / incomplete (functional gaps)
These are not build-structure mismatches, but API/behavior mismatches that will affect testing.

1) Several exported C APIs are still TODO (they currently do nothing)
- ports/renesas-ra/ble/ra_ble.c:
  - ra_ble_gap_disconnect(): TODO (commented-out R_BLE_GAP_Disconnect)
  - ra_ble_gatts_notify(): TODO (commented-out R_BLE_GATTS_Notification)
  - ra_ble_gatts_indicate(): TODO (commented-out R_BLE_GATTS_Indication)
  - ra_ble_gap_set_device_name(): TODO (commented-out R_BLE_GAP_SetDevName)

Impact:
- ports/renesas-ra/modble_renesas.c exposes renesas_ble.notify(...).
  That call will currently return success from the wrapper, but will not actually notify on-air.

2) GAP callback currently pushes only a subset of events
- ra_ble_gap_cb() in ra_ble.c handles:
  - BLE_GAP_EVENT_STACK_ON -> BLE_EVT_STACK_READY
  - BLE_GAP_EVENT_ADV_ON   -> BLE_EVT_GAP_ADV_STARTED
  - BLE_GAP_EVENT_ADV_OFF  -> BLE_EVT_GAP_ADV_STOPPED
- There is no implemented push for connect/disconnect indications yet.

Impact:
- renesas_ble.on("connect", ...) / on("disconnect", ...) may never fire even if a phone connects.

3) Advertising payload/device name behavior is not yet end-to-end
- modble_renesas.advertise(name, interval_ms) calls:
  - ra_ble_gap_set_device_name(name, ...)
  - ra_ble_gap_start_advertising(...)
- But ra_ble_gap_set_device_name() is TODO, and advertise currently does not build adv/scan-rsp payload containing the name.

Impact:
- Advertising may start, but the device name may not be visible (depends on defaults inside the prebuilt stack).

4) Potential link-time requirements from the prebuilt library
- When inspecting extracted objects from libr_ble.a, some objects reference globals like:
  - g_ble_external_irq
  - g_ble_rf_config
  (observed as undefined in relpf_board_init.o)

Impact:
- If these are not provided by the generated RA Smart Configurator code or by port glue, linking can fail or runtime can be wrong.
  This must be verified against the actual link line + final firmware.elf symbol table.

## Summary (real status)
- The build flags, module registration, and idle-hook pumping path are consistent.
- The integration is not feature-complete: several key BLE operations are stubbed (notify/indicate/disconnect/name) and important GAP events are not surfaced.
- FSP library type selection (Compact) is consistent with the linked library in the current tree.

