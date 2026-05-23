/*
 * lorawan/system/flash/dflash_lwnvm.h
 *
 * Power-loss-safe NVM blob storage on RA4M2 data flash via FSP
 * R_FLASH_HP. Uses the same `g_flash0` instance already opened by the
 * port's `moddataflash.c`, so no separate flash driver init is needed.
 *
 * Atomic ping-pong: two banks at adjacent offsets in data flash.
 * Each bank holds a 32-byte header (sequence#, CRC32, per-module
 * sizes) followed by the serialized blob. On boot, the bank with the
 * higher sequence number AND a valid CRC is chosen. On write, the
 * other bank is erased + written; the previous bank stays valid until
 * the new bank's CRC verifies.
 *
 * RA4M2 data flash facts (per ?44.2 of the user manual):
 *   * 8 KB total at 0x40100000 (some SKUs 4 KB; sized via FSP infoGet)
 *   * 64-byte erase block, 4-byte write granularity
 *   * AHB reads valid only after FCACHE invalidation
 *   * BGO mode supported but we use blocking writes for simplicity
 *     (Phase 6a.2 v1) ? typical store completes in 50-150 ms which
 *     LoRaMac can absorb between TX_DONE and the RX1 window.
 */

#ifndef LORAWAN_SYSTEM_FLASH_DFLASH_LWNVM_H
#define LORAWAN_SYSTEM_FLASH_DFLASH_LWNVM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On-flash header preceding each bank's serialized blob. Fixed 32 B
   so the data offset is block-aligned (RA4M2 erase block = 64 B). */
typedef struct {
    uint32_t sequence;       /* monotonic; 0xFFFFFFFF == invalid bank */
    uint32_t valid_magic;    /* 0x4E564D31 == "NVM1" when written     */
    uint32_t crc32;          /* over (mac_size..reserved2 + payload)   */
    uint32_t reserved;
    uint16_t mac_size;
    uint16_t region_size;
    uint16_t crypto_size;
    uint16_t se_size;
    uint16_t cmds_size;
    uint16_t classb_size;
    uint16_t cq_size;
    uint16_t reserved2;
} dflash_header_t;

#define DFLASH_VALID_MAGIC   (0x4E564D31u)   /* "NVM1" */
#define DFLASH_HEADER_SIZE   (32u)

/* Lifecycle. Idempotent ? safe to call multiple times. Returns true
   if the data flash is sized correctly (? 2 KB) for ping-pong. */
bool dflash_init(void);

/* Read the latest valid blob from flash. On success, copies up to
   `dst_max` bytes into `dst`, fills `*hdr` with the validated header,
   and returns true. On no valid bank, returns false (and *hdr_out is
   left zero-initialized). */
bool dflash_load_blob(uint8_t *dst, size_t dst_max,
    dflash_header_t *hdr_out);

/* Write a new blob, atomically, to the inactive bank. The header's
   sequence# is auto-incremented from the previous active bank.
   `payload_len` must be ? active bank capacity. Returns true on
   successful erase+write+verify; false otherwise. Blocking call ?
   typical 50-150 ms. */
bool dflash_save_blob(const dflash_header_t *hdr_template,
    const uint8_t *payload, size_t payload_len);

/* Factory reset ? erase both LoRaMac NVM banks. Does NOT touch the
   legacy Block 0 (credentials) or Block 1 (session-state log). */
bool dflash_factory_reset(void);

/* Read 40-byte "LWCR" credentials record from Block 0 (written by
   `provision_credentials.py`). On success, fills the three buffers
   (DevEUI / JoinEUI / AppKey) with MSB-order bytes and returns true.
   Returns false on blank / wrong magic / version mismatch / CRC fail. */
bool dflash_load_credentials(uint8_t deveui[8], uint8_t joineui[8],
    uint8_t appkey[16]);

#ifdef __cplusplus
}
#endif

#endif /* LORAWAN_SYSTEM_FLASH_DFLASH_LWNVM_H */
