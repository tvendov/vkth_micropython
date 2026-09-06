/* Setup/control-time mathematics for the autonomous RA transmitter. */
#ifndef MICROPY_INCLUDED_RENESAS_RA_TX_CORE_H
#define MICROPY_INCLUDED_RENESAS_RA_TX_CORE_H

#include "ra_tx_hw.h"

bool ra_tx_core_validate(const ra_tx_config_t *config);

/* Writes little-endian DAC codes into one bank; never called per sample.
 * CW needs no bank and accepts NULL/0. AM/FM require RA_TX_LUT_BYTES.
 * Alignment and ownership of the bank are hardware-backend responsibilities.
 */
bool ra_tx_core_build_lut(const ra_tx_config_t *config, uint8_t *bank, size_t bytes);

/* 'from' is already held before the first timer event. out[j-1] is the
 * code for event j, j=1..n, so n events span n/Fs seconds. The final code
 * is exactly 'to'. NULL or n=0 is a no-op. Caller supplies n entries.
 * Prepare the normalized 0..65535 shape once at initialization; use
 * ra_tx_core_scale_ramp at key edges. Never call this in a sample ISR.
 */
void ra_tx_core_ramp(uint16_t *out, uint16_t n, uint16_t from, uint16_t to);

/* Integer-only scaling of n normalized shape entries to the requested
 * endpoints. shape must be monotonic 0..65535 (prepared at initialization).
 * Round signed changes symmetrically; force the final entry to exactly 'to'.
 * NULL buffers or n=0 are no-ops. Caller supplies n entries in both buffers.
 * This is control-event work only; it is not part of sample processing.
 */
void ra_tx_core_scale_ramp(uint16_t *out, const uint16_t *shape, uint16_t n, uint16_t from, uint16_t to);

/* DOC addend -fm_gain*adc_mid modulo 65536, for a validated config.
 * A NULL config returns zero.
 */
uint16_t ra_tx_core_fm_bias(const ra_tx_config_t *config);

#endif
