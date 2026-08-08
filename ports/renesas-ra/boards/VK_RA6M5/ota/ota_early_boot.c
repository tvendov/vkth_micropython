/*
 * VK_RA6M5 — OTA early-boot bootstrap.
 *
 * Runs from MICROPY_BOARD_STARTUP, BEFORE ra_init() and any peripheral
 * setup.  Looks at the OTHER slot's trailer; if it is marked PENDING and
 * its SHA-256 verifies, performs a cold trampoline jump into that slot.
 *
 * Layout (no separate bootloader — each firmware embeds this bootstrap):
 *
 *   0x00000000   Slot 0   1 MB (- last 128 B for trailer)
 *   0x000FFF80   Slot 0 trailer
 *   0x00100000   Slot 1   1 MB (- last 128 B for trailer)
 *   0x001FFF80   Slot 1 trailer
 *
 * After MICROPY_BOARD_STARTUP returns, main() continues to normal init —
 * unless we trampolined, in which case the new slot's reset handler runs
 * and reinitialises the world.
 */

#include <stdint.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "hal_data.h"
#include "r_flash_hp.h"

/* INTENTIONALLY DISABLED — this file is the Phase 1 reference skeleton.
 * To activate after live verification:
 *   1. Re-enable the guard below: change `#if 0` -> `#if MICROPY_PY_OTA`.
 *   2. Add this file to Makefile (USE_OTA=1) alongside mod_ota.c.
 *   3. In boards/VK_RA6M5/mpconfigboard.h (under USE_OTA):
 *        void ota_early_boot(void);
 *        #define MICROPY_BOARD_STARTUP() ota_early_boot()
 *   4. Either (a) build position-correct images per slot, or
 *      (b) replace jump_to_slot with R_FLASH_HP_BankSwap+reset.
 *      jump_to_slot in this file ASSUMES same image at both slot
 *      addresses, which only works when both are linked for that
 *      specific address.  See ota/README.md for the full plan.
 */
#if 0   /* not built — see ota/README.md */

#define SLOT0_BASE          (0x00000000UL)
#define SLOT1_BASE          (0x00100000UL)
#define SLOT_SIZE           (0x00100000UL)             /* 1 MB each */
#define TRAILER_OFFSET      (SLOT_SIZE - 128U)         /* last 128 B of slot */
#define TRAILER_SECTOR_OFF  (SLOT_SIZE - 0x8000U)      /* last 32 KB sector start */
#define MAGIC_OTAV          (0x5641544FU)              /* 'O''T''A''V' little-endian */
#define PAGE_SIZE           (128U)
#define SECTOR_SIZE         (0x8000U)

#define STATE_EMPTY          0xFF
#define STATE_PENDING        0x01
#define STATE_PENDING_VERIFY 0x02
#define STATE_GOOD           0x03
#define STATE_BAD            0xFE

typedef struct __attribute__((packed)) {
    uint32_t magic;        /* 'OTAV' = 0x5641544F LE */
    uint32_t version_int;
    uint32_t image_size;
    uint32_t build_hash;
    uint8_t  sha256[32];
    uint8_t  slot_state;
    uint8_t  retry_count;
    uint8_t  sig_present;
    uint8_t  reserved;
    char     version_str[12];
} ota_trailer_t;

extern flash_hp_instance_ctrl_t g_flash0_ctrl;
extern const flash_cfg_t        g_flash0_cfg;

/* Helper: take the SCB->VTOR register without including cmsis headers
 * (we may not have them available this early on some toolchains). */
static inline uint32_t read_vtor(void) {
    return *(volatile uint32_t *)0xE000ED08;
}

static int sha256_verify_slot(uint32_t slot_base, uint32_t image_size,
                              const uint8_t expected[32]) {
    mbedtls_sha256_context ctx;
    uint8_t out[32];
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx); return 0;
    }
    /* Internal flash is memory-mapped — feed mbedtls in 4 KB strides
     * to keep stack/cache pressure low. */
    uint32_t off = 0;
    while (off < image_size) {
        uint32_t n = (image_size - off);
        if (n > 4096) n = 4096;
        if (mbedtls_sha256_update(&ctx, (const uint8_t *)(slot_base + off), n) != 0) {
            mbedtls_sha256_free(&ctx); return 0;
        }
        off += n;
    }
    if (mbedtls_sha256_finish(&ctx, out) != 0) {
        mbedtls_sha256_free(&ctx); return 0;
    }
    mbedtls_sha256_free(&ctx);
    return memcmp(out, expected, 32) == 0;
}

/* Atomic-ish trailer state update.  Reads the trailer page, modifies
 * one byte, erases the last sector, and re-writes the trailer page. */
static int update_slot_state(uint32_t slot_base, uint8_t new_state) {
    uint8_t page_buf[PAGE_SIZE] __attribute__((aligned(4)));
    /* Source page lives in the LAST page of the slot's last sector. */
    memcpy(page_buf, (const void *)(slot_base + TRAILER_OFFSET), PAGE_SIZE);
    ((ota_trailer_t *)page_buf)->slot_state = new_state;

    if (R_FLASH_HP_Open(&g_flash0_ctrl, &g_flash0_cfg) != FSP_SUCCESS) {
        return 0;
    }

    __disable_irq();
    fsp_err_t err = R_FLASH_HP_Erase(&g_flash0_ctrl,
                                     slot_base + TRAILER_SECTOR_OFF, 1);
    if (err == FSP_SUCCESS) {
        err = R_FLASH_HP_Write(&g_flash0_ctrl,
                               (uint32_t)page_buf,
                               slot_base + TRAILER_OFFSET,
                               PAGE_SIZE);
    }
    __enable_irq();
    R_FLASH_HP_Close(&g_flash0_ctrl);
    return err == FSP_SUCCESS;
}

static void __attribute__((noreturn)) jump_to_slot(uint32_t slot_base) {
    /* Disable interrupts; the new slot's reset handler will re-enable. */
    __disable_irq();
    /* Re-point the vector table.  SCB->VTOR is at 0xE000ED08. */
    *(volatile uint32_t *)0xE000ED08 = slot_base;
    /* Set Main Stack Pointer from the new vector table (first word). */
    uint32_t new_msp = *(volatile uint32_t *)slot_base;
    uint32_t new_pc  = *(volatile uint32_t *)(slot_base + 4);
    __asm volatile (
        "msr msp, %0   \n"
        "bx  %1        \n"
        :: "r"(new_msp), "r"(new_pc) : "memory"
    );
    __builtin_unreachable();
}

void ota_early_boot(void) {
    /* Determine which slot we're running from.  After cold reset the CPU
     * starts at the address loaded by the linker — Slot 0 for our
     * normally-flashed image.  After a previous trampoline jump,
     * SCB->VTOR points to the other slot. */
    uint32_t my_base    = read_vtor();
    uint32_t other_base = (my_base == SLOT0_BASE) ? SLOT1_BASE : SLOT0_BASE;

    const ota_trailer_t *other = (const ota_trailer_t *)(other_base + TRAILER_OFFSET);

    /* Quick sanity: magic + plausible image size. */
    if (other->magic != MAGIC_OTAV) return;
    if (other->image_size == 0 || other->image_size > (SLOT_SIZE - PAGE_SIZE)) return;

    if (other->slot_state == STATE_PENDING) {
        /* New image waiting.  Verify SHA before trampolining. */
        if (sha256_verify_slot(other_base, other->image_size, other->sha256)) {
            /* Mark the slot as PENDING_VERIFY so the next boot can detect
             * a failed mark_good handshake. */
            update_slot_state(other_base, STATE_PENDING_VERIFY);
            jump_to_slot(other_base);
        } else {
            /* Image is corrupt — refuse to boot it. */
            update_slot_state(other_base, STATE_BAD);
        }
    } else if (other->slot_state == STATE_PENDING_VERIFY) {
        /* The other slot was claimed last boot but we ended up back here
         * — meaning that slot failed to call mark_good.  Mark it BAD so
         * we don't loop. */
        update_slot_state(other_base, STATE_BAD);
    }
    /* All other states (GOOD / EMPTY / BAD) → just continue with my_base. */
}

/* Public API helpers callable from mod_ota.c. */
uint32_t ota_my_slot_base(void) { return read_vtor(); }
uint32_t ota_other_slot_base(void) {
    uint32_t b = read_vtor();
    return (b == SLOT0_BASE) ? SLOT1_BASE : SLOT0_BASE;
}

#endif /* MICROPY_PY_OTA */
