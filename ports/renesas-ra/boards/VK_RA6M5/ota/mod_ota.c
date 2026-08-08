/*
 * VK_RA6M5 OTA flash glue — exposed to MicroPython as `_ota`.
 *
 * Phase 2.5 surface (no bootloader yet — we just program Slot B and
 * leave it for verification):
 *
 *   _ota.SLOT_A_BASE / _ota.SLOT_A_SIZE
 *   _ota.SLOT_B_BASE / _ota.SLOT_B_SIZE
 *   _ota.PAGE          = 128   (RA6M5 code-flash write granularity)
 *   _ota.SECTOR        = 32768 (RA6M5 code-flash sector size)
 *   _ota.erase_slot_b()              -> True on success
 *   _ota.write_slot_b(offset, data)  -> None  (data length must be 128-aligned)
 *   _ota.read_slot_b(offset, length) -> bytes (memory-mapped, no XIP toggle)
 *
 * Layout (R7FA6M5BH3CFC, 2 MB internal code flash):
 *
 *   0x00000000  bootloader        64 KB    (provisioned via JLink, Phase 1)
 *   0x00010000  Slot A — active   960 KB
 *   0x00100000  Slot B — staging  960 KB   (this module writes here)
 *   0x001F0000  reserved          64 KB
 *
 * `R_FLASH_HP_Write/Erase` runs from RAM (FSP places those functions in
 * a SRAM section), so it's safe to program Slot B while the active app
 * itself is executing from Slot A in the same flash array.
 *
 * To enable: set USE_OTA=1 in your make invocation; the Makefile picks
 * this file up via the USE_OTA guard and defines MICROPY_PY_OTA=1.
 */

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "hal_data.h"
#include "r_flash_hp.h"

#if MICROPY_PY_OTA

/* ---- legacy two-slot constants (kept for backward compat — not used
 * in new single-slot architecture) ---- */
#define BOOTLOADER_BASE        (0x00000000UL)
#define BOOTLOADER_SIZE        (0x00010000UL)   /* 64 KB (legacy) */
#define SLOT_A_BASE            (0x00010000UL)
#define SLOT_A_SIZE            (0x000F0000UL)   /* legacy 960 KB Slot A */
#define SLOT_B_BASE            (0x00100000UL)
#define SLOT_B_SIZE            (0x000F0000UL)   /* legacy 960 KB Slot B */

/* ---- new single-slot architecture (rescue at 0x0 + 1.97 MB app) ---- */
#define RESCUE_BASE            (0x00000000UL)
#define RESCUE_SIZE            (0x00008000UL)   /* 32 KB rescue stub */
#define APP_BASE               (0x00008000UL)
#define APP_SIZE               (0x001E8000UL)   /* 1.97 MB single app slot */
#define METADATA_BASE          (0x001F0000UL)
#define METADATA_SIZE          (0x00010000UL)   /* 64 KB last region */

#undef OTA_SECTOR_SIZE                    /* avoid clash with FSP macro */
#define OTA_SECTOR_SIZE      (0x8000U)        /* 32 KB code-flash sector */
#define OTA_SMALL_SECTOR     (0x2000U)        /* 8 KB sectors for first 64 KB of CF */
#define FLASH_HP_CF_PAGE       (128U)           /* 128 B write granularity */

/* The FSP-generated hal_data.c provides g_flash0_ctrl and g_flash0_cfg.
 * If they're not yet instantiated by the FSP Smart Configurator, the link
 * step will surface "undefined reference" — add the flash_hp module via
 * configuration.xml. */
extern flash_hp_instance_ctrl_t g_flash0_ctrl;
extern const flash_cfg_t        g_flash0_cfg;

static bool flash_open_done = false;

static fsp_err_t flash_open_once(void) {
    if (!flash_open_done) {
        fsp_err_t err = R_FLASH_HP_Open(&g_flash0_ctrl, &g_flash0_cfg);
        if (err != FSP_SUCCESS) {
            return err;
        }
        flash_open_done = true;
    }
    return FSP_SUCCESS;
}

/* ---------------- erase ---------------- */

static mp_obj_t ota_erase_slot_b(void) {
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    /* SLOT_B_SIZE / OTA_SECTOR_SIZE = 960 / 32 = 30 sectors. */
    uint32_t state = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Erase(&g_flash0_ctrl,
                                     SLOT_B_BASE,
                                     SLOT_B_SIZE / OTA_SECTOR_SIZE);
    mp_hal_quiet_timing_exit(state);
    return mp_obj_new_bool(err == FSP_SUCCESS);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ota_erase_slot_b_obj, ota_erase_slot_b);

/* ---------------- write ---------------- */

static mp_obj_t ota_write_slot_b(mp_obj_t offset_in, mp_obj_t data_in) {
    uint32_t offset = mp_obj_get_int(offset_in);
    mp_buffer_info_t bi;
    mp_get_buffer_raise(data_in, &bi, MP_BUFFER_READ);

    if (offset + bi.len > SLOT_B_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("write past end of Slot B"));
    }
    if ((bi.len % FLASH_HP_CF_PAGE) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("len must be a multiple of 128"));
    }
    if ((offset % FLASH_HP_CF_PAGE) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("offset must be 128-byte aligned"));
    }
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t state = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Write(&g_flash0_ctrl,
                                     (uint32_t)bi.buf,
                                     SLOT_B_BASE + offset,
                                     bi.len);
    mp_hal_quiet_timing_exit(state);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_write_slot_b_obj, ota_write_slot_b);

/* ---------------- read ---------------- */
/* Internal flash is memory-mapped — just copy out the requested bytes.
 * Avoids forcing the user to import uctypes for verification. */

static mp_obj_t ota_read_slot_b(mp_obj_t offset_in, mp_obj_t length_in) {
    uint32_t offset = mp_obj_get_int(offset_in);
    uint32_t length = mp_obj_get_int(length_in);
    if (offset + length > SLOT_B_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("read past end of Slot B"));
    }
    return mp_obj_new_bytes((const byte *)(SLOT_B_BASE + offset), length);
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_read_slot_b_obj, ota_read_slot_b);

/* ---------------- trailer state management ----------------
 * The trailer lives in the LAST 128-byte page of the slot's last 32 KB
 * sector.  To update one byte (e.g. slot_state) we must:
 *   1) read the entire page,
 *   2) erase the last sector,
 *   3) re-write the page with the modified state byte.
 *
 * This costs ~50 ms but is infrequent (one transition per OTA event).
 * Slot A self-write would brick a running app — only Slot B is exposed. */

#define TRAILER_PAGE_OFFSET   (SLOT_B_SIZE - FLASH_HP_CF_PAGE)        /* 0xEFF80 */
#define TRAILER_SECTOR_OFFSET (SLOT_B_SIZE - OTA_SECTOR_SIZE)          /* 0xE8000 */

static mp_obj_t ota_set_state_b(mp_obj_t state_in) {
    uint32_t state = mp_obj_get_int(state_in);
    if (state > 0xFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("state must fit a byte"));
    }
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    /* Snapshot the existing trailer page from memory-mapped flash. */
    uint8_t page[FLASH_HP_CF_PAGE] __attribute__((aligned(4)));
    memcpy(page, (const void *)(SLOT_B_BASE + TRAILER_PAGE_OFFSET),
           FLASH_HP_CF_PAGE);
    /* slot_state is at trailer offset 48 (see ota.py / ota_pack.py). */
    page[48] = (uint8_t)state;

    /* Erase the last sector (32 KB) — image lives at slot start; sector
     * boundary keeps the firmware bytes untouched as long as image_size
     * < SLOT_B_SIZE - OTA_SECTOR_SIZE = 928 KB. */
    uint32_t istate = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Erase(&g_flash0_ctrl,
                                     SLOT_B_BASE + TRAILER_SECTOR_OFFSET, 1);
    if (err == FSP_SUCCESS) {
        err = R_FLASH_HP_Write(&g_flash0_ctrl,
                               (uint32_t)page,
                               SLOT_B_BASE + TRAILER_PAGE_OFFSET,
                               FLASH_HP_CF_PAGE);
    }
    mp_hal_quiet_timing_exit(istate);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ota_set_state_b_obj, ota_set_state_b);

static mp_obj_t ota_read_trailer_b(void) {
    /* Trailer is the 64 valid bytes at the start of the trailer page. */
    return mp_obj_new_bytes((const byte *)(SLOT_B_BASE + TRAILER_PAGE_OFFSET), 64);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ota_read_trailer_b_obj, ota_read_trailer_b);

/* ---------------- generic slot trailer ops ----------------
 * Parameterised over slot_base — accepts SLOT_A_BASE or SLOT_B_BASE only.
 * Required for OTA Option B where the running app may live in either
 * slot and need to update its own (or the other slot's) trailer state. */

static void check_slot_base(uint32_t base) {
    if (base != SLOT_A_BASE && base != SLOT_B_BASE) {
        mp_raise_ValueError(MP_ERROR_TEXT("slot_base must be SLOT_A_BASE or SLOT_B_BASE"));
    }
}

static mp_obj_t ota_read_trailer(mp_obj_t slot_base_in) {
    uint32_t base = mp_obj_get_int(slot_base_in);
    check_slot_base(base);
    return mp_obj_new_bytes((const byte *)(base + TRAILER_PAGE_OFFSET), 64);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ota_read_trailer_obj, ota_read_trailer);

/* ---------------- generic slot erase/write/read (param-by-base) ---------- */

static mp_obj_t ota_erase_slot(mp_obj_t slot_base_in) {
    uint32_t base = mp_obj_get_int(slot_base_in);
    check_slot_base(base);
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t istate = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Erase(&g_flash0_ctrl, base,
                                     SLOT_B_SIZE / OTA_SECTOR_SIZE);
    mp_hal_quiet_timing_exit(istate);
    return mp_obj_new_bool(err == FSP_SUCCESS);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ota_erase_slot_obj, ota_erase_slot);

static mp_obj_t ota_write_slot(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint32_t base   = mp_obj_get_int(args[0]);
    uint32_t offset = mp_obj_get_int(args[1]);
    mp_buffer_info_t bi;
    mp_get_buffer_raise(args[2], &bi, MP_BUFFER_READ);
    check_slot_base(base);
    if (offset + bi.len > SLOT_B_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("write past end of slot"));
    }
    if ((bi.len % FLASH_HP_CF_PAGE) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("len must be a multiple of 128"));
    }
    if ((offset % FLASH_HP_CF_PAGE) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("offset must be 128-byte aligned"));
    }
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    uint32_t istate = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Write(&g_flash0_ctrl, (uint32_t)bi.buf,
                                     base + offset, bi.len);
    mp_hal_quiet_timing_exit(istate);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ota_write_slot_obj, 3, 3, ota_write_slot);

static mp_obj_t ota_read_slot(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint32_t base   = mp_obj_get_int(args[0]);
    uint32_t offset = mp_obj_get_int(args[1]);
    uint32_t length = mp_obj_get_int(args[2]);
    check_slot_base(base);
    if (offset + length > SLOT_B_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("read past end of slot"));
    }
    return mp_obj_new_bytes((const byte *)(base + offset), length);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ota_read_slot_obj, 3, 3, ota_read_slot);

static mp_obj_t ota_set_state(mp_obj_t slot_base_in, mp_obj_t state_in) {
    uint32_t base  = mp_obj_get_int(slot_base_in);
    uint32_t state = mp_obj_get_int(state_in);
    check_slot_base(base);
    if (state > 0xFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("state must fit a byte"));
    }
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    /* Snapshot the current trailer page into RAM. */
    uint8_t page[FLASH_HP_CF_PAGE] __attribute__((aligned(4)));
    memcpy(page, (const void *)(base + TRAILER_PAGE_OFFSET), FLASH_HP_CF_PAGE);
    page[48] = (uint8_t)state;
    /* Erase the last sector (32 KB) of the target slot, then re-write
     * the trailer page.  R_FLASH_HP_* runs from SRAM (PLACE_IN_RAM), so
     * even when `base == SLOT_A_BASE` and we're executing from Slot A,
     * the write goes through (provided the image_size + trailer area do
     * not overlap; image_size must be < SLOT_SIZE - SECTOR_SIZE). */
    uint32_t istate = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Erase(&g_flash0_ctrl,
                                     base + TRAILER_SECTOR_OFFSET, 1);
    if (err == FSP_SUCCESS) {
        err = R_FLASH_HP_Write(&g_flash0_ctrl,
                               (uint32_t)page,
                               base + TRAILER_PAGE_OFFSET,
                               FLASH_HP_CF_PAGE);
    }
    mp_hal_quiet_timing_exit(istate);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_set_state_obj, ota_set_state);

/* ---------------- DATA_FLASH (separate erase domain, 8 KB at 0x08000000) ----
 * RA6M5 data flash is byte-erasable in 64-byte blocks and supports
 * arbitrary 1/2/4-byte programming via the same R_FLASH_HP_* API.
 * It survives full code-flash reflash, making it the right home for the
 * "running-image is healthy" mark_good marker that doesn't depend on
 * which Slot is currently active. */

#define DATAFLASH_BASE   (0x08000000UL)
#define DATAFLASH_SIZE   (0x00002000UL)        /* 8 KB */
#define DATAFLASH_BLOCK  (64U)                  /* erase granularity */

static mp_obj_t ota_dataflash_read(mp_obj_t offset_in, mp_obj_t length_in) {
    uint32_t off = mp_obj_get_int(offset_in);
    uint32_t len = mp_obj_get_int(length_in);
    if (off + len > DATAFLASH_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("read past end of data flash"));
    }
    return mp_obj_new_bytes((const byte *)(DATAFLASH_BASE + off), len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_dataflash_read_obj, ota_dataflash_read);

static mp_obj_t ota_dataflash_write(mp_obj_t offset_in, mp_obj_t data_in) {
    uint32_t off = mp_obj_get_int(offset_in);
    mp_buffer_info_t bi;
    mp_get_buffer_raise(data_in, &bi, MP_BUFFER_READ);
    if (off + bi.len > DATAFLASH_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("write past end of data flash"));
    }
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    /* Erase the affected block(s) first.  Data flash erase is per 64-B
     * block and can be done one block at a time. */
    uint32_t block_lo = (off / DATAFLASH_BLOCK) * DATAFLASH_BLOCK;
    uint32_t block_hi = ((off + bi.len + DATAFLASH_BLOCK - 1) / DATAFLASH_BLOCK)
                        * DATAFLASH_BLOCK;
    uint32_t blocks   = (block_hi - block_lo) / DATAFLASH_BLOCK;

    uint32_t istate = mp_hal_quiet_timing_enter();
    fsp_err_t err = R_FLASH_HP_Erase(&g_flash0_ctrl,
                                     DATAFLASH_BASE + block_lo, blocks);
    if (err == FSP_SUCCESS) {
        err = R_FLASH_HP_Write(&g_flash0_ctrl,
                               (uint32_t)bi.buf,
                               DATAFLASH_BASE + off, bi.len);
    }
    mp_hal_quiet_timing_exit(istate);
    if (err != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_dataflash_write_obj, ota_dataflash_write);

/* ---------------- single-slot in-place upgrade (RAM-resident) ----------
 * `commit_in_place(src, size)` — erase the entire app region 0x00008000+
 * and reprogram it from `src` (must be a memory-mapped, non-CF address —
 * typically OSPI RAM at 0x68000000 after the staged image has been
 * copied there from /flash).  After the last page is programmed we
 * trigger NVIC_SystemReset() — the function NEVER returns to the
 * caller.  All flash work happens with IRQs disabled, FSP `R_FLASH_HP_*`
 * runs from RAM, and so does this very function (PLACE_IN_RAM_SECTION).
 *
 * If power fails mid-update the device will end up with a half-erased
 * app image.  The rescue stub at 0x00000000 is unaffected and will
 * spin (no valid app) — recovery requires JLink reflash.  This is the
 * accepted threat model; UART-recovery and bootloader-side CF write
 * are deferred (see HANDOFF.md). */

#include "bsp_api.h"   /* for PLACE_IN_RAM_SECTION via FSP common headers */

static int PLACE_IN_RAM_SECTION ota_commit_in_place_inner(uint32_t src,
                                                          uint32_t image_size) {
    /* Disable interrupts globally; nothing useful can run during the
     * 1+ minute of self-rewrite, and an ISR firing into now-erased
     * code-flash would HardFault. */
    __asm volatile ("cpsid i" ::: "memory");

    /* Walk sectors from APP_BASE upward until we've covered image_size
     * bytes.  RA6M5 has 8 small sectors (8 KB) for the first 64 KB of CF
     * and 32 KB sectors after that.  APP starts at 0x00008000 so the
     * first 32 KB of the slot still uses small sectors (sectors 4..7 of
     * the small region); from 0x00020000 on it's all 32 KB sectors. */
    uint32_t dst = APP_BASE;
    uint32_t end = APP_BASE + image_size;
    /* Round end up to page boundary so the final write covers the tail. */
    end = (end + FLASH_HP_CF_PAGE - 1U) & ~(FLASH_HP_CF_PAGE - 1U);
    if (end > APP_BASE + APP_SIZE) {
        return -3;
    }

    /* Erase loop. */
    uint32_t addr = APP_BASE;
    while (addr < end) {
        uint32_t blksz = (addr < 0x00020000UL) ? OTA_SMALL_SECTOR : OTA_SECTOR_SIZE;
        fsp_err_t e = R_FLASH_HP_Erase(&g_flash0_ctrl, addr, 1);
        if (e != FSP_SUCCESS) return -1;
        addr += blksz;
    }

    /* Program loop, page-sized chunks (128 B). */
    uint32_t off = 0;
    while (off < image_size) {
        uint32_t chunk = image_size - off;
        if (chunk > FLASH_HP_CF_PAGE) chunk = FLASH_HP_CF_PAGE;
        /* Pad final partial page with 0xFF in caller's buffer if needed —
         * caller is expected to pre-pad.  R_FLASH_HP_Write requires a
         * multiple of 128. */
        if ((chunk & (FLASH_HP_CF_PAGE - 1U)) != 0) return -3;
        fsp_err_t e = R_FLASH_HP_Write(&g_flash0_ctrl,
                                       (uint32_t)(src + off),
                                       (uint32_t)(dst + off), chunk);
        if (e != FSP_SUCCESS) return -2;
        off += chunk;
    }

    /* Done — reset.  NVIC_SystemReset is in CMSIS; the macro accesses
     * SCB->AIRCR which is in the system region (not CF), so safe. */
    *(volatile uint32_t *)0xE000ED0CUL = (0x05FAUL << 16) | (1UL << 2);
    __asm volatile ("dsb sy");
    while (1) { __asm volatile ("wfi"); }
}

static mp_obj_t ota_commit_in_place(mp_obj_t src_in, mp_obj_t size_in) {
    uint32_t src  = (uint32_t)mp_obj_get_int(src_in);
    uint32_t size = (uint32_t)mp_obj_get_int(size_in);
    if (size == 0 || size > APP_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("size out of range"));
    }
    /* Source must be page-aligned in length (caller pads); 4-byte aligned
     * for FSP write. */
    if ((size & (FLASH_HP_CF_PAGE - 1U)) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("size must be a multiple of 128"));
    }
    if (flash_open_once() != FSP_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    /* Never returns. */
    (void)ota_commit_in_place_inner(src, size);
    /* If we somehow get here, raise. */
    mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_commit_in_place_obj, ota_commit_in_place);

/* Tiny memcpy-to-arbitrary-address helper for staging the new image
 * into OSPI RAM before commit_in_place runs.  The Python-side wrapper
 * reads the .ota file from /flash and copies it 8 KB at a time into
 * 0x68000000 via this. */
static mp_obj_t ota_copy_to_ram(mp_obj_t dst_in, mp_obj_t src_in) {
    uint32_t dst = (uint32_t)mp_obj_get_int(dst_in);
    mp_buffer_info_t bi;
    mp_get_buffer_raise(src_in, &bi, MP_BUFFER_READ);
    /* Restrict destination to OSPI RAM range (0x68000000-0x68800000)
     * to avoid accidental writes elsewhere. */
    if (dst < 0x68000000UL || dst + bi.len > 0x68800000UL) {
        mp_raise_ValueError(MP_ERROR_TEXT("dst must be in OSPI RAM"));
    }
    memcpy((void *)dst, bi.buf, bi.len);
    return mp_obj_new_int_from_uint(bi.len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_copy_to_ram_obj, ota_copy_to_ram);

/* Direct memcpy from any source memory address (e.g. CF mapped at
 * 0x00000000-0x001FFFFF) into OSPI RAM.  Avoids the
 * `uctypes.bytearray_at` aliasing trick on the Python side which seems
 * to confuse the GC when the alias goes out of scope after a heavy
 * allocation pattern. */
static mp_obj_t ota_memcpy_to_ram(mp_obj_t dst_in, mp_obj_t src_in,
                                  mp_obj_t len_in) {
    uint32_t dst = (uint32_t)mp_obj_get_int(dst_in);
    uint32_t src = (uint32_t)mp_obj_get_int(src_in);
    uint32_t len = (uint32_t)mp_obj_get_int(len_in);
    if (dst < 0x68000000UL || dst + len > 0x68800000UL) {
        mp_raise_ValueError(MP_ERROR_TEXT("dst must be in OSPI RAM"));
    }
    /* Allow source from any of CF, SRAM, OSPI — but reject pointer-like
     * tiny addresses to catch wild calls. */
    if (src < 0x00000100UL) {
        mp_raise_ValueError(MP_ERROR_TEXT("src too low"));
    }
    memcpy((void *)dst, (const void *)src, len);
    /* Drain pending writes + cache (if present) before subsequent reads. */
    __asm volatile ("dsb sy" ::: "memory");
    __asm volatile ("isb"   ::: "memory");
    return mp_obj_new_int_from_uint(len);
}
static MP_DEFINE_CONST_FUN_OBJ_3(ota_memcpy_to_ram_obj, ota_memcpy_to_ram);

/* SHA-256 over an arbitrary memory region.  Lets the staging test
 * verify OSPI RAM contents without relying on uctypes aliases (which
 * appear to confuse MicroPython's GC after a heavy alloc pattern). */
#include "mbedtls/sha256.h"
static mp_obj_t ota_sha256_at(mp_obj_t addr_in, mp_obj_t len_in) {
    uint32_t addr = (uint32_t)mp_obj_get_int(addr_in);
    uint32_t len  = (uint32_t)mp_obj_get_int(len_in);
    if (addr < 0x00000100UL) {
        mp_raise_ValueError(MP_ERROR_TEXT("addr too low"));
    }
    mbedtls_sha256_context ctx;
    uint8_t digest[32];
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);   /* 0 = SHA-256 (not 224) */
    mbedtls_sha256_update(&ctx, (const unsigned char *)addr, len);
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    return mp_obj_new_bytes(digest, 32);
}
static MP_DEFINE_CONST_FUN_OBJ_2(ota_sha256_at_obj, ota_sha256_at);

/* memcmp two memory regions; returns offset of first difference, or
 * -1 if they're identical.  Used to debug staging integrity. */
static mp_obj_t ota_memcmp_at(size_t n_args, const mp_obj_t *args) {
    uint32_t a   = (uint32_t)mp_obj_get_int(args[0]);
    uint32_t b   = (uint32_t)mp_obj_get_int(args[1]);
    uint32_t len = (uint32_t)mp_obj_get_int(args[2]);
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (uint32_t i = 0; i < len; i++) {
        if (pa[i] != pb[i]) {
            return mp_obj_new_int_from_uint(i);
        }
    }
    return mp_obj_new_int(-1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ota_memcmp_at_obj, 3, 3, ota_memcmp_at);

/* ---------------- runtime introspection ---------------- */

static mp_obj_t ota_my_slot_base(void) {
    /* SCB->VTOR — Vector Table Offset Register at 0xE000ED08.
     * Reflects the current active slot. */
    return mp_obj_new_int_from_uint(*(volatile uint32_t *)0xE000ED08);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ota_my_slot_base_obj, ota_my_slot_base);

/* ---------------- module table ---------------- */

static const mp_rom_map_elem_t ota_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),       MP_ROM_QSTR(MP_QSTR__ota) },

    { MP_ROM_QSTR(MP_QSTR_BOOTLOADER_BASE),MP_ROM_INT(BOOTLOADER_BASE) },
    { MP_ROM_QSTR(MP_QSTR_BOOTLOADER_SIZE),MP_ROM_INT(BOOTLOADER_SIZE) },
    { MP_ROM_QSTR(MP_QSTR_SLOT_A_BASE),    MP_ROM_INT(SLOT_A_BASE) },
    { MP_ROM_QSTR(MP_QSTR_SLOT_A_SIZE),    MP_ROM_INT(SLOT_A_SIZE) },
    { MP_ROM_QSTR(MP_QSTR_SLOT_B_BASE),    MP_ROM_INT(SLOT_B_BASE) },
    { MP_ROM_QSTR(MP_QSTR_SLOT_B_SIZE),    MP_ROM_INT(SLOT_B_SIZE) },
    { MP_ROM_QSTR(MP_QSTR_PAGE),           MP_ROM_INT(FLASH_HP_CF_PAGE) },
    { MP_ROM_QSTR(MP_QSTR_SECTOR),         MP_ROM_INT(OTA_SECTOR_SIZE) },

    { MP_ROM_QSTR(MP_QSTR_erase_slot_b),   MP_ROM_PTR(&ota_erase_slot_b_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_slot_b),   MP_ROM_PTR(&ota_write_slot_b_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_slot_b),    MP_ROM_PTR(&ota_read_slot_b_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_state_b),    MP_ROM_PTR(&ota_set_state_b_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_trailer_b), MP_ROM_PTR(&ota_read_trailer_b_obj) },

    /* Generic Slot A / Slot B helpers — preferred for new code. */
    { MP_ROM_QSTR(MP_QSTR_read_trailer),   MP_ROM_PTR(&ota_read_trailer_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_state),      MP_ROM_PTR(&ota_set_state_obj) },
    { MP_ROM_QSTR(MP_QSTR_erase_slot),     MP_ROM_PTR(&ota_erase_slot_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_slot),     MP_ROM_PTR(&ota_write_slot_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_slot),      MP_ROM_PTR(&ota_read_slot_obj) },

    { MP_ROM_QSTR(MP_QSTR_DATAFLASH_BASE), MP_ROM_INT(DATAFLASH_BASE) },
    { MP_ROM_QSTR(MP_QSTR_DATAFLASH_SIZE), MP_ROM_INT(DATAFLASH_SIZE) },
    { MP_ROM_QSTR(MP_QSTR_DATAFLASH_BLOCK),MP_ROM_INT(DATAFLASH_BLOCK) },
    { MP_ROM_QSTR(MP_QSTR_dataflash_read), MP_ROM_PTR(&ota_dataflash_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_dataflash_write),MP_ROM_PTR(&ota_dataflash_write_obj) },

    { MP_ROM_QSTR(MP_QSTR_my_slot_base),   MP_ROM_PTR(&ota_my_slot_base_obj) },

    /* Single-slot architecture (1.97 MB app at 0x00008000). */
    { MP_ROM_QSTR(MP_QSTR_APP_BASE),       MP_ROM_INT(APP_BASE) },
    { MP_ROM_QSTR(MP_QSTR_APP_SIZE),       MP_ROM_INT(APP_SIZE) },
    { MP_ROM_QSTR(MP_QSTR_METADATA_BASE),  MP_ROM_INT(METADATA_BASE) },
    { MP_ROM_QSTR(MP_QSTR_METADATA_SIZE),  MP_ROM_INT(METADATA_SIZE) },
    { MP_ROM_QSTR(MP_QSTR_commit_in_place),MP_ROM_PTR(&ota_commit_in_place_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy_to_ram),    MP_ROM_PTR(&ota_copy_to_ram_obj) },
    { MP_ROM_QSTR(MP_QSTR_memcpy_to_ram),  MP_ROM_PTR(&ota_memcpy_to_ram_obj) },
    { MP_ROM_QSTR(MP_QSTR_sha256_at),      MP_ROM_PTR(&ota_sha256_at_obj) },
    { MP_ROM_QSTR(MP_QSTR_memcmp_at),      MP_ROM_PTR(&ota_memcmp_at_obj) },
};
static MP_DEFINE_CONST_DICT(ota_module_globals, ota_module_globals_table);

const mp_obj_module_t mp_module_ota = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ota_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR__ota, mp_module_ota);

#endif /* MICROPY_PY_OTA */
