/* Setup/control-time mathematics for the autonomous RA transmitter. */
#ifndef MICROPY_INCLUDED_RENESAS_RA_TX_CORE_H
#define MICROPY_INCLUDED_RENESAS_RA_TX_CORE_H

#include "ra_tx_hw.h"

#define RA_TX_SSB_TAPS (255U)
#define RA_TX_SSB_RING (256U)
typedef struct {
    int16_t history[RA_TX_SSB_RING];
    uint16_t next;
    bool primed;
    uint32_t clips;
} ra_tx_ssb_state_t;

/* One persistent context for the entire stream, never reset at block boundaries.
 * Fixed 12 kHz analytic voice filter. USB: I+jQ at +f; LSB: conjugate at -f.
 * Caller validates config once and owns serialization. No heap or float work.
 * amplitude maps +/-2048 ADC codes to the nominal DAC peak; overload uses one
 * shared limiter for I and Q. Reset only at an explicit start/new stream.
 */
void ra_tx_core_ssb_reset(ra_tx_ssb_state_t *state);
void ra_tx_core_ssb_sample(ra_tx_ssb_state_t *state, const ra_tx_config_t *config,
    uint16_t adc, uint16_t *i_code, uint16_t *q_code);

bool ra_tx_core_validate(const ra_tx_config_t *config);

/* Compact setup-time LUT requirements.  The alignment keeps each complete
 * table inside one 64-KiB address window, so the DTC only has to patch the
 * low halfword of a future source address.  CW needs no LUT. */
size_t ra_tx_core_lut_bytes(const ra_tx_config_t *config);
size_t ra_tx_core_lut_alignment(const ra_tx_config_t *config);

/* Writes little-endian DAC codes into one bank; never called per sample.
 * CW needs no bank and accepts NULL/0. AM and FM use their compact sizes from
 * ra_tx_core_lut_bytes().
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
