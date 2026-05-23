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

typedef enum {
    LORAWAN_PUMP_REASON_NOTIFY   = 1,
    LORAWAN_PUMP_REASON_TIMER    = 2,
    LORAWAN_PUMP_REASON_DIO1     = 3,
    LORAWAN_PUMP_REASON_PY       = 4,
    LORAWAN_PUMP_REASON_INTERNAL = 5,
} lorawan_pump_reason_t;

void lorawan_pump_init(void);

#define LORAWAN_PUMP_REQUEST_TIMER   (1u << 0)
#define LORAWAN_PUMP_REQUEST_DIO1    (1u << 1)
#define LORAWAN_PUMP_REQUEST_NOTIFY  (1u << 2)
#define LORAWAN_PUMP_REQUEST_PY      (1u << 3)

void lorawan_pump_request(uint32_t reason_mask);

#ifdef __cplusplus
}
#endif

#endif /* LORAWAN_BOARD_VK_LORAWAN_PUMP_H */
