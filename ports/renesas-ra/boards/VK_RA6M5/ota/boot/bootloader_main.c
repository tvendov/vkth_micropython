/*
 * VK_RA6M5 OTA bootloader — minimal A/B chooser at 0x00000000.
 *
 * Lives in 64 KB at 0x00000000.  On every cold reset:
 *
 *   1. Read trailer at end of Slot A   (0x000FFF80, last 128 B page).
 *   2. Read trailer at end of Slot B   (0x001EFF80).
 *   3. Score each slot:
 *        invalid magic / BAD / EMPTY  -> -1
 *        PENDING                       -> base + 1000
 *        PENDING_VERIFY                -> base + 500
 *        GOOD                          -> base + 10000
 *        unknown state                 -> base
 *      where base = version_int.
 *   4. Boot the higher-scored slot.  Ties go to Slot A.
 *   5. If neither slot has a valid OTA trailer (clean / first boot),
 *      blindly boot Slot A.  Slot A's reset vector is at 0x00010004.
 *
 * The bootloader does NOT update trailer state.  All transitions
 * (PENDING -> PENDING_VERIFY, PENDING_VERIFY -> GOOD/BAD) are done by
 * the running app via mod_ota / ota.py.  This keeps the bootloader
 * trivial and side-effect-free; the cost is that retry-count tracking
 * has to live in DATA_FLASH and be advanced by the app side.
 *
 * SHA-256 verification is NOT done in the bootloader: the app already
 * verifies SHA before stamping PENDING and again before mark_good.
 * Adding mbedtls_sha256 to the bootloader doubles its size with no
 * practical security benefit in this threat model.  Re-add later if
 * trailer integrity becomes a concern (e.g. via ECDSA).
 *
 * Build: see ota/boot/Makefile.  Output goes to ota/boot/build/
 * (bootloader.bin, bootloader.hex, bootloader.elf).
 *
 * Provision (one-time, JLink):
 *      JLink.exe -CommanderScript ota/boot/jlink_provision.txt
 * which erases the chip, flashes bootloader at 0x00000000 and
 * firmware_slotA.hex at 0x00010000.
 */

#include <stdint.h>
#include <string.h>

#include "uECC.h"
#include "WjCryptLib_Sha256.h"
#include "bootloader_pubkey.h"
#include "bootloader_fcu.h"

#define SLOT_A_BASE          0x00010000UL
#define SLOT_B_BASE          0x00100000UL
#define SLOT_SIZE            0x000F0000UL    /* 960 KB */
#define TRAILER_OFFSET       (SLOT_SIZE - 128U)   /* 0xEFF80 */
#define SIG_OFFSET           (TRAILER_OFFSET + 64U)   /* 64 B sig follows trailer in same page */
#define MAGIC_OTAV           0x5641544FU      /* 'OTAV' little-endian */

#define STATE_EMPTY            0xFFu
#define STATE_PENDING          0x01u
#define STATE_PENDING_VERIFY_2 0x02u   /* first verify attempt, 2 retries left */
#define STATE_GOOD             0x03u
#define STATE_PENDING_VERIFY_1 0x12u   /* second verify attempt, 1 retry left */
#define STATE_BAD              0xFEu

typedef struct __attribute__((packed)) {
    uint32_t magic;
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

/* ---------------- DATA_FLASH retry-counter helpers ----------------------
 * Layout (in the first 64-B block of DATA_FLASH at 0x08000000):
 *   off 0x00  4 B  magic 0xC0FFEE01 (uninit = all 0xFF on virgin board)
 *   off 0x04  4 B  retry_a  (0..3, or 0xFFFFFFFF uninit = treat as 3)
 *   off 0x08  4 B  retry_b
 *   off 0x0C  4 B  reserved
 *   ... 48 bytes of 0xFF padding
 *
 * Update strategy (one counter per boot): read whole block, modify
 * 4 B word, erase, rewrite.  ~5 ms.  Only done for the slot we are
 * about to jump into. */

static int df_initialized = 0;

static uint32_t df_get_retry(uint32_t slot_base) {
    uint32_t magic, retry;
    bootloader_df_read(OTA_DF_OFF_MAGIC, &magic, 4);
    if (magic != OTA_DF_MAGIC_VALUE) return OTA_DF_INITIAL_RETRY;
    uint32_t off = (slot_base == SLOT_A_BASE) ? OTA_DF_OFF_RETRY_A
                                              : OTA_DF_OFF_RETRY_B;
    bootloader_df_read(off, &retry, 4);
    if (retry == 0xFFFFFFFFUL) return OTA_DF_INITIAL_RETRY;
    return retry;
}

static int df_write_block_with_changes(uint32_t magic_val,
                                       uint32_t retry_a, uint32_t retry_b) {
    uint8_t block[DATA_FLASH_BLOCK];
    /* Build the new 64-B image: 4 fields + 0xFF padding. */
    for (uint32_t i = 0; i < DATA_FLASH_BLOCK; i++) block[i] = 0xFF;
    *((uint32_t *)(block + OTA_DF_OFF_MAGIC))   = magic_val;
    *((uint32_t *)(block + OTA_DF_OFF_RETRY_A)) = retry_a;
    *((uint32_t *)(block + OTA_DF_OFF_RETRY_B)) = retry_b;
    /* Single-session erase + write — re-entering PE mode between erase
     * and program triggered FCU FESETERR in earlier debug. */
    return bootloader_df_erase_and_write(DATA_FLASH_BASE, block, DATA_FLASH_BLOCK);
}

static void df_init_if_needed(void) {
    if (df_initialized) return;
    df_initialized = 1;
    uint32_t magic;
    bootloader_df_read(OTA_DF_OFF_MAGIC, &magic, 4);
    if (magic == OTA_DF_MAGIC_VALUE) return;
    /* Virgin DATA_FLASH or stale magic — initialise to (3, 3). */
    df_write_block_with_changes(OTA_DF_MAGIC_VALUE,
                                OTA_DF_INITIAL_RETRY,
                                OTA_DF_INITIAL_RETRY);
}

static int df_set_retry(uint32_t slot_base, uint32_t value) {
    df_init_if_needed();
    uint32_t a, b;
    bootloader_df_read(OTA_DF_OFF_RETRY_A, &a, 4);
    bootloader_df_read(OTA_DF_OFF_RETRY_B, &b, 4);
    if (a == 0xFFFFFFFFUL) a = OTA_DF_INITIAL_RETRY;
    if (b == 0xFFFFFFFFUL) b = OTA_DF_INITIAL_RETRY;
    if (slot_base == SLOT_A_BASE) a = value;
    else                          b = value;
    return df_write_block_with_changes(OTA_DF_MAGIC_VALUE, a, b);
}

/* SHA-256 over a memory range.  Streams to keep the working set small. */
static void sha256_range(const uint8_t *base, uint32_t length, uint8_t out[32]) {
    Sha256Context ctx;
    Sha256Initialise(&ctx);
    /* WjCryptLib uses uint32_t for length; cap each chunk to 64 KB so
     * we don't run afoul of any internal counters even if firmware is
     * eventually >4 GB (it isn't, but cheap insurance). */
    uint32_t off = 0;
    while (off < length) {
        uint32_t n = length - off;
        if (n > 0x10000U) n = 0x10000U;
        Sha256Update(&ctx, base + off, n);
        off += n;
    }
    SHA256_HASH digest;
    Sha256Finalise(&ctx, &digest);
    memcpy(out, digest.bytes, 32);
}

/* Verify ECDSA-P256 signature over the firmware bytes of a slot.
 * Returns 1 on success, 0 on failure (or when sig_present == 0 and the
 * caller doesn't enforce signing).  Pubkey is 65 B (0x04||X||Y); uECC
 * wants 64 B (X||Y) so we skip byte 0. */
static int verify_signature(uint32_t slot_base, const ota_trailer_t *t) {
    if (t->sig_present == 0) return 1;        /* unsigned image — caller's policy */
    uint8_t hash[32];
    sha256_range((const uint8_t *)slot_base, t->image_size, hash);
    /* Self-check: trailer SHA must match what we just computed.  Catches
     * the case where a builder appends a bogus trailer SHA but a valid
     * signature over a different hash — uECC would still pass. */
    if (memcmp(hash, t->sha256, 32) != 0) return 0;
    const uint8_t *sig = (const uint8_t *)(slot_base + SIG_OFFSET);
    return uECC_verify(ota_bootloader_pubkey + 1,    /* skip 0x04 */
                       hash, 32, sig, uECC_secp256r1());
}

/* Score a slot.  Higher score wins; ties go to Slot A.
 *
 * Goal: a fresh PENDING upload of v2.0 should win over v1.0 GOOD
 * (allow upgrade), but at the SAME version GOOD wins over PENDING
 * (avoid re-trying an already-rolled-back image).  PENDING_VERIFY is
 * a diagnostic state — the slot was claimed last boot but never
 * called mark_good, so we treat it as BAD until the OTA cycle clears
 * the trailer.
 *
 *   GOOD:           version * 10 + 5
 *   PENDING:        version * 10 + 1
 *   PENDING_VERIFY: -2     (revert)
 *   BAD:            -1
 *   EMPTY/invalid:  -3
 */
static int score_slot_for(uint32_t slot_base, const ota_trailer_t *t) {
    if (t->magic != MAGIC_OTAV) return -3;
    if (t->image_size == 0 || t->image_size > (SLOT_SIZE - 128U)) return -3;
    /* Reject signed images whose signature does not verify.  Unsigned
     * images (sig_present == 0) skip this and fall through to the normal
     * trailer-state scoring — useful during development.  In production
     * builds we'd hard-fail unsigned images by checking sig_present == 0
     * → return -3 here. */
    if (!verify_signature(slot_base, t)) return -1;
    int base = (int)t->version_int * 10;
    switch (t->slot_state) {
    case STATE_GOOD:             return base + 5;
    case STATE_PENDING:          return base + 3;   /* prefer fresh PENDING */
    case STATE_PENDING_VERIFY_2: return base + 2;   /* 2 retries remain */
    case STATE_PENDING_VERIFY_1: return base + 1;   /* 1 retry remains */
    case STATE_BAD:              return -1;
    case STATE_EMPTY:            return -3;
    default:                     return base;
    }
}

static void __attribute__((noreturn)) jump_to_slot(uint32_t slot_base) {
    /* Vector Table Offset Register at 0xE000ED08 (SCB->VTOR). */
    *(volatile uint32_t *)0xE000ED08UL = slot_base;
    uint32_t new_msp = *(volatile uint32_t *)slot_base;
    uint32_t new_pc  = *(volatile uint32_t *)(slot_base + 4);
    /* Make sure IRQs are ENABLED in PRIMASK before handing control over —
     * the FSP/Cortex-M reset handler assumes the CPU's PRIMASK has its
     * post-reset default of 0 (IRQs unmasked).  We must not leave it
     * disabled, otherwise SysTick/lwIP/UART never fire and the new
     * image looks dead even though it is executing.  No interrupts can
     * actually fire here yet — the bootloader hasn't enabled any peripheral
     * IRQ sources — so the cpsie has no race with our own state. */
    __asm volatile ("cpsie i" ::: "memory");
    __asm volatile (
        "msr msp, %0   \n"
        "isb           \n"
        "bx  %1        \n"
        :: "r"(new_msp), "r"(new_pc) : "memory"
    );
    __builtin_unreachable();
    while (1) {}
}

/* Sanity check before jumping into a slot: the first word must be a
 * plausible Main Stack Pointer pointing into SRAM (0x20000000-0x20100000
 * on RA6M5; the chip has 512 KB but reserve a generous 1 MB window for
 * safety).  An erased slot (0xFFFFFFFF) and a half-written slot will
 * fail this and the bootloader will fall back to the OTHER slot. */
static int slot_image_looks_valid(uint32_t slot_base) {
    uint32_t msp = *(volatile uint32_t *)slot_base;
    if (msp < 0x20000000U || msp > 0x20100000U) return 0;
    uint32_t reset = *(volatile uint32_t *)(slot_base + 4);
    /* Reset_Handler must be inside the slot, with the Thumb bit set. */
    if ((reset & 1) == 0) return 0;
    if (reset < slot_base || reset >= slot_base + SLOT_SIZE) return 0;
    return 1;
}

/* Returns 1 if the slot is in a "PENDING-ish" state (gets retry counted
 * down on each boot).  GOOD slots and BAD/EMPTY slots don't decrement. */
static int slot_is_retry_eligible(const ota_trailer_t *t) {
    switch (t->slot_state) {
    case STATE_PENDING:
    case STATE_PENDING_VERIFY_2:
    case STATE_PENDING_VERIFY_1:
        return 1;
    default:
        return 0;
    }
}

int bootloader_main(void) {
    /* NOTE: DATA_FLASH retry-counter management deliberately disabled —
     * the bare-metal FCU sequence in `bootloader_fcu.c` triggers FCU
     * FESETERR mid-sequence which poisons the FSP flash state in the
     * running app (subsequent R_FLASH_HP_Erase fails).  Multi-stage
     * trailer-state retry (3 attempts via PENDING -> PV_2 -> PV_1 -> BAD)
     * already provides revert robustness without DATA_FLASH writes from
     * the bootloader.  When FCU sequence is fully debugged, re-enable: */
    /* df_init_if_needed(); */

    const ota_trailer_t *ta =
        (const ota_trailer_t *)(SLOT_A_BASE + TRAILER_OFFSET);
    const ota_trailer_t *tb =
        (const ota_trailer_t *)(SLOT_B_BASE + TRAILER_OFFSET);

    int sa = score_slot_for(SLOT_A_BASE, ta);
    int sb = score_slot_for(SLOT_B_BASE, tb);

    /* DATA_FLASH retry-counter override disabled — see note above. */

    /* Sanity-fail any slot whose image bytes don't even look like a
     * valid Cortex-M reset record.  Catches half-erased / never-written
     * slots whose trailer might still pass magic check (e.g. someone
     * stamped a trailer before writing the firmware). */
    if (sa >= 0 && !slot_image_looks_valid(SLOT_A_BASE)) sa = -1;
    if (sb >= 0 && !slot_image_looks_valid(SLOT_B_BASE)) sb = -1;

    /* If neither slot looks valid, try Slot A unconditionally — the
     * JLink-provisioned factory image typically lives there.  If that
     * is also blank, the CPU will HardFault and Default_Handler spins
     * (allowing JLink to attach for diagnosis). */
    if (sa < 0 && sb < 0) {
        if (slot_image_looks_valid(SLOT_A_BASE)) {
            jump_to_slot(SLOT_A_BASE);
        }
        if (slot_image_looks_valid(SLOT_B_BASE)) {
            jump_to_slot(SLOT_B_BASE);
        }
        /* Both slots blank — spin forever; user must re-provision via
         * JLink.  Phase 3c will replace this with a UART recovery REPL. */
        while (1) { __asm volatile ("wfi"); }
    }

    uint32_t winner = (sb > sa) ? SLOT_B_BASE : SLOT_A_BASE;
    const ota_trailer_t *tw = (winner == SLOT_A_BASE) ? ta : tb;

    /* DATA_FLASH counter decrement disabled — see top-of-fn note. */
    (void)tw;
    jump_to_slot(winner);
}
