/*
 * Rescue stub — single-slot OTA architecture for VK_RA6M5.
 *
 * Memory map:
 *   0x00000000  Rescue stub      32 KB   (this binary)
 *   0x00008000  Application      1.97 MB (linked at this fixed address)
 *   0x001F0000  Metadata          64 KB  (state, retry, sig, copy progress)
 *
 *   QSPI 8 MB at 0x60000000:
 *     /flash             FAT FS (~3 MB user)
 *     0x60500000  staging area  2 MB (raw — incoming `.ota` payload)
 *     0x60700000  factory backup 2 MB (raw — last-resort recovery)
 *
 * Decision flow on every reset:
 *   1. Sanity-check the active app at 0x00008000 (MSP+Reset_Handler).
 *   2. Read metadata sector at 0x001F0000.
 *   3. Branch on metadata.state:
 *        STATE_NONE / STATE_GOOD  → boot the active app
 *        STATE_PENDING            → verify SHA on QSPI staging,
 *                                   if OK → erase 0x8000+, copy QSPI → CF,
 *                                   re-verify, set PENDING_VERIFY, boot
 *        STATE_PENDING_VERIFY     → app failed mark_good last boot;
 *                                   decrement retry; if exhausted →
 *                                   restore from factory backup;
 *                                   else boot active app again
 *        STATE_BAD                → restore from factory backup, boot
 *   4. Hand off via VTOR/MSP/PC to 0x00008004.
 *
 * This file is the *skeleton* — Phase 1 (Task 17) has only the boot-
 * decision and the jump.  Phases 2-4 (Tasks 18-20) add CF write,
 * QSPI read, and the copy state machine.
 */

#include <stdint.h>
#include <string.h>

/* ---- memory map ---- */
#define APP_BASE              0x00008000UL
#define APP_SIZE              0x001E8000UL     /* 1.97 MB */
#define METADATA_BASE         0x001F0000UL
#define METADATA_SIZE         0x00010000UL     /* 64 KB last region */
#define QSPI_STAGING_BASE     0x60500000UL
#define QSPI_STAGING_SIZE     0x00200000UL     /* 2 MB */
#define QSPI_FACTORY_BASE     0x60700000UL
#define QSPI_FACTORY_SIZE     0x00200000UL     /* 2 MB */

/* ---- metadata layout in flash sector @ 0x001F0000 ---- */
#define METADATA_MAGIC        0x4F544145UL      /* 'OTAE' */

#define STATE_NONE             0xFFu
#define STATE_PENDING          0x01u
#define STATE_PENDING_VERIFY   0x02u
#define STATE_GOOD             0x03u
#define STATE_BAD              0xFEu

typedef struct __attribute__((packed)) {
    uint32_t magic;             /* 'OTAE' */
    uint8_t  state;             /* one of STATE_* */
    uint8_t  retry_count;       /* decremented per failed boot */
    uint8_t  sig_present;       /* 0 unsigned / 1 ECDSA-P256 */
    uint8_t  reserved;
    uint32_t image_size;        /* bytes in QSPI staging */
    uint32_t copy_progress;     /* bytes already copied to CF (resumable) */
    uint32_t qspi_source_addr;  /* QSPI_STAGING_BASE or QSPI_FACTORY_BASE */
    uint8_t  sha256[32];        /* expected SHA of image */
    uint8_t  sig[64];           /* ECDSA r||s if sig_present == 1 */
    uint32_t version_int;       /* informational */
    char     version_str[12];
    /* Total = 4+1+1+1+1+4+4+4+32+64+4+12 = 132 bytes; padded to 1 page (128) */
} ota_metadata_t;

#define METADATA_RETRY_INITIAL  3u

/* ---- helpers ---- */

static int slot_image_looks_valid(uint32_t base) {
    uint32_t msp   = *(volatile uint32_t *)base;
    uint32_t reset = *(volatile uint32_t *)(base + 4);
    if (msp < 0x20000000U || msp > 0x20100000U) return 0;
    if ((reset & 1) == 0) return 0;
    if (reset < base || reset >= base + APP_SIZE) return 0;
    return 1;
}

static void __attribute__((noreturn)) jump_to_app(void) {
    *(volatile uint32_t *)0xE000ED08UL = APP_BASE;       /* SCB->VTOR */
    uint32_t new_msp = *(volatile uint32_t *)APP_BASE;
    uint32_t new_pc  = *(volatile uint32_t *)(APP_BASE + 4);
    __asm volatile ("cpsie i" ::: "memory");
    __asm volatile (
        "msr msp, %0   \n"
        "isb           \n"
        "bx  %1        \n"
        :: "r"(new_msp), "r"(new_pc) : "memory"
    );
    __builtin_unreachable();
    while (1) { }
}

/* ---- entry point ---- */

int rescue_main(void) {
    /* Trampoline-only rescue stub:
     *
     *   - sanity-check the image at 0x00008000 (MSP + Reset_Handler)
     *   - jump to the app
     *
     * All flash operations (staging upload, erase, in-place rewrite)
     * happen INSIDE the running app via FSP (mod_ota.c).  The FSP path
     * is proven; bare-metal CF write from this stub still hits FLWEERR
     * (see rescue_fcu.c for the partial implementation).  Recovery
     * from a bricked app today is via JLink revert; future work moves
     * this to a self-recovery via FCU once the FLWEERR cause is found.
     */
    if (slot_image_looks_valid(APP_BASE)) {
        jump_to_app();
    }
    /* No bootable app — spin so JLink can attach. */
    while (1) { __asm volatile ("wfi"); }
}
