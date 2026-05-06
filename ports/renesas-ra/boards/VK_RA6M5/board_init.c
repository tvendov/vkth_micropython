/*
 * board_init.c - Инициализация на OSPI RAM
 */

#include "py/mpconfig.h"
#include "py/runtime.h"

#if MICROPY_HW_HAS_OSPI_RAM

#include "hal_data.h"
#include "r_ospi.h"
#include "r_spi_flash_api.h"

extern const spi_flash_instance_t g_ospi_ram0;

// DOPI mode read command (double data rate)
#define OSPI_DOPI_READ_CMD     0xA000

void board_init(void) {
    static bool initialized = false;
    
    if (initialized) {
        return;
    }
    
    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] board_init() called!\n");
    mp_printf(&mp_plat_print, "[BOARD] MICROPY_HW_HAS_OSPI_RAM is defined\n");
    mp_printf(&mp_plat_print, "[BOARD] Initializing OSPI RAM...\n");
    #endif
    
    // Open OSPI driver
    fsp_err_t err = g_ospi_ram0.p_api->open(g_ospi_ram0.p_ctrl, g_ospi_ram0.p_cfg);
    if (err != FSP_SUCCESS) {
        #if MICROPY_HW_BOARD_INIT_DEBUG
        mp_printf(&mp_plat_print, "[BOARD] Failed to open OSPI: %d\n", err);
        #endif
        return;
    }

    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] OSPI RAM hardware initialized successfully\n");
    mp_printf(&mp_plat_print, "[BOARD] Entering memory-mapped mode...\n");
    #endif

    // Prepare for memory-mapped mode (DOPI)
    spi_flash_direct_transfer_t direct_transfer = {0};

    direct_transfer.command        = (uint16_t)OSPI_DOPI_READ_CMD;
    direct_transfer.address_length = SPI_FLASH_ADDRESS_BYTES_4;
    direct_transfer.dummy_cycles   = 5;
    
    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] Using DOPI read command: 0x%04X\n", direct_transfer.command);
    mp_printf(&mp_plat_print, "[BOARD] Address length: %d bytes\n", 4);
    mp_printf(&mp_plat_print, "[BOARD] Dummy cycles: %d\n", direct_transfer.dummy_cycles);
    #endif
    
    #ifdef SPI_FLASH_DIRECT_TRANSFER_DIR_READ
    direct_transfer.read_mode  = SPI_FLASH_DIRECT_TRANSFER_DIR_READ;
    #else
    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] SPI_FLASH_DIRECT_TRANSFER_DIR_READ not defined, skipping directTransfer\n");
    #endif
    #endif
    
    // Enter memory-mapped mode
    err = g_ospi_ram0.p_api->directTransfer(g_ospi_ram0.p_ctrl, &direct_transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (err != FSP_SUCCESS) {
        #if MICROPY_HW_BOARD_INIT_DEBUG
        mp_printf(&mp_plat_print, "[BOARD] Failed to enter memory-mapped mode: %d\n", err);
        #endif
        return;
    }

    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] OSPI RAM ready for memory-mapped access\n");
    #endif
    
    // Quick test write/read
    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] Testing OSPI RAM access at 0x68000000...\n");
    #endif
    
    volatile uint32_t *ospi_ram = (volatile uint32_t *)0x68000000;
    
    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] Initial read: 0x%08lX\n", ospi_ram[0]);
    #endif
    
    ospi_ram[0] = 0x12345678;
    if (ospi_ram[0] == 0x12345678) {
        #if MICROPY_HW_BOARD_INIT_DEBUG
        mp_printf(&mp_plat_print, "[BOARD] OSPI RAM test PASSED! Write/Read OK\n");
        #endif
    } else {
        #if MICROPY_HW_BOARD_INIT_DEBUG
        mp_printf(&mp_plat_print, "[BOARD] OSPI RAM test FAILED! Read: 0x%08lX\n", ospi_ram[0]);
        #endif
    }
    
    ospi_ram[0] = 0x00000000;  // Clear test value
    
    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] OSPI RAM hardware ready for GC integration\n");
    mp_printf(&mp_plat_print, "[BOARD] board_init() completed\n");
    #endif
    
    initialized = true;
}

#else

// Empty implementation when OSPI is not enabled
void board_init(void) {
    #if MICROPY_HW_BOARD_INIT_DEBUG
    mp_printf(&mp_plat_print, "[BOARD] Basic board init (OSPI disabled)\n");
    #endif
}

#endif // MICROPY_HW_HAS_OSPI_RAM