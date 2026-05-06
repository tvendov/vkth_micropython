/*
 * lorawan/glue/dma_board.h
 *
 * Optional DMAC channel for SX126x burst SPI (RX continuous mode, large
 * payloads). DTC via ra_sci_spi.c is the default and covers all <256 B
 * transactions; this glue exists for future upgrade.
 *
 * Phase 0: API surface only, no allocation.
 *
 * Reservation strategy (per resource audit):
 *   * DMAC7 last-fit (DAC streaming uses first-fit from channel 0,
 *     so 7 minimises collision probability).
 *   * Acquired via existing `ra_dmac_reserve()` in `ra/ra_utils.c`.
 *   * Freed when LoRa stack is deinit'd.
 */

#ifndef LORAWAN_GLUE_DMA_BOARD_H
#define LORAWAN_GLUE_DMA_BOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LORAWAN_DMAC_PREFERRED_CH (7u)

// Returns true if a DMAC channel was successfully reserved. Stores the
// channel number in *ch_out. Falls back to DTC if no channel free.
bool dma_board_acquire(uint8_t *ch_out);
void dma_board_release(uint8_t ch);

// Phase ≥3: fast non-blocking SPI burst via DMAC. Returns immediately;
// `done_cb` fires from scheduler-deferred context (NOT the DMAC ISR).
typedef void (*dma_board_done_cb_t)(void *ctx, bool ok);
bool dma_board_spi_burst_async(uint8_t ch,
    const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len,
    dma_board_done_cb_t done_cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif // LORAWAN_GLUE_DMA_BOARD_H
