# Session 2026-01-09 - ble_host_conn_config investigation

## Goal
Investigate where `ble_host_conn_config` should be defined for RA4W1 FSP BLE stack, and verify BLE library variant configuration vs linked library.

## Starting status (before changes)
- Workspace: c:\msys64\home\teodor\renesas_micropython
- No code changes made in this session yet.

## Key discoveries so far (read-only)
1. Build system links precompiled FSP BLE library for RA4W1 when MICROPY_HW_ENABLE_BLE=1:
   - ports/renesas-ra/Makefile:
     - BLE_LIB_DIR = lib/fsp/ra/fsp/lib/r_ble/cm4_gcc/compact
     - LIBS += -L$(BLE_LIB_DIR) -lr_ble

2. Board BLE config file currently sets library type to 0 with a comment claiming "Compact":
   - ports/renesas-ra/boards/EK_RA4W1/ra_cfg/fsp_cfg/r_ble_cfg.h
     - BLE_CFG_LIBRARY_TYPE (0)

3. Renesas FSP header defines library type constants:
   - lib/fsp/ra/fsp/inc/api/r_ble_api.h
     - BLE_LIB_EXTENDED = 0
     - BLE_LIB_BALANCE  = 1
     - BLE_LIB_COMPACT  = 2

This means the comment in r_ble_cfg.h appears inconsistent with FSP.

4. In-tree sources do not reference `ble_host_conn_config`:
   - `lib/fsp/ra/fsp/src/r_ble/r_ble.c`: no symbol reference
   - `lib/fsp/ra/fsp/src/rm_ble_abs/rm_ble_abs.c`: no symbol reference
   - Current working hypothesis: the `ble_host_conn_config` undefined-symbol report is either stale (older FSP), from a different library variant, or from a different object/library than the ones currently built.

5. Existing build log indicates successful link with compact BLE lib:
   - `ports/renesas-ra/build_clean_V1.log` includes a link command that uses:
     - `-Llib/fsp/ra/fsp/lib/r_ble/cm4_gcc/compact -lr_ble`
   - `firmware.elf` is produced without linker errors (in that log).

## Next actions
- Confirm with user whether to change EK_RA4W1 `BLE_CFG_LIBRARY_TYPE` to `BLE_LIB_COMPACT` (2) to match the linked library.
- If you still see a real `ble_host_conn_config` undefined symbol during link, run `nm -g`/`objdump -t` on the actual `libr_ble.a` used by the build (from the `cm4_gcc/compact` directory) and capture the exact object file that references it.
- Then either:
  - provide the missing application-side definition (if required by that library), or
  - switch to the correct prebuilt library variant (if we are linking the wrong one), or
  - regenerate the FSP-generated config sources that define it.

