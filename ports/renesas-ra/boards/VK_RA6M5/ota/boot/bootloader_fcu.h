/* Bootloader FCU API — bare-metal data-flash access. */
#ifndef VK_RA6M5_OTA_BOOTLOADER_FCU_H
#define VK_RA6M5_OTA_BOOTLOADER_FCU_H

#include <stdint.h>

/* DATA_FLASH layout */
#define DATA_FLASH_BASE    0x08000000UL
#define DATA_FLASH_SIZE    0x00002000UL    /* 8 KB */
#define DATA_FLASH_BLOCK   64U              /* erase granularity */

/* Memory-mapped read; fine without FCU. */
void bootloader_df_read(uint32_t offset, void *dst, uint32_t len);

/* Erase one 64-byte block at absolute DATA_FLASH address.
 * Returns 0 on success, negative on error. */
int  bootloader_df_erase_block(uint32_t address);

/* Program 4-byte aligned region; cells must be erased first.
 * `address` and `len` must be multiples of 4. */
int  bootloader_df_write(uint32_t address, const void *src, uint32_t len);

/* Combined erase + write within ONE PE-mode session.  Required for
 * the retry-counter update because re-entering DF mode mid-sequence
 * triggers FCU FESETERR.  `len` <= 64 (one block). */
int  bootloader_df_erase_and_write(uint32_t address, const void *src, uint32_t len);

/* OTA retry-counter layout in DATA_FLASH (Phase 8 / Task 14):
 *   0x08000000   4 B   magic = 0xC0FFEE01
 *   0x08000004   4 B   slot_a_retry  (0xFFFFFFFF = uninit, otherwise 0..3)
 *   0x08000008   4 B   slot_b_retry
 *   0x0800000C   4 B   reserved
 *   ... rest of the 64-byte first block: 0xFF
 *
 * The first 64 B block is owned by OTA; the remaining 8 KB - 64 B is
 * available for user code if it stays out of this offset window. */
#define OTA_DF_MAGIC_VALUE  0xC0FFEE01UL
#define OTA_DF_OFF_MAGIC    0x00
#define OTA_DF_OFF_RETRY_A  0x04
#define OTA_DF_OFF_RETRY_B  0x08
#define OTA_DF_INITIAL_RETRY  3

#endif
