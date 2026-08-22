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

#if MICROPY_HW_ENABLE_IQ_ADC

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

/* Phase-3 block-boundary DSP: DC removal + x2 decimation, done in C with no
 * allocation and no Python (REQ-DSP-001).  Produces block_samples/2 centered
 * int16 samples per channel per block.  This struct exposes the counters and the
 * last-block DC means for control-plane inspection; it is not the realtime path. */
typedef struct {
    uint32_t dsp_blocks;    /* blocks processed by the C DSP stage since start() */
    uint16_t dsp_samples;   /* decimated samples produced per block (block/2)     */
    int16_t i_mean;         /* last block DC mean removed from I                  */
    int16_t q_mean;         /* last block DC mean removed from Q                  */
} ra_iq_dsp_status_t;

void ra_iq_adc_get_dsp_status(ra_iq_dsp_status_t *status);

/* Phase-4 AM demodulation to DAC.  The decimated I/Q from the phase-3 DSP is
 * envelope-detected (alpha-max-beta-min, integer only), DC-blocked to center the
 * envelope at mid-scale, and pushed into a single-producer/single-consumer
 * lock-free ring.  The producer is the ADC0_SCAN_END block callback; the consumer
 * is the DAC DMAC ping-pong fill callback.  No CPU is in the per-sample DAC path:
 * the ra_dac double-buffered stream clocks one sample to DADR per timer tick via
 * DMAC.  The audio rate is sample_rate_hz/2 (the decimated rate). */
typedef struct {
    uint32_t audio_underruns;   /* fill_cb ticks where the ring was empty        */
    uint32_t ring_overruns;     /* produced samples dropped because ring was full */
    uint8_t am_active;          /* AM->DAC path is running                        */
} ra_iq_am_status_t;

/* dac_pin must be a DAC-capable pin (P014 / DA0 on VK_RA6M3); dac_ch selects the
 * DAC channel.  Requires the capture to be running.  Returns false if capture is
 * not running, the pin is not a DAC pin, or the DAC stream fails to start. */
bool ra_iq_adc_am_dac_start(uint32_t dac_pin, uint8_t dac_ch);
void ra_iq_adc_am_dac_stop(void);
void ra_iq_adc_get_am_status(ra_iq_am_status_t *status);

#endif /* MICROPY_HW_ENABLE_IQ_ADC */

#endif /* MICROPY_INCLUDED_RENESAS_RA_IQ_ADC_H */
