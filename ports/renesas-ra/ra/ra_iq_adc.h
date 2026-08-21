/*
 * Coherent I/Q capture for RA6M3: AGT -> ELC -> ADC0 + ADC1 -> DTC -> ping-pong.
 *
 * Implements ARCH-ADC-001, ARCH-TRIG-002, ARCH-ADC-002 and ARCH-BUF-001 of
 * SDR-RA6M3-RULES-v0.3.  Two points of the design are load bearing:
 *
 *   1. Both ADC units are started by the SAME ELC event.  ELSR8 (ELC_AD00) and
 *      ELSR10 (ELC_AD10) are linked to one timer event, the units share ADCLK
 *      and carry identical sampling times, so their dedicated sample-and-hold
 *      circuits capture at the same instant.
 *
 *   2. Both results are moved by ONE activation.  A two-descriptor DTC chain on
 *      ADC0_SCAN_END reads ADDR of unit 0 into the I buffer and ADDR of unit 1
 *      into the Q buffer.  Two independent transfers would be able to lose one
 *      activation on one side only, which shifts Q against I permanently and
 *      shows up as an uncorrectable phase rotation that the demodulator cannot
 *      detect.  ADC1_SCAN_END is therefore a diagnostic, not a second transport
 *      trigger.
 *
 * Evidence status: code review only.  Nothing in this file has been executed on
 * hardware, and the DTC chain + repeat + ping-pong behaviour in particular is
 * the first thing that has to be checked on the bench (REQ-WORK-004).
 */

#ifndef MICROPY_INCLUDED_RENESAS_RA_IQ_ADC_H
#define MICROPY_INCLUDED_RENESAS_RA_IQ_ADC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ra_adc.h"

#if defined(RA6M3)

#define RA_IQ_ADC_MAX_BLOCK_SAMPLES (256)

typedef struct {
    uint8_t initialised;
    uint8_t running;
    uint8_t ready;              /* a completed block is waiting to be acquired */
    uint8_t active_half;        /* half the DTC is currently filling           */
    int8_t last_error;          /* 0 = none, negative = ra_iq_adc_error_t      */
    uint32_t i_pin;
    uint32_t q_pin;
    uint32_t sample_rate_hz;
    uint16_t block_samples;
    uint32_t blocks;            /* completed blocks since start()              */
    uint32_t overruns;          /* blocks overwritten before being acquired    */
    uint32_t unit1_stalls;      /* blocks with no scan-end seen from unit 1    */
} ra_iq_adc_status_t;

typedef enum {
    RA_IQ_ADC_ERR_NONE = 0,
    RA_IQ_ADC_ERR_OVERRUN = -1,
    RA_IQ_ADC_ERR_UNIT1_STALL = -2,
} ra_iq_adc_error_t;

/* i_pin must be an AN000..AN002 pin, q_pin an AN100..AN102 pin: only those six
 * channels carry the dedicated sample-and-hold circuits and the PGA.
 * pga_mode is applied to both channels; RA_ADC_PGA_BYPASS is the safe default,
 * and RA_ADC_PGA_OFF is rejected because ADC12 does not work in that state. */
bool ra_iq_adc_init(uint32_t i_pin, uint32_t q_pin, uint32_t sample_rate_hz,
    size_t block_samples, ra_adc_pga_mode_t pga_mode, uint8_t pga_gain);
void ra_iq_adc_deinit(void);

bool ra_iq_adc_start(void);
void ra_iq_adc_stop(void);

/* Hands out the half that was completed last.  Returns false when no block is
 * ready.  The pointers stay valid until the block after next completes, which
 * is one block period - the caller must consume them inside that window. */
bool ra_iq_adc_acquire(const uint16_t **i_block, const uint16_t **q_block,
    size_t *block_samples, uint32_t *sequence);

void ra_iq_adc_get_status(ra_iq_adc_status_t *status);

#endif /* RA6M3 */

#endif /* MICROPY_INCLUDED_RENESAS_RA_IQ_ADC_H */
