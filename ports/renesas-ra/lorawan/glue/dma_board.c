/*
 * lorawan/glue/dma_board.c
 *
 * Phase 0 stubs. Real DMAC plumbing arrives in a later phase only if SPI
 * payload sizes prove DTC-bound. Default path keeps DTC-via-ra_sci_spi.
 */

#include "py/runtime.h"
#include "glue/dma_board.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

bool dma_board_acquire(uint8_t *ch_out) {
    (void)ch_out;
    // Phase 0: report "no channel" so callers fall back to DTC path.
    return false;
}

void dma_board_release(uint8_t ch) {
    (void)ch;
}

bool dma_board_spi_burst_async(uint8_t ch,
    const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len,
    dma_board_done_cb_t done_cb, void *ctx) {
    (void)ch; (void)tx_buf; (void)rx_buf; (void)len; (void)done_cb; (void)ctx;
    return false;
}

#endif // MICROPY_HW_LORA_STACK_RENESAS
