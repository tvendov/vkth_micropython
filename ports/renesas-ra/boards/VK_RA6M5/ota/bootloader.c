/*
 * VK_RA6M5 OTA bootloader — skeleton, ~200 lines.
 *
 * This is a stand-alone program that lives at 0x00000000-0x0000FFFF.
 * It is NOT part of the MicroPython firmware build — build it as its
 * own e2 studio FSP project (Bare-Metal "minimal" template), or
 * convert into a standalone GCC project with the linker placing it
 * at 0x00000000.
 *
 * Reset flow:
 *
 *   power-on
 *     ├── read STAGING_FLAG_ADDR
 *     │     ├── magic == OTA_MAGIC_REQUEST_SWAP
 *     │     │     ├── erase active slot (0x00010000..0x00110000)
 *     │     │     ├── copy STAGING -> active (1 MB)
 *     │     │     ├── write OTA_MAGIC_NORMAL_BOOT to flag
 *     │     │     └── fall through
 *     │     └── otherwise: skip swap
 *     └── jump to *(uint32_t *)(0x00010004)   // Reset_Handler of app
 *
 * What you must add for production:
 *
 *   - Image signature / hash verification BEFORE the swap (otherwise
 *     a corrupted upload bricks the board).  See MCUboot for an
 *     industrial-strength implementation.
 *
 *   - Roll-back: keep the previous image in a secondary staging slot
 *     and revert if the new image fails to mark itself "ok" within
 *     N seconds (watchdog-driven).
 *
 *   - Power-fail safety: the swap is a long erase+write sequence,
 *     interruption mid-way leaves the active slot half-written.
 *     Mitigation: copy in 32 KB chunks and update a "progress" flag,
 *     so a re-boot mid-swap resumes from where it stopped.
 */

#include <stdint.h>
#include "bsp_api.h"
#include "r_flash_hp.h"

#define APP_BASE              (0x00010000UL)
#define APP_SIZE              (0x00100000UL)   /* 1 MB */
#define STAGING_BASE          (0x00110000UL)
#define STAGING_SIZE          (0x00080000UL)   /* 512 KB */
#define STAGING_FLAG_ADDR     (STAGING_BASE)
#define OTA_MAGIC_REQUEST_SWAP  (0x5741504DU)  /* "MAPW" -> "swap please" */
#define OTA_MAGIC_NORMAL_BOOT   (0x4F4B4F4BU)  /* "OKOK" -> nothing to do */

extern flash_hp_instance_ctrl_t g_flash_hp_ctrl;
extern const flash_cfg_t        g_flash_hp_cfg;

static void boot_app(void) {
    uint32_t app_sp = *(uint32_t *)(APP_BASE);
    uint32_t app_pc = *(uint32_t *)(APP_BASE + 4);

    /* Set the vector table to the app's start so its IRQs resolve. */
    SCB->VTOR = APP_BASE;
    __set_MSP(app_sp);

    /* Hand off — never returns. */
    void (*app_reset)(void) = (void (*)(void))app_pc;
    __DSB(); __ISB();
    app_reset();
    while (1) {}                              /* unreachable */
}

static fsp_err_t do_swap(void) {
    fsp_err_t err = R_FLASH_HP_Open(&g_flash_hp_ctrl, &g_flash_hp_cfg);
    if (err != FSP_SUCCESS) return err;

    /* Erase the active slot.  RA6M5 code-flash sector size is 32 KB —
       APP_SIZE / 0x8000 = 32 sectors. */
    err = R_FLASH_HP_Erase(&g_flash_hp_ctrl, APP_BASE, APP_SIZE / 0x8000U);
    if (err != FSP_SUCCESS) goto out;

    /* Copy STAGING_BASE..STAGING_BASE+APP_SIZE to APP_BASE..APP_BASE+APP_SIZE.
       FSP requires writes in multiples of FLASH_HP_CF_WRITE_SIZE (128 B on
       RA6M5).  Copy in chunks; staging may have less than 1 MB content,
       padded with 0xFF after the actual image end. */
    const uint32_t chunk = 0x1000;            /* 4 KB chunks */
    for (uint32_t off = 0; off < APP_SIZE; off += chunk) {
        err = R_FLASH_HP_Write(&g_flash_hp_ctrl,
                               STAGING_BASE + off,
                               APP_BASE + off,
                               chunk);
        if (err != FSP_SUCCESS) goto out;
    }

    /* Clear the flag so the next boot is a normal one.  The flag word is
       in STAGING — erase the first staging sector. */
    err = R_FLASH_HP_Erase(&g_flash_hp_ctrl, STAGING_BASE, 1);
    /* Write OTA_MAGIC_NORMAL_BOOT explicitly so a half-erased flag is
       not interpreted as "swap needed" on the next boot. */
    if (err == FSP_SUCCESS) {
        uint32_t buf[32] __attribute__((aligned(4))) = { OTA_MAGIC_NORMAL_BOOT };
        for (int i = 1; i < 32; ++i) buf[i] = 0xFFFFFFFFU;
        err = R_FLASH_HP_Write(&g_flash_hp_ctrl,
                               (uint32_t)buf, STAGING_FLAG_ADDR, 128);
    }

out:
    R_FLASH_HP_Close(&g_flash_hp_ctrl);
    return err;
}

int main(void) {
    /* Bootloader runs only the bare minimum: clocks, GPIO sufficient
       to access internal flash.  No USB, no Ethernet, no peripherals
       beyond R_FLASH_HP. */

    uint32_t flag = *(volatile uint32_t *)STAGING_FLAG_ADDR;
    if (flag == OTA_MAGIC_REQUEST_SWAP) {
        /* If swap fails, fall through to boot whatever is still in the
           active slot.  Production code should track the failure and
           refuse to boot a partially-written image. */
        (void)do_swap();
    }

    boot_app();
    return 0;                                 /* unreachable */
}
