/*
 * lorawan/boards/vk_ra4m2_sx126x/lorawan_pump.h
 *
 * Minimal foreground service request shim. The board/timer callbacks only
 * mark that LoRaMacProcess() must be serviced from the binding foreground
 * path; this is not a parallel LoRaWAN state machine.
 */

#ifndef LORAWAN_BOARD_VK_LORAWAN_PUMP_H
#define LORAWAN_BOARD_VK_LORAWAN_PUMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single board-layer entry: DIO1 ISR (sx126x-board.c) requests foreground
 * LoRaMac service. lorawan_pump_init() is internal to mod_lorawan.c. */
void lorawan_pump_request_dio1(void);

#ifdef __cplusplus
}
#endif

#endif /* LORAWAN_BOARD_VK_LORAWAN_PUMP_H */
