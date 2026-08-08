/*
 * lorawan/system/flash/nvm_board.h
 *
 * Non-volatile storage active flash layer for LoRaMac-node `NvmDataMgmt*` callbacks.
 * Backs LoRaWAN session persistence (DevNonce, FCnt up/down, session
 * keys, NetID, RJcount).
 *
 * Storage strategy:
 *   * R_FLASH_HP in BGO mode (`data_flash_bgo = true`) ? write returns
 *     immediately, callback fires on FLASH_EVENT_WRITE_COMPLETE.
 *   * Two physical pages, ping-pong with sequence number for
 *     wear-levelling and atomic update.
 *   * In-flight writes: max 2 queued, additional rejected with retry.
 *
 */

#ifndef LORAWAN_SYSTEM_FLASH_NVM_BOARD_H
#define LORAWAN_SYSTEM_FLASH_NVM_BOARD_H

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

#ifdef __cplusplus
}
#endif

#endif // LORAWAN_SYSTEM_FLASH_NVM_BOARD_H
