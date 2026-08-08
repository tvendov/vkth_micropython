/*
 * lorawan/system/flash/dataflash_partition.h
 *
 * Single source of truth for the VK_RA4M2 (R7FA4M2AD) data-flash partition
 * map. Mirrored on the Python side; any change here must be reflected there.
 *
 * Geometry (RA4M2 HW User's Manual r01uh0892ej0140 §44; data flash at
 * 0x0800_0000-0x0800_1FFF per datasheet §4965 + BSP bsp_feature.h + board
 * linker DATA_FLASH region):
 *   base        0x08000000
 *   total       8192 B
 *   erase block 64 B
 *   write unit  4 B
 *
 * Region layout (block units of 64 B):
 *   CRED    block   0        0x08000000-0x0800003F     64 B
 *   NVM_A   blocks  1-32     0x08000040-0x0800083F   2048 B
 *   NVM_B   blocks 33-64     0x08000840-0x0800103F   2048 B
 *   NONCE   blocks 65-66     0x08001040-0x080010BF    128 B
 *   CONFIG  block  67        0x080010C0-0x080010FF     64 B
 *   APP     blocks 68-127    0x08001100-0x08001FFF   3840 B
 *
 * The default (un-scoped) `dataflash` object addresses ONLY the APP region.
 * Access to CRED / NVM_A / NVM_B / NONCE / CONFIG is exclusively through the
 * region-scoped view (dataflash.region("...")), which bounds-checks every
 * offset against the region SIZE.
 */

#ifndef LORAWAN_SYSTEM_FLASH_DATAFLASH_PARTITION_H
#define LORAWAN_SYSTEM_FLASH_DATAFLASH_PARTITION_H

#include <stdint.h>
#include <stddef.h>

/* Runtime AHB address of data flash on this board/FSP config. The RA4M2 HW
   manual lists 0x40100000, but this board's FSP `g_flash0` infoGet (and the
   linker DATA_FLASH region) place data flash at 0x08000000 — verified by
   reading the live LWCR record there. Region addresses below MUST match the
   FSP-resolved base or read/write hit dead address space (returns 0x00). */
#define DF_BASE              (0x08000000u)
#define DF_TOTAL_SIZE        (8192u)
#define DF_ERASE_BLOCK       (64u)
#define DF_WRITE_UNIT        (4u)

#define DF_CRED_START        (DF_BASE + 0x0000u)   /* block 0 */
#define DF_CRED_SIZE         (64u)

#define DF_NVM_A_START       (DF_BASE + 0x0040u)   /* blocks 1-32 */
#define DF_NVM_A_SIZE        (2048u)

#define DF_NVM_B_START       (DF_BASE + 0x0840u)   /* blocks 33-64 */
#define DF_NVM_B_SIZE        (2048u)

#define DF_NONCE_START       (DF_BASE + 0x1040u)   /* blocks 65-66 */
#define DF_NONCE_SIZE        (128u)

#define DF_CONFIG_START      (DF_BASE + 0x10C0u)   /* block 67 */
#define DF_CONFIG_SIZE       (64u)

#define DF_APP_START         (DF_BASE + 0x1100u)   /* blocks 68-127 */
#define DF_APP_SIZE          (3840u)

typedef enum {
    DF_REGION_CRED = 0,
    DF_REGION_NVM_A,
    DF_REGION_NVM_B,
    DF_REGION_NONCE,
    DF_REGION_CONFIG,
    DF_REGION_APP,
    DF_REGION_COUNT
} df_region_id_t;

typedef struct {
    const char *name;
    uint32_t    start;   /* absolute data-flash address */
    uint32_t    size;    /* bytes */
} df_region_t;

/* Indexed by df_region_id_t. Defined here as a static const so each
   translation unit that needs the map gets its own copy in flash; the
   table is tiny (6 entries) and avoids an extra .c file in the build. */
static const df_region_t df_partition_map[DF_REGION_COUNT] = {
    { "CRED",   DF_CRED_START,   DF_CRED_SIZE   },
    { "NVM_A",  DF_NVM_A_START,  DF_NVM_A_SIZE  },
    { "NVM_B",  DF_NVM_B_START,  DF_NVM_B_SIZE  },
    { "NONCE",  DF_NONCE_START,  DF_NONCE_SIZE  },
    { "CONFIG", DF_CONFIG_START, DF_CONFIG_SIZE },
    { "APP",    DF_APP_START,    DF_APP_SIZE    },
};

#endif /* LORAWAN_SYSTEM_FLASH_DATAFLASH_PARTITION_H */
