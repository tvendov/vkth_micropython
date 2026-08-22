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

/* Per-block DSP processing time (dsp_process + demod_produce) measured with the DWT
 * cycle counter.  avg is over all blocks since start(); cpu_hz is SystemCoreClock so
 * the caller can turn cycles into a fraction of the block period.  Control-plane. */
void ra_iq_adc_get_timing(uint32_t *last_cyc, uint32_t *max_cyc, uint32_t *avg_cyc,
    uint32_t *cpu_hz);

/* Hybrid CMSIS-DSP stage 1: select the x2 decimation kernel.  0 = hand integer
 * half-band FIR (default, fallback), 1 = CMSIS arm_fir_decimate_q15.  Same symmetric
 * half-band taps, so the two paths produce equivalent output; the switch exists to
 * A/B their per-block cost (iq.timing()) before committing the stage.  Control-plane. */
void ra_iq_adc_set_dec_kernel(uint8_t use_cmsis);
uint8_t ra_iq_adc_get_dec_kernel(void);

/* Hybrid CMSIS-DSP stage 2: select the SSB Hilbert kernel.  1 = CMSIS arm_fir_q15
 * (default, measured ~15% faster on target), 0 = hand loop (fallback).  Same 31
 * antisymmetric taps and 15-sample group delay, so the output is equivalent; A/B their
 * per-block cost (iq.timing()) on USB/LSB.  Control-plane. */
void ra_iq_adc_set_hil_kernel(uint8_t use_cmsis);
uint8_t ra_iq_adc_get_hil_kernel(void);

/* Hybrid CMSIS-DSP stage 3: select the channel-filter kernel.  0 = integer one-pole
 * cascade (default, fallback), 1 = f32 4-pole Butterworth arm_biquad_cascade_df1_f32.
 * f32 (not q15) is deliberate: the filter is not the timing bottleneck but needs an
 * accurate response over a wide cutoff and must not saturate like the integer one-pole
 * at fc >= ~3820 Hz.  A/B their per-block cost with iq.timing().  Control-plane. */
void ra_iq_adc_set_chf_kernel(uint8_t use_f32);
uint8_t ra_iq_adc_get_chf_kernel(void);

/* Hybrid CMSIS-DSP stage 4: select the AM envelope kernel.  0 = integer alpha-max-beta-
 * min (default, fallback), 1 = f32 arm_cmplx_mag_f32 (exact magnitude, no ~4% ripple).
 * A/B their per-block cost with iq.timing() on AM.  Control-plane. */
void ra_iq_adc_set_mag_kernel(uint8_t use_f32);
uint8_t ra_iq_adc_get_mag_kernel(void);

/* Post-demod AUDIO filter preset, applied to the demodulated audio before the AGC:
 * OFF (bypass), AM (4.5 kHz low-pass), VOICE (300..2700 Hz band-pass for SSB), CW
 * (narrow band-pass peak at the 700 Hz BFO).  set_demod applies a per-mode default;
 * this overrides it.  f32 biquads, designed in the control plane. */
#define RA_IQ_AF_OFF   (0U)
#define RA_IQ_AF_AM    (1U)
#define RA_IQ_AF_VOICE (2U)
#define RA_IQ_AF_CW    (3U)
void ra_iq_adc_set_audio_filter(uint8_t mode);
uint8_t ra_iq_adc_get_audio_filter(void);

/* Squelch: mute the audio while the pre-AGC envelope stays below thresh, which also
 * freezes the AGC so it does not amplify noise on silence.  thresh in audio-envelope
 * units; 0 disables (default).  The gate has open/close hysteresis.  Control-plane. */
void ra_iq_adc_set_squelch(int32_t thresh);
void ra_iq_adc_get_squelch(int32_t *thresh, uint8_t *open, int32_t *env);

/* S-meter: smoothed RMS of the channel-filtered complex baseband (mode/AGC independent)
 * and its dBFS level vs the ~2047 baseband full-scale.  Either pointer may be NULL. */
void ra_iq_adc_get_smeter(int32_t *rms, float *dbfs);

/* Tuning NCO (digital fine-tune): shift an offset hz inside the captured baseband
 * (-fs/2 .. +fs/2, fs = sample_rate_hz/2) down to 0 Hz before the channel filter, so a
 * signal off the ADC centre can be tuned in without moving the analog LO.  In a Tayloe/
 * Si5351 front end the LO sets the coarse frequency and this is the digital fine-tune /
 * passband offset.  hz == 0 is centre (default); |hz| clamps just inside fs/2. */
void ra_iq_adc_set_tune(int32_t hz);
int32_t ra_iq_adc_get_tune(void);

/* Phase-4 demodulation.  The decimated I/Q from the phase-3 DSP is run through the
 * selected demod (currently AM: envelope-detected alpha-max-beta-min, DC-blocked to
 * center at mid-scale) and pushed into a single-producer/single-consumer lock-free
 * ring of DAC codes.  The producer is the ADC0_SCAN_END block callback; the consumer
 * is any DAC DMAC ping-pong fill callback that calls ra_iq_adc_audio_pull().  The
 * demodulator does NOT own the DAC: it only produces a generic audio stream.  The
 * audio rate is sample_rate_hz/2 (the decimated rate). */
typedef enum {
    RA_IQ_DEMOD_OFF = 0,
    RA_IQ_DEMOD_AM = 1,
    RA_IQ_DEMOD_USB = 2,    /* upper sideband, phasing method: I_delayed - H(Q) */
    RA_IQ_DEMOD_LSB = 3,    /* lower sideband, phasing method: I_delayed + H(Q) */
    RA_IQ_DEMOD_CW = 4,     /* CW: mix baseband up to a fixed 700 Hz beat, real part */
} ra_iq_demod_mode_t;

typedef struct {
    uint32_t audio_underruns;   /* pull ticks where the ring was empty            */
    uint32_t ring_overruns;     /* produced samples dropped because ring was full */
    uint8_t demod_mode;         /* current ra_iq_demod_mode_t                     */
} ra_iq_audio_status_t;

/* Select the demod mode.  0 = OFF, 1 = AM, 2 = USB, 3 = LSB, 4 = CW; any other
 * value clamps to OFF.  On a (re)selection the producer DSP state (DC blocker,
 * SSB Hilbert histories, CW NCO phase) and audio counters are reset so the
 * producer starts clean; the audio ring is NOT flushed so a mode switch does not
 * underrun the DAC (the ring is flushed once in ra_iq_adc_start).
 * Control-plane. */
void ra_iq_adc_set_demod(uint8_t mode);
uint8_t ra_iq_adc_get_demod(void);

/* Audio-stream parameters for a DAC consumer: freq_hz is the decimated audio rate
 * (sample_rate_hz/2), sample_count is the decimated block length (block_samples/2).
 * Either pointer may be NULL. */
void ra_iq_adc_get_audio_params(uint32_t *freq_hz, size_t *sample_count);

/* Generic SPSC consumer of the audio ring.  Pops n samples into buf; on an empty
 * ring writes 2048 (mid-scale silence) and counts an underrun so the stream never
 * stops.  Callable from any DMAC-ISR fill callback.  No allocation, no Python
 * (REQ-RT-002/003).  Always returns n. */
size_t ra_iq_adc_audio_pull(uint16_t *buf, size_t n);

void ra_iq_adc_get_audio_status(ra_iq_audio_status_t *status);

/* Manual I/Q imbalance correction, applied to the decimated I/Q before demod:
 * I is the reference, Q' = amp*Q + phase*I in Q15 (amp = 32768 is unity, phase =
 * 0 is no skew).  Disabled by default.  Control-plane setters/getters. */
void ra_iq_adc_set_iq_correction(uint8_t enable, int32_t amp_q15, int32_t phase_q15);
void ra_iq_adc_get_iq_correction(uint8_t *enable, int32_t *amp_q15, int32_t *phase_q15);

/* Channel low-pass filter, applied to the decimated complex I/Q AFTER the I/Q
 * imbalance correction and BEFORE demod/AGC, so those stages do not work on
 * out-of-channel noise.  N=4 cascaded one-pole sections (~24 dB/oct), integer,
 * allocation-free, run in the block callback.  The cutoff (hz) is turned into a
 * Q15 pole coefficient in the control plane from hz and the current audio rate
 * (sample_rate_hz/2).  hz == 0, or hz >= fs/2, => BYPASS: the path is then
 * bit-identical to no filter.  set_bandwidth recomputes the coefficient and
 * resets the section state so a cutoff change does not click; ra_iq_adc_set_demod
 * applies a per-mode default cutoff and does the same reset. */
void ra_iq_adc_set_bandwidth(uint32_t hz);
uint32_t ra_iq_adc_get_bandwidth(void);
uint8_t ra_iq_adc_filter_bypassed(void);

/* Audio-output stages applied per audio sample in the block callback, between the
 * demod and the ring push: an integer RMS AGC and a manual master volume, followed
 * by a peak limiter that keeps 2048+audio inside 0..4095.
 *
 * AGC mode: 0 = off (unity, bypass), 1 = fast (AM/voice), 2 = slow (SSB/CW, no
 * keying pump), 3 = manual (fixed gain_q15, no adaptation).  gain_q15 is used only
 * in manual mode; target is the RMS servo set-point in audio amplitude units and
 * target <= 0 keeps the current value.  The running servo state (mean-square and
 * gain) is reset on each demod (re)selection; mode/target/shifts/gain-cap and the
 * master volume persist across mode changes.  Control-plane setters/getters. */
void ra_iq_adc_set_agc(uint8_t mode, int32_t gain_q15, int32_t target);
void ra_iq_adc_get_agc(uint8_t *mode, int32_t *gain_q15, int32_t *target,
    int32_t *env, uint32_t *clips);

/* Master output volume to the DAC, Q15 (32768 = unity).  Applied after the AGC. */
void ra_iq_adc_set_volume(int32_t vol_q15);
int32_t ra_iq_adc_get_volume(void);

/* Spectrum (FFT) path.  256-point complex FFT of the decimated I/Q baseband.  The
 * block callback (ISR) only copies decimated samples into an integer accumulator,
 * gated by ra_iq_adc_spectrum_enable(); the float window + FFT + magnitude run in
 * ra_iq_adc_spectrum() from the Python control plane (uses the FPU, not ISR-safe).
 * ra_iq_adc_spectrum() fills out[] with the magnitude spectrum, fftshift-ed so DC
 * is at out[N/2], out[0] = -fs_audio/2 and out[N-1] = +fs_audio/2 - one bin; it
 * writes min(n, N) bins and returns false when no fresh snapshot is ready. */
#define RA_IQ_SPECTRUM_N (256)
bool ra_iq_adc_spectrum(float *out, size_t n);

/* Allocation-free UI accessors: the FFT->bars reduction (dB-scaled int16 heights) and
 * the counter snapshot are done entirely in C, filling caller-preallocated buffers, so
 * the Python poll loop never creates a MicroPython object (no boxed floats, no dicts) --
 * required so GC never runs and stalls the realtime ADC ISR. */
bool ra_iq_adc_spectrum_bars(int16_t *out, size_t nbars, int16_t max_h);
size_t ra_iq_adc_get_counters(int32_t *out, size_t n);
void ra_iq_adc_spectrum_enable(uint8_t on);
size_t ra_iq_adc_spectrum_size(void);
uint32_t ra_iq_adc_spectrum_bin_hz(void);

#endif /* MICROPY_HW_ENABLE_IQ_ADC */

#endif /* MICROPY_INCLUDED_RENESAS_RA_IQ_ADC_H */
