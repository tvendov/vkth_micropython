/*
 * Rescue-stub FCU code-flash erase/write.
 *
 * Functions in this file MUST execute from SRAM, not from code flash.
 * The FCU stalls CF reads during P/E operations; if we tried to fetch
 * instructions from CF while the FCU is busy programming CF we'd
 * deadlock and HardFault.  We mark each function with the .ramcode
 * section attribute, and rescue_startup.c copies that section from
 * flash LMA → RAM VMA before calling main().
 *
 * RA6M5 specifics:
 *   - FENTRYR for CF P/E mode: 0xAA01 (KEY 0xAA + FENTRYC=1).
 *   - CF programming unit: 128 bytes (64 halfwords).
 *   - CF erase blocks:
 *       * 0x00000000 .. 0x0001FFFF — eight 8 KB blocks  (small)
 *       * 0x00020000 .. 0x001FFFFF — sixty 32 KB blocks (large)
 *   - In dual-bank mode (default on this board), writing in Bank 0
 *     (0x00000000-0x000FFFFF) only stalls Bank 0 reads, not Bank 1
 *     (0x00100000-0x001FFFFF).  But our rescue stub lives in Bank 0,
 *     so .ramcode is mandatory regardless of bank.
 */

#include <stdint.h>
#include "rescue_fcu.h"

/* ---- registers (same as DF) ---- */
#define FACI_HP_BASE        0x407FE000UL
#define FACI_HP_CMD_BASE    0x407E0000UL

#define FACI_HP_FSADDR      (*(volatile uint32_t *)(FACI_HP_BASE + 0x30))
#define FACI_HP_FSTATR      (*(volatile uint32_t *)(FACI_HP_BASE + 0x80))
#define FACI_HP_FENTRYR     (*(volatile uint16_t *)(FACI_HP_BASE + 0x84))
#define FACI_HP_FMEPROT     (*(volatile uint16_t *)(FACI_HP_BASE + 0x44))
#define FACI_HP_FBPROT0     (*(volatile uint16_t *)(FACI_HP_BASE + 0x78))
#define FACI_HP_FBPROT1     (*(volatile uint16_t *)(FACI_HP_BASE + 0x7C))
#define FACI_HP_FPCKAR      (*(volatile uint16_t *)(FACI_HP_BASE + 0xE4))
#define FACI_HP_CMD8        (*(volatile uint8_t  *)(FACI_HP_CMD_BASE + 0x00))
#define FACI_HP_CMD16       (*(volatile uint16_t *)(FACI_HP_CMD_BASE + 0x00))

/* Flash cache (must be disabled during CF P/E).
 * R_FCACHE_BASE = 0x4001C000.  FCACHEE @ 0x100 is enable bit.
 * R_CACHE_BASE  = 0x40007000.  CCACTL  @ 0x000 is C-cache control. */
#define FCACHE_FCACHEE      (*(volatile uint16_t *)0x4001C100UL)
#define FCACHE_FCACHEIV     (*(volatile uint16_t *)0x4001C104UL)
#define CACHE_CCACTL        (*(volatile uint32_t *)0x40007000UL)

#define FSTATR_FRDY         (1U << 15)
#define FSTATR_DBFULL       (1U << 10)
#define FSTATR_FLWEERR      (1U << 6)
#define FSTATR_ERROR_MASK   ((1U << 12) | (1U << 13) | (1U << 14) | FSTATR_FLWEERR)
                            /* PRGERR | ERSERR | ILGLERR | FLWEERR (protect) */
#define FACI_HP_FAWMON      (*(volatile uint32_t *)(FACI_HP_BASE + 0xDC))

#define FACI_CMD_BLOCK_ERASE  0x20
#define FACI_CMD_PROGRAM      0xE8
#define FACI_CMD_FINAL        0xD0
#define FACI_CMD_FORCED_STOP  0xB3
#define FACI_CMD_STATUS_CLEAR 0x50

#define CF_PE_MODE          0xAA01u
#define READ_MODE           0xAA00u

#define CF_PROGRAM_UNIT     128U      /* 64 halfwords */

#define BL_STATUS_ADDR      0x2007FFF0UL
#define BL_STATUS_BYTE(off) (*(volatile uint8_t  *)(BL_STATUS_ADDR + (off)))
#define BL_STATUS_W32(off)  (*(volatile uint32_t *)(BL_STATUS_ADDR + (off)))

/* Ramcode attribute — these functions live in SRAM, copied at startup. */
#define RAMCODE __attribute__((section(".ramcode"), noinline))

#define FCU_TIMEOUT_LOOPS   8000000UL

/* ---- low-level helpers (all RAMCODE) ---- */

static RAMCODE int wait_frdy(void) {
    uint32_t loops = FCU_TIMEOUT_LOOPS;
    while ((FACI_HP_FSTATR & FSTATR_FRDY) == 0) {
        if (--loops == 0) return -1;
    }
    return 0;
}

static RAMCODE int wait_dbfull(void) {
    uint32_t loops = FCU_TIMEOUT_LOOPS;
    while ((FACI_HP_FSTATR & FSTATR_DBFULL) != 0) {
        if (--loops == 0) return -1;
    }
    return 0;
}

static RAMCODE void fcu_clear_errors(void) {
    FACI_HP_CMD8 = FACI_CMD_FORCED_STOP;
    uint32_t loops = 100000;
    while ((FACI_HP_FSTATR & FSTATR_FRDY) == 0) {
        if (--loops == 0) break;
    }
    FACI_HP_CMD8 = FACI_CMD_STATUS_CLEAR;
}

static RAMCODE int enter_cf_pe_mode(void) {
    BL_STATUS_BYTE(1) = 0x70;
    /* FPCKAR — claim 4 MHz to be safe regardless of actual FCLK. */
    FACI_HP_FPCKAR = (uint16_t)(0x1E00U | 4U);
    BL_STATUS_BYTE(1) = 0x71;
    BL_STATUS_W32(4)  = FACI_HP_FSTATR;          /* FSTATR before */

    /* Disable C-cache and flash cache.  CF P/E modifies code flash; if
     * cache holds stale lines from the soon-to-be-erased area we'd see
     * inconsistent reads after the operation, and the FCU itself may
     * refuse to enter PE mode while caches are active. */
    CACHE_CCACTL    = 0;                /* Cortex-M33 I-cache off */
    FCACHE_FCACHEE  = 0;                /* Flash cache disable */

    if (wait_frdy() != 0) { BL_STATUS_BYTE(1) = 0x7F; return -1; }
    BL_STATUS_BYTE(1) = 0x72;

    /* Make sure we are in READ mode first, so the transition to CF P/E
     * doesn't trip FESETERR if FENTRYD was left set by a previous run. */
    FACI_HP_FENTRYR = READ_MODE;
    {
        uint32_t loops = 100000;
        while ((FACI_HP_FENTRYR & 0xFF) != 0x00) {
            if (--loops == 0) break;
        }
    }
    BL_STATUS_BYTE(1) = 0x73;

    /* Unlock CF P/E entry protection (FMEPROT.CEPROT=0, KEY 0xD9). */
    FACI_HP_FMEPROT = 0xD900;
    /* Cancel block protection: write KEY=0x78 + BPCN0=1 to TRIGGER
     * cancellation (the bit is "always read 0" — it's an action register,
     * writing 1 fires the cancel pulse). */
    FACI_HP_FBPROT0 = 0x7801;
    FACI_HP_FBPROT1 = 0xB101;     /* same for secure region */

    FACI_HP_FENTRYR = CF_PE_MODE;
    BL_STATUS_BYTE(1) = 0x74;

    uint32_t loops = 100000;
    while ((FACI_HP_FENTRYR & 0xFF) != 0x01) {
        if (--loops == 0) {
            BL_STATUS_W32(12) = FACI_HP_FENTRYR;     /* readback that didn't transition */
            BL_STATUS_BYTE(1) = 0x7E;
            return -1;
        }
    }
    BL_STATUS_BYTE(1) = 0x75;
    BL_STATUS_W32(4)  = FACI_HP_FSTATR;              /* FSTATR after entry */
    BL_STATUS_W32(12) = FACI_HP_FAWMON;              /* Flash Access Window monitor */

    /* Clear sticky error flags. */
    if (FACI_HP_FSTATR & 0x00F07040UL) {
        fcu_clear_errors();
    }
    BL_STATUS_BYTE(1) = 0x76;
    return 0;
}

static RAMCODE int exit_pe_mode(void) {
    if (wait_frdy() != 0) return -1;
    FACI_HP_FENTRYR = READ_MODE;
    uint32_t loops = 100000;
    while ((FACI_HP_FENTRYR & 0xFF) != 0x00) {
        if (--loops == 0) return -1;
    }
    return 0;
}

/* Erase one sector containing `addr`.  Caller must already be in CF P/E
 * mode.  Sector size handling is done by the FCU itself — we just give
 * it the start address.  In practice we pass an address aligned to the
 * relevant boundary (8 KB below 0x20000, 32 KB above). */
static RAMCODE int erase_one_block(uint32_t addr) {
    FACI_HP_FSADDR = addr;
    FACI_HP_CMD8   = FACI_CMD_BLOCK_ERASE;
    FACI_HP_CMD8   = FACI_CMD_FINAL;
    if (wait_frdy() != 0) return -1;
    if (FACI_HP_FSTATR & FSTATR_ERROR_MASK) return -2;
    return 0;
}

/* Program one 128-byte page at `addr` from `src`.  Caller must be in
 * CF P/E mode.  `addr` and the page boundary aligned. */
static RAMCODE int program_one_page(uint32_t addr, const uint8_t *src) {
    FACI_HP_FSADDR = addr;
    FACI_HP_CMD8   = FACI_CMD_PROGRAM;
    FACI_HP_CMD8   = (uint8_t)(CF_PROGRAM_UNIT / 2U);   /* 64 halfwords */
    for (uint32_t i = 0; i < CF_PROGRAM_UNIT / 2U; i++) {
        uint16_t hw = (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
        FACI_HP_CMD16 = hw;
        if (wait_dbfull() != 0) return -1;
        src += 2;
    }
    FACI_HP_CMD8 = FACI_CMD_FINAL;
    if (wait_frdy() != 0) return -1;
    if (FACI_HP_FSTATR & FSTATR_ERROR_MASK) return -2;
    return 0;
}

/* Compute the start of the erase block containing `addr` and the size
 * of that block, per RA6M5's mixed-sector layout. */
static RAMCODE void block_geometry(uint32_t addr,
                                   uint32_t *block_start, uint32_t *block_size) {
    if (addr < 0x00020000U) {
        /* 8 KB blocks 0..7 */
        *block_size  = 0x2000U;
        *block_start = addr & ~(0x2000U - 1U);
    } else {
        /* 32 KB blocks */
        *block_size  = 0x8000U;
        *block_start = addr & ~(0x8000U - 1U);
    }
}

/* ---- public API (also RAMCODE) ---- */

int RAMCODE rescue_cf_erase_range(uint32_t start, uint32_t length) {
    if (start < 0x00008000U) return -3;        /* refuse to touch the rescue stub */
    if (start + length > 0x00200000U) return -3;
    if (length == 0) return 0;

    BL_STATUS_BYTE(1) = 0x50;
    if (enter_cf_pe_mode() != 0) {
        BL_STATUS_BYTE(0) = 0xF1;
        return -1;
    }
    BL_STATUS_BYTE(1) = 0x51;

    uint32_t addr = start;
    uint32_t end  = start + length;
    while (addr < end) {
        uint32_t bs, bsz;
        block_geometry(addr, &bs, &bsz);
        int r = erase_one_block(bs);
        if (r != 0) {
            BL_STATUS_BYTE(0) = (r == -2) ? 0xF2 : 0xF3;
            BL_STATUS_W32(4)  = FACI_HP_FSTATR;
            exit_pe_mode();
            return r;
        }
        addr = bs + bsz;
    }

    BL_STATUS_BYTE(1) = 0x52;
    if (exit_pe_mode() != 0) {
        BL_STATUS_BYTE(0) = 0xF4;
        return -1;
    }
    BL_STATUS_BYTE(1) = 0x53;
    return 0;
}

int RAMCODE rescue_cf_write(uint32_t addr, const void *src, uint32_t len) {
    if (addr < 0x00008000U) return -3;
    if (addr + len > 0x00200000U) return -3;
    if ((addr & (CF_PROGRAM_UNIT - 1U)) != 0) return -3;
    if ((len  & (CF_PROGRAM_UNIT - 1U)) != 0) return -3;
    if (len == 0) return 0;

    BL_STATUS_BYTE(1) = 0x60;
    if (enter_cf_pe_mode() != 0) {
        BL_STATUS_BYTE(0) = 0xF5;
        return -1;
    }
    BL_STATUS_BYTE(1) = 0x61;

    const uint8_t *p = (const uint8_t *)src;
    uint32_t a = addr;
    uint32_t remaining = len;
    while (remaining > 0) {
        int r = program_one_page(a, p);
        if (r != 0) {
            BL_STATUS_BYTE(0) = (r == -2) ? 0xF6 : 0xF7;
            BL_STATUS_W32(4)  = FACI_HP_FSTATR;
            exit_pe_mode();
            return r;
        }
        a += CF_PROGRAM_UNIT;
        p += CF_PROGRAM_UNIT;
        remaining -= CF_PROGRAM_UNIT;
    }

    BL_STATUS_BYTE(1) = 0x62;
    if (exit_pe_mode() != 0) {
        BL_STATUS_BYTE(0) = 0xF8;
        return -1;
    }
    BL_STATUS_BYTE(1) = 0x63;
    return 0;
}
