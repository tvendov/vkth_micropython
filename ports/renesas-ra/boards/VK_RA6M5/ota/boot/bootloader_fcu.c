/*
 * VK_RA6M5 OTA bootloader — minimal FCU register-level access.
 *
 * Provides bare-metal data-flash read/erase/program WITHOUT pulling in
 * FSP.  Sequences extracted from `lib/fsp/.../r_flash_hp/r_flash_hp.c`,
 * trimmed to what the bootloader actually needs:
 *
 *   - dataflash_read(off, dst, len)
 *   - dataflash_erase_block(addr)        // single 64-B block
 *   - dataflash_write(addr, src, len)    // multiple of 4 bytes
 *
 * Code-flash operations (for Phase 3c UART recovery) live in a separate
 * file because CF write requires the FCU code to run from RAM (the FSP
 * `PLACE_IN_RAM` trick).  Data flash is a SEPARATE erase domain on
 * RA6M5 and can be programmed while running code from CF without
 * stalling — confirmed by RA6M5 hardware manual §39.5.
 *
 * Register map (RA6M5):
 *   R_FACI_HP        @ 0x407FE000
 *     FSADDR         @ +0x30      uint32_t — destination address
 *     FENTRYR        @ +0x84      uint16_t — mode entry (key in bits 15:8)
 *     FSTATR         @ +0x80      uint32_t — status (FRDY=bit15, DBFULL=bit10)
 *   R_FACI_HP_CMD    @ 0x407E0000
 *     FACI_CMD8      @ +0x00      uint8_t  — command stream byte
 *     FACI_CMD16     @ +0x00      uint16_t — command stream halfword
 *
 * Mode-entry KEY+value (write to FENTRYR, 16-bit):
 *   0xAA80   data-flash P/E mode
 *   0xAA01   code-flash P/E mode  (Phase 3c)
 *   0xAA00   read mode
 *
 * After writing FENTRYR to enter a mode, poll FENTRYR readback for the
 * low 8 bits to settle (e.g. 0x80 for DF mode, 0x00 for read mode).
 *
 * Command sequences:
 *   block-erase:   FACI_CMD8 = 0x20; FACI_CMD8 = 0xD0; wait FRDY
 *   program:       FACI_CMD8 = 0xE8; FACI_CMD8 = N_halfwords;
 *                  FACI_CMD16 = data[0..N-1] (wait DBFULL=0 between writes);
 *                  FACI_CMD8 = 0xD0; wait FRDY
 */

#include <stdint.h>
#include <string.h>

#define FACI_HP_BASE        0x407FE000UL
#define FACI_HP_CMD_BASE    0x407E0000UL

#define FACI_HP_FSADDR      (*(volatile uint32_t *)(FACI_HP_BASE + 0x30))
#define FACI_HP_FSTATR      (*(volatile uint32_t *)(FACI_HP_BASE + 0x80))
#define FACI_HP_FENTRYR     (*(volatile uint16_t *)(FACI_HP_BASE + 0x84))
#define FACI_HP_FPCKAR      (*(volatile uint16_t *)(FACI_HP_BASE + 0xE4))
#define FACI_HP_CMD8        (*(volatile uint8_t  *)(FACI_HP_CMD_BASE + 0x00))
#define FACI_HP_CMD16       (*(volatile uint16_t *)(FACI_HP_CMD_BASE + 0x00))

/* Scratch SRAM near the very top of the chip's 512 KB SRAM, used for
 * status reporting to the app.  The bootloader's BOOT_RAM is only the
 * first 32 KB; the app uses 512 KB.  We pick an address ABOVE the
 * bootloader's BOOT_RAM and at the very top of the app's RAM so that
 * neither the bootloader's stack/.bss nor the app's startup code
 * (which zeros .bss in the lower half of RAM) clobbers it.
 * Layout @ 0x2007FFF0:
 *   +0  uint8_t  bl_fcu_status     last FCU op error (0=ok, -1 timeout, -2 err, -3 args)
 *   +1  uint8_t  bl_fcu_phase      where in the sequence we are
 *   +4  uint32_t bl_fcu_fstatr     last FSTATR snapshot (debug) */
#define BL_STATUS_ADDR      0x2007FFF0UL
#define BL_STATUS_BYTE(off) (*(volatile uint8_t  *)(BL_STATUS_ADDR + (off)))
#define BL_STATUS_W32(off)  (*(volatile uint32_t *)(BL_STATUS_ADDR + (off)))

#define FSTATR_FRDY         (1U << 15)
#define FSTATR_DBFULL       (1U << 10)

#define FACI_CMD_BLOCK_ERASE  0x20
#define FACI_CMD_PROGRAM      0xE8
#define FACI_CMD_FINAL        0xD0
#define FACI_CMD_FORCED_STOP  0xB3
#define FACI_CMD_STATUS_CLEAR 0x50

#define DF_PE_MODE          0xAA80
#define READ_MODE           0xAA00

#define DF_BLOCK_SIZE       64U
#define DF_PROGRAM_UNIT      4U      /* 2 halfwords = 4 bytes */

/* Conservative timeout — RA6M5 manual: DF erase 18 ms typical, 320 ms max.
 * At ~200 MHz CPU we get ~30 cycles per loop iteration → 6.4M iterations
 * per second → 320 ms = ~2M iterations. Use 8M as safety margin. */
#define FCU_TIMEOUT_LOOPS   8000000UL

static int wait_frdy(void) {
    uint32_t loops = FCU_TIMEOUT_LOOPS;
    while ((FACI_HP_FSTATR & FSTATR_FRDY) == 0) {
        if (--loops == 0) return -1;
    }
    return 0;
}

static int wait_dbfull_clear(void) {
    uint32_t loops = FCU_TIMEOUT_LOOPS;
    while ((FACI_HP_FSTATR & FSTATR_DBFULL) != 0) {
        if (--loops == 0) return -1;
    }
    return 0;
}

/* Set FPCKAR to inform the FCU of the current FCLK frequency.  We
 * conservatively claim 4 MHz — slowest legal value, always safe.  At
 * higher actual FCLK the FCU just runs slower than optimal but works.
 * This MUST be called before any P/E mode entry on a fresh boot. */
static void fcu_set_clock(void) {
    /* KEY 0x1E in upper byte, freq in MHz in lower byte. */
    FACI_HP_FPCKAR = (uint16_t)(0x1E00U | 4U);
}

/* Clear any sticky FCU error flags via the FORCED_STOP + STATUS_CLEAR
 * command sequence.  Must be in P/E mode for these commands. */
static void fcu_clear_errors(void) {
    /* FORCED_STOP (0xB3) is a single-byte command (no FINAL). */
    FACI_HP_CMD8 = FACI_CMD_FORCED_STOP;
    /* Wait FRDY — should be quick. */
    uint32_t loops = 100000;
    while ((FACI_HP_FSTATR & FSTATR_FRDY) == 0) {
        if (--loops == 0) break;
    }
    /* STATUS_CLEAR (0x50) clears the error bits. */
    FACI_HP_CMD8 = FACI_CMD_STATUS_CLEAR;
}

static int enter_df_pe_mode(void) {
    fcu_set_clock();
    BL_STATUS_BYTE(1) = 0x10;   /* phase: enter_df, before wait */
    BL_STATUS_W32(4)  = FACI_HP_FSTATR;
    /* Wait for FRDY before changing mode.  After a cold boot, FRDY=1. */
    if (wait_frdy() != 0) { BL_STATUS_BYTE(0) = 0xE1; return -1; }
    BL_STATUS_BYTE(1) = 0x11;   /* phase: writing FENTRYR */
    FACI_HP_FENTRYR = DF_PE_MODE;
    BL_STATUS_BYTE(1) = 0x12;   /* phase: poll readback */
    /* Readback should reflect the mode bits without the KEY. */
    uint32_t loops = 100000;
    while ((FACI_HP_FENTRYR & 0xFF) != 0x80) {
        if (--loops == 0) { BL_STATUS_BYTE(0) = 0xE2; return -1; }
    }
    /* Clear any leftover error flags from previous (failed) operations. */
    if (FACI_HP_FSTATR & 0x00F07040UL) {
        BL_STATUS_BYTE(1) = 0x14;     /* phase: clearing errors */
        BL_STATUS_W32(4)  = FACI_HP_FSTATR;
        fcu_clear_errors();
        BL_STATUS_W32(4)  = FACI_HP_FSTATR;    /* post-clear snapshot */
    }
    BL_STATUS_BYTE(1) = 0x13;   /* phase: in DF mode, errors clear */
    return 0;
}

static int exit_pe_mode(void) {
    if (wait_frdy() != 0) return -1;
    FACI_HP_FENTRYR = READ_MODE;
    uint32_t loops = 1000;
    while ((FACI_HP_FENTRYR & 0xFF) != 0x00) {
        if (--loops == 0) return -1;
    }
    return 0;
}

/* Read DATA_FLASH — memory-mapped, no FCU sequence needed. */
void bootloader_df_read(uint32_t offset, void *dst, uint32_t len) {
    /* DATA_FLASH lives at 0x08000000-0x08001FFF on RA6M5. */
    const uint8_t *src = (const uint8_t *)(0x08000000UL + offset);
    memcpy(dst, src, len);
}

/* Erase one 64-byte block at the current `address`, assuming DF P/E mode
 * has already been entered.  Internal helper. */
static int erase_block_no_mode_change(uint32_t address) {
    FACI_HP_FSADDR = address;
    FACI_HP_CMD8   = FACI_CMD_BLOCK_ERASE;
    FACI_HP_CMD8   = FACI_CMD_FINAL;
    if (wait_frdy() != 0) return -1;
    uint32_t st = FACI_HP_FSTATR;
    BL_STATUS_W32(4) = st;
    if (st & ((1U << 13) | (1U << 14))) return -2;
    return 0;
}

/* Program 4 bytes at `address`, assuming DF P/E mode is entered. */
static int program_4_no_mode_change(uint32_t address, const uint8_t *p) {
    FACI_HP_FSADDR = address;
    FACI_HP_CMD8   = FACI_CMD_PROGRAM;
    FACI_HP_CMD8   = (uint8_t)(DF_PROGRAM_UNIT / 2U);
    for (uint32_t i = 0; i < DF_PROGRAM_UNIT / 2U; i++) {
        uint16_t hw = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        FACI_HP_CMD16 = hw;
        if (wait_dbfull_clear() != 0) return -1;
        p += 2;
    }
    FACI_HP_CMD8 = FACI_CMD_FINAL;
    if (wait_frdy() != 0) return -1;
    uint32_t st = FACI_HP_FSTATR;
    BL_STATUS_W32(4) = st;
    if (st & ((1U << 12) | (1U << 14))) return -2;
    return 0;
}

/* Erase one 64-byte block of DATA_FLASH at absolute address.
 * Returns 0 on success, -1 on FCU timeout, -2 on error flags. */
int bootloader_df_erase_block(uint32_t address) {
    BL_STATUS_BYTE(0) = 0;
    BL_STATUS_BYTE(1) = 0x01;
    if (address < 0x08000000UL || address >= 0x08002000UL) { BL_STATUS_BYTE(0) = 0xE3; return -3; }
    if ((address & (DF_BLOCK_SIZE - 1)) != 0) { BL_STATUS_BYTE(0) = 0xE4; return -3; }
    if (enter_df_pe_mode() != 0) return -1;
    BL_STATUS_BYTE(1) = 0x21;
    int r = erase_block_no_mode_change(address);
    BL_STATUS_BYTE(1) = 0x24;
    if (exit_pe_mode() != 0) { BL_STATUS_BYTE(0) = 0xE6; return -1; }
    BL_STATUS_BYTE(1) = 0x25;
    if (r != 0) { BL_STATUS_BYTE(0) = (r == -2) ? 0xE7 : 0xE5; return r; }
    return 0;
}

/* Combined erase + write (single PE-mode session) — used by retry-counter
 * update.  Avoids re-entering DF mode mid-sequence which the FCU rejects
 * with FESETERR. */
int bootloader_df_erase_and_write(uint32_t address, const void *src, uint32_t len) {
    if (address < 0x08000000UL || address + len > 0x08002000UL) return -3;
    if ((address & (DF_BLOCK_SIZE - 1)) != 0) return -3;
    if ((len & 3) != 0 || len > DF_BLOCK_SIZE) return -3;
    BL_STATUS_BYTE(0) = 0;
    BL_STATUS_BYTE(1) = 0x40;
    if (enter_df_pe_mode() != 0) return -1;
    BL_STATUS_BYTE(1) = 0x41;
    int r = erase_block_no_mode_change(address);
    if (r != 0) {
        BL_STATUS_BYTE(0) = 0xEA;
        exit_pe_mode();
        return r;
    }
    BL_STATUS_BYTE(1) = 0x42;
    const uint8_t *p = (const uint8_t *)src;
    while (len > 0) {
        r = program_4_no_mode_change(address, p);
        if (r != 0) {
            BL_STATUS_BYTE(0) = 0xEB;
            exit_pe_mode();
            return r;
        }
        address += DF_PROGRAM_UNIT;
        p       += DF_PROGRAM_UNIT;
        len     -= DF_PROGRAM_UNIT;
    }
    BL_STATUS_BYTE(1) = 0x43;
    if (exit_pe_mode() != 0) { BL_STATUS_BYTE(0) = 0xEC; return -1; }
    BL_STATUS_BYTE(1) = 0x44;
    return 0;
}

/* Program `len` bytes (must be multiple of 4) at `address`.
 * The destination cells must already be erased (0xFFFFFFFF).
 * Returns 0 on success, -1 on timeout, -2 on error flags. */
int bootloader_df_write(uint32_t address, const void *src, uint32_t len) {
    if (address < 0x08000000UL || address + len > 0x08002000UL) return -3;
    if ((address & 3) != 0) return -3;
    if ((len & 3) != 0) return -3;

    const uint8_t *p = (const uint8_t *)src;

    if (enter_df_pe_mode() != 0) return -1;
    BL_STATUS_BYTE(1) = 0x31;     /* PE mode for write */

    /* Program 4 bytes (2 halfwords) at a time. */
    while (len > 0) {
        FACI_HP_FSADDR = address;
        FACI_HP_CMD8   = FACI_CMD_PROGRAM;
        FACI_HP_CMD8   = (uint8_t)(DF_PROGRAM_UNIT / 2U);   /* 2 halfwords */

        for (uint32_t i = 0; i < DF_PROGRAM_UNIT / 2U; i++) {
            uint16_t hw = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
            FACI_HP_CMD16 = hw;
            if (wait_dbfull_clear() != 0) {
                exit_pe_mode();
                return -1;
            }
            p += 2;
        }
        FACI_HP_CMD8 = FACI_CMD_FINAL;
        if (wait_frdy() != 0) {
            exit_pe_mode();
            return -1;
        }
        address += DF_PROGRAM_UNIT;
        len     -= DF_PROGRAM_UNIT;
    }

    BL_STATUS_BYTE(1) = 0x32;     /* write loop done */
    uint32_t st = FACI_HP_FSTATR;
    BL_STATUS_W32(4) = st;
    if (exit_pe_mode() != 0) { BL_STATUS_BYTE(0) = 0xE8; return -1; }
    BL_STATUS_BYTE(1) = 0x33;     /* exit ok */
    if (st & ((1U << 12) | (1U << 14))) { BL_STATUS_BYTE(0) = 0xE9; return -2; }
    return 0;
}
