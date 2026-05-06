/*
 * lorawan/glue/nvm_board.h
 *
 * Non-volatile storage glue for LoRaMac-node `NvmDataMgmt*` callbacks.
 * Backs LoRaWAN session persistence (DevNonce, FCnt up/down, session
 * keys, NetID, RJcount).
 *
 * Storage strategy (Phase 5+):
 *   * R_FLASH_HP in BGO mode (`data_flash_bgo = true`) — write returns
 *     immediately, callback fires on FLASH_EVENT_WRITE_COMPLETE.
 *   * Two physical pages, ping-pong with sequence number for
 *     wear-levelling and atomic update.
 *   * In-flight writes: max 2 queued, additional rejected with retry.
 *
 * Phase 0: interface only, no real storage. NvmDataMgmtStore returns
 * "stored" immediately and discards the data.
 */

#ifndef LORAWAN_GLUE_NVM_BOARD_H
#define LORAWAN_GLUE_NVM_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void nvm_board_init(void);
void nvm_board_deinit(void);

// LoRaMac-node compatible signatures (matched against
// peripherals/soft-se interface and apps/.../common/flash/nvm.c).
uint16_t NvmDataMgmtStore(void);
uint16_t NvmDataMgmtRestore(void);
bool NvmDataMgmtFactoryReset(void);

// Phase 6a debug accessors — last MIB-Get status code + total raw
// context size from the most recent NvmDataMgmtStore() attempt.
int    nvm_board_last_status(void);
size_t nvm_board_last_total_size(void);

#ifdef __cplusplus
}
#endif

#endif // LORAWAN_GLUE_NVM_BOARD_H
