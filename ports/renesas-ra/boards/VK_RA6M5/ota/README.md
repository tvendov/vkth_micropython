# OTA firmware update for VK_RA6M5

A 4-piece skeleton for Over-The-Air firmware updates on VK_RA6M5.
None of this is wired into the default build — the directory is opt-in
so the working firmware at `0x00000000` is not disturbed.

## Memory layout (proposed)

The RA6M5 has 2 MB of internal code flash split into two banks (Bank 0
and Bank 1, 1 MB each).  We use the dual-bank ability to swap the
active firmware atomically.

```
            ┌──────────────────────────────────┐
0x00000000  │  Bootloader   (0x10000 = 64 KB)  │  ← never overwritten by OTA
            ├──────────────────────────────────┤
0x00010000  │  Active app   (0x100000 = 1 MB)  │  ← MicroPython firmware
            ├──────────────────────────────────┤
0x00110000  │  Staging area (0x80000 = 512 KB) │  ← new firmware lands here
            ├──────────────────────────────────┤
0x00190000  │  /flash FS    (0x70000 = 448 KB) │  ← unchanged from current
            └──────────────────────────────────┘
```

The trade-off vs the current `vk_ra6m5.ld` (app at 0x00000000, no
staging) is that the active app shrinks from ~1.6 MB to 1 MB and the
filesystem drops by 64 KB to make room for the bootloader.  Most
firmware images today are 480 KB so 1 MB is plenty.

## The four pieces

### 1. `bootloader.c` (skeleton)

Minimal bootloader that lives at `0x00000000`:

- Reads the *boot flag* word at the start of the staging area
  (`0x00110000`).
- If the flag is `OK_TO_SWAP`, copies staging → active (using
  `R_FLASH_HP_Erase` + `R_FLASH_HP_Write` from FSP), clears the
  flag, then jumps to `*(uint32_t *)(0x00010004)` (Reset_Handler of
  the app).
- If the flag is anything else, jumps directly to the app.

This file is a **template**.  To build it as a standalone bootloader
you need:

- An e2 studio "Bare-Metal Bootloader" project, OR
- A standalone GCC build with its own linker script that places the
  bootloader at `0x00000000-0x0000FFFF` and reserves the rest as
  unused.

Pre-built MCUboot for RA6M5 is available from Renesas FSP (FSP
example `app_lvd_bootloader_ra6m5`) and is the recommended path —
it adds image signature verification on top of what we sketch here.

### 2. `vk_ra6m5_app.ld` (alternate linker)

Same as `vk_ra6m5.ld` but with `FLASH` origin shifted to
`0x00010000` (after the 64 KB bootloader).  Build the MicroPython
app with `LD_FILES=boards/VK_RA6M5/ota/vk_ra6m5_app.ld`.

### 3. `mod_ota.py` (Python helper)

Module that runs on the active firmware:

```python
import ota
ota.write_bin("/flash/firmware.bin")   # programs staging
ota.commit()                            # sets boot flag, resets
```

Internally uses `machine.Flash` block-device or direct
`R_FLASH_HP_Write` via a small C glue (preferred).

### 4. `flash_glue.c` (C helper for `mod_ota.py`)

Wraps `R_FLASH_HP_Erase` / `R_FLASH_HP_Write` for the staging area.
Internal flash operations on RA6M5 are protected — they cannot run
out of code flash, only out of SRAM.  FSP handles that automatically
by placing `R_FLASH_HP_Write` in the `.fsp_warm_start` section, but
the wrapper has to disable interrupts for the duration.

## Workflow

1. **Provision once**: build & flash the bootloader at 0x00000000
   (e2 studio FSP project).  Then flash the new app linker layout
   at 0x00010000.
2. **OTA upgrade**:
   - Upload `firmware_v2.bin` to the device's filesystem (e.g. via
     WebREPL `webrepl_cli.py firmware_v2.bin :firmware_v2.bin`).
   - On the device:
     ```python
     import ota
     ota.write_bin('/flash/firmware_v2.bin')   # programs staging
     ota.commit()                               # sets flag + resets
     ```
   - Bootloader runs, sees flag, swaps, jumps to the new firmware.
3. **Roll-back**: if the new firmware fails to boot
   (no `mark_ok()` call within 30 s), the bootloader can roll
   back to the previous slot.  Requires keeping the old image in a
   second staging slot — out of scope for this minimal skeleton.

## Why this is a skeleton

What's here:

- Memory layout decision
- Alternate linker
- Python helper for write + commit
- C glue for in-app flash programming

What's missing (you supply):

- The actual bootloader binary built and flashed at 0x00000000.
  Use Renesas FSP's `app_lvd_bootloader_ra6m5` as the production
  starting point — it handles flash/swap, signature verification,
  and roll-back.

## References

- Renesas FSP User's Manual, "Bootloader and Firmware Update"
  application note.
- MCUboot for RA: https://github.com/renesas/fsp (path
  `ra/fsp/src/r_flash_hp/`).
- AN-R01AN5781EJ: "Bootloader and OTA Firmware Update for RA Family".
