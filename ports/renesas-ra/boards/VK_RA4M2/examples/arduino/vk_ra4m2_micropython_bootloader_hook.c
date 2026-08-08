/*
 * MicroPython hook for entering the VK_RA4M2 Arduino-style bootloader.
 *
 * Add this file to the VK_RA4M2 board build and wire it from mpconfigboard.h:
 *
 *   #define MICROPY_BOARD_ENTER_BOOTLOADER(nargs, args) VK_RA4M2_board_enter_bootloader()
 *   void VK_RA4M2_board_enter_bootloader(void);
 */

#include <stdint.h>

#include "bsp_api.h"

#define BSP_PRV_PRCR_KEY         (0xA500U)
#define BSP_PRV_PRCR_PRC1_UNLOCK ((BSP_PRV_PRCR_KEY) | 0x0002U)
#define BSP_PRV_PRCR_LOCK        ((BSP_PRV_PRCR_KEY) | 0x0000U)

#define VK_RA4M2_BOOT_DOUBLE_TAP_MAGIC (0x07738135U)
#define VK_RA4M2_BOOT_DOUBLE_TAP_ADDR  (*((volatile uint32_t *)&R_SYSTEM->VBTBKR[0]))

void VK_RA4M2_board_enter_bootloader(void) {
    R_SYSTEM->PRCR = (uint16_t)BSP_PRV_PRCR_PRC1_UNLOCK;
    VK_RA4M2_BOOT_DOUBLE_TAP_ADDR = VK_RA4M2_BOOT_DOUBLE_TAP_MAGIC;
    R_SYSTEM->PRCR = (uint16_t)BSP_PRV_PRCR_LOCK;

    NVIC_SystemReset();
}
