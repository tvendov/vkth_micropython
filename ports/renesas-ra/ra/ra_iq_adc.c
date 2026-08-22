/*
 * Coherent I/Q capture for RA6M3.  See ra_iq_adc.h for the design rationale.
 *
 * Register facts are taken from R01UH0886EJ0120 Rev.1.20 (DOC-MCU-001):
 *   47.2.15  ADSHCR   - SHANS (use / bypass the dedicated S&H), SSTSH
 *   47.2.16  ADSHMSR  - SHMD (continuous sampling); ADST at least 400 ns later
 *   47.3.11  starting a scan from a synchronous (ELC) trigger
 *   47.5.2   ELSR8 = ELC_AD00 (unit 0), ELSR10 = ELC_AD10 (unit 1)
 *   47.6.8   Table 47.14 - valid ASEL / ADPGACR / ADSHCR combinations
 */

#include <string.h>

#include "hal_data.h"
#include "r_adc.h"
#include "r_dtc.h"
#include "ra_adc.h"
#include "ra_iq_adc.h"
#include "ra_sdr_caps.h"
#include "ra_timer.h"
#include "ra_utils.h"
#include "vector_data.h"

#if MICROPY_HW_ENABLE_IQ_ADC

#include "arm_math.h"
#include "arm_const_structs.h"

/* Sampling time applied to both units.  Identical values on the two units are
 * required by ARCH-ADC-002: unequal sampling times move the aperture of one
 * channel against the other. */
#define RA_IQ_ADSSTR    (0x0BU)     /* 11 states, the reset value            */
#define RA_IQ_SSTSH     (0x18U)     /* 24 states, the ADSHCR reset value     */

/* Block-boundary interrupt.  One per block, never per sample (REQ-RT-004). */
#ifndef RA_IQ_ADC_IRQ_PRIORITY
#define RA_IQ_ADC_IRQ_PRIORITY (2U)
#endif

#define RA_IQ_ADC_CHANNELS_PER_UNIT (32U)

typedef struct {
    bool opened0;
    bool opened1;
    bool dtc_open;
    bool timer_reserved;
    bool i_pin_enabled;
    bool q_pin_enabled;
    uint8_t timer_ch;
    uint8_t i_ch;               /* 0..2   */
    uint8_t q_ch;               /* 32..34 */
    uint32_t sequence;
    uint8_t ready_half;
    adc_instance_ctrl_t adc0_ctrl;
    adc_instance_ctrl_t adc1_ctrl;
    adc_cfg_t adc0_cfg;
    adc_cfg_t adc1_cfg;
    adc_extended_cfg_t adc0_ext;
    adc_extended_cfg_t adc1_ext;
    adc_channel_cfg_t adc0_ch_cfg;
    adc_channel_cfg_t adc1_ch_cfg;
    dtc_instance_ctrl_t dtc_ctrl;
    dtc_extended_cfg_t dtc_ext;
    transfer_cfg_t dtc_cfg;
} ra_iq_adc_private_t;

static ra_iq_adc_private_t s_iq;
static ra_iq_adc_status_t s_status;

/* Per-block DSP processing time, measured with the DWT cycle counter around the
 * dsp_process + demod_produce work in the block callback.  This is the budget gate
 * for the CMSIS/f32 vs integer decision (measure before switching a stage). */
static volatile uint32_t s_proc_last;
static volatile uint32_t s_proc_max;
static volatile uint32_t s_proc_sum;
static volatile uint32_t s_proc_count;

/* DTC reads and writes its transfer information straight out of this array, so
 * it is the live state of the transfer, not a copy.  Descriptor 0 carries I and
 * chains into descriptor 1, which carries Q and raises the block interrupt. */
static transfer_info_t BSP_ALIGN_VARIABLE(4) s_dtc_info[2];

static uint16_t BSP_ALIGN_VARIABLE(4) s_i_buf[2][RA_IQ_ADC_MAX_BLOCK_SAMPLES];
static uint16_t BSP_ALIGN_VARIABLE(4) s_q_buf[2][RA_IQ_ADC_MAX_BLOCK_SAMPLES];

/* Phase-3 DSP output: DC-removed, x2-decimated block, produced in C on the block
 * boundary.  block_samples/2 signed samples per channel; overwritten each block. */
static int16_t BSP_ALIGN_VARIABLE(4) s_i_dc[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2];
static int16_t BSP_ALIGN_VARIABLE(4) s_q_dc[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2];
static ra_iq_dsp_status_t s_dsp;

/* x2 decimation half-band anti-alias FIR.  11-tap linear-phase half-band low-pass,
 * cutoff Fs/4, replacing the former (x[2j]+x[2j+1])>>1 two-sample average.  A
 * half-band filter has its center tap = 0.5 and every even-offset tap (except the
 * center) exactly 0, so a length-11 filter has non-zero taps only at offsets 0,
 * +/-1, +/-3, +/-5 -- and the +/-5 endpoints vanish here because the Blackman window
 * is 0 at n=0 and n=10.  Five non-zero taps remain (center + the +/-1 and +/-3 pairs).
 *
 * Generated in double precision (windowed-sinc), then scaled to Q15:
 *   hd[n] = 0.5              for offset k = n-5 == 0
 *         = sin(pi*k/2)/(pi*k)   otherwise            (ideal half-band Fs/4 LPF)
 *   w[n]  = 0.42 - 0.5*cos(2*pi*n/10) + 0.08*cos(4*pi*n/10)   (Blackman)
 *   h[n]  = hd[n]*w[n];  the even-offset non-center taps computed as ~1e-17 and are
 *           forced to exactly 0.
 * Normalized for unity DC gain: q[n] = round(h[n]/sum(h) * 32768), with the +/-32768
 * rounding residue folded into the center tap so sum(q) == 32768 exactly (a raw
 * round left sum == 32768 already here; the center tap absorbs any residue).  The
 * center tap is 16416 rather than 16384 because renormalizing after zeroing the
 * windowed endpoint taps redistributes their (tiny) DC share onto the passband.
 *
 * Magnitude response vs the old 2-sample average (the band Fs/4..Fs/2 folds into
 * baseband on x2 decimation, so this is the alias-rejection that matters):
 *   0.375 Fs: -21.1 dB (FIR) vs -8.3 dB (avg);  0.4375 Fs: -35.5 dB vs -14.2 dB.
 * Half-band symmetry pins -6 dB exactly at Fs/4.  Integer conv, >>15 back to Q0. */
#define RA_IQ_DEC_TAPS (11U)
static const int16_t s_dec_hb[RA_IQ_DEC_TAPS] = {
    0, 0, -699, 0, 8875, 16416, 8875, 0, -699, 0, 0,
};

/* Full-rate delay line of the last RA_IQ_DEC_TAPS-1 raw samples per channel, so the
 * FIR can be centered on even positions near the start of a block (its support
 * reaches into the previous block).  Single-producer (block callback) owned; reset
 * to zero at capture start and carried across blocks while running. */
static int16_t s_dec_hist_i[RA_IQ_DEC_TAPS - 1U];
static int16_t s_dec_hist_q[RA_IQ_DEC_TAPS - 1U];

/* Manual I/Q imbalance correction (Q15), applied to the decimated I/Q before any
 * demodulation.  Standard two-parameter linear model: I is the reference, and
 * Q' = amp*Q + phase*I compensates the gain and quadrature-skew mismatch between
 * ADC0 and ADC1.  Disabled by default so the path is unchanged until the operator
 * sets it.  amp defaults to 1.0 (32768 in Q15), phase to 0.  Written from Python
 * (control plane) and read by the block callback; separate word reads make a
 * mid-update read harmless (transient parameter, no crash). */
static volatile uint8_t s_iqc_enable;
static volatile int32_t s_iqc_amp_q15 = 32768;
static volatile int32_t s_iqc_phase_q15;

/* Channel low-pass filter, applied to the decimated I/Q in ra_iq_dsp_process AFTER
 * the imbalance correction, BEFORE the demod reads s_i_dc/s_q_dc.  N=4 cascaded
 * one-pole sections give ~24 dB/oct and are unconditionally stable in integer:
 *   per section  y += ((x - y) * alpha_q15) >> 15;  x = y;
 * A Q15 biquad was rejected because its feedback coefficient exceeds the Q15 range
 * near audio-rate cutoffs.  The section accumulators are int32 for headroom over the
 * int16 samples.  Producer-owned (block callback), so no lock on the running state.
 *
 * s_filt_alpha_q15 == 32768 (unity pole) means BYPASS: the DSP loop then skips the
 * cascade entirely so the path is bit-identical to no filter.  s_filt_bw_hz is the
 * requested cutoff kept so a rate change / demod re-select can recompute alpha.
 * s_filt_alpha_q15 and s_filt_bw_hz are computed in the control plane (set_bandwidth
 * / set_demod), never in the ISR (REQ-RT-002/003). */
#define RA_IQ_FILT_SECTIONS (4U)

static int32_t s_filt_i[RA_IQ_FILT_SECTIONS];
static int32_t s_filt_q[RA_IQ_FILT_SECTIONS];
static volatile int32_t s_filt_alpha_q15 = 32768;  /* 32768 = unity pole = bypass */
static volatile uint32_t s_filt_bw_hz;             /* 0 = bypass                   */

/* Phase-4 demod audio.  Single-producer (ADC0_SCAN_END block callback) /
 * single-consumer (DAC DMAC fill callback via ra_iq_adc_audio_pull) lock-free ring
 * of DAC codes.  Power of two so count/wrap are masks.  s_ring_head is owned by the
 * producer only, s_ring_tail by the consumer only; each publishes its index after
 * the data write so no critical section is needed (REQ-RT-002).  count = (head -
 * tail) & MASK.  The DAC itself is owned by machine.DAC, not this file. */
#define RA_IQ_AUDIO_RING (1024U)
#define RA_IQ_AUDIO_RING_MASK (RA_IQ_AUDIO_RING - 1U)

static uint16_t s_audio_ring[RA_IQ_AUDIO_RING];
static volatile uint32_t s_ring_head;   /* producer owns */
static volatile uint32_t s_ring_tail;   /* consumer owns */

/* Q8 slow LPF of the envelope, used as an IIR DC blocker so the audio is
 * AC-coupled around mid-scale.  Owned by the producer.  AM-only. */
static int32_t s_env_mean;

/* Audio-output stages inserted between each demod case and the ring push:
 *   signed audio  ->  RMS AGC  ->  * master volume  ->  peak limiter  ->  DAC code.
 * All integer, all producer-owned (block callback), so no lock is needed on the
 * running state.  The volatile fields are written from the control plane and read
 * by the producer; separate word reads make a mid-update read harmless (a servo
 * parameter, no crash).  REQ-RT-002/003: no FPU, no allocation, no Python here. */
#define RA_IQ_AGC_MODE_OFF    (0U)
#define RA_IQ_AGC_MODE_FAST   (1U)
#define RA_IQ_AGC_MODE_SLOW   (2U)
#define RA_IQ_AGC_MODE_MANUAL (3U)

#define RA_IQ_AGC_GAIN_UNITY  (32768)   /* Q15 1.0                              */
#define RA_IQ_AGC_GAIN_MIN    (256)     /* Q15 ~1/128, servo floor              */

/* PI gain servo with asymmetric attack/decay.  att_sh < dec_sh => attack (output
 * too loud) is faster than decay (recovery when quiet), the standard SSB/CW rule.
 * kp_sh is the proportional feed-forward strength (larger shift = weaker P).
 * fast preset is also the power-on default so agc("fast") without args matches. */
#define RA_IQ_AGC_FAST_MS_SH   (6U)
#define RA_IQ_AGC_FAST_ATT_SH  (5U)
#define RA_IQ_AGC_FAST_DEC_SH  (9U)
#define RA_IQ_AGC_FAST_KP_SH   (6U)
/* slow preset: long RMS window and very slow decay so SSB/CW keying does not pump. */
#define RA_IQ_AGC_SLOW_MS_SH   (9U)
#define RA_IQ_AGC_SLOW_ATT_SH  (6U)
#define RA_IQ_AGC_SLOW_DEC_SH  (13U)
#define RA_IQ_AGC_SLOW_KP_SH   (8U)

#define RA_IQ_AGC_INV_SH       (22)             /* 1/target fixed-point shift    */
#define RA_IQ_AGC_TARGET_MIN   (16)             /* keeps 1/target bounded        */
#define RA_IQ_AGC_TARGET_DEF   (1000)           /* ~half of the ~2048 range      */
#define RA_IQ_AGC_GAIN_MAX_DEF (8 * 32768)      /* Q15 8.0                       */

static volatile uint8_t s_agc_mode = RA_IQ_AGC_MODE_OFF;
static int32_t s_agc_ms;                        /* smoothed mean-square         */
static int32_t s_agc_gain_q15 = RA_IQ_AGC_GAIN_UNITY;
static volatile int32_t s_agc_target = RA_IQ_AGC_TARGET_DEF;
static volatile uint8_t s_agc_ms_sh = RA_IQ_AGC_FAST_MS_SH;
static volatile uint8_t s_agc_att_sh = RA_IQ_AGC_FAST_ATT_SH;
static volatile uint8_t s_agc_dec_sh = RA_IQ_AGC_FAST_DEC_SH;
static volatile uint8_t s_agc_kp_sh = RA_IQ_AGC_FAST_KP_SH;
static volatile int32_t s_agc_inv_target = ((1 << RA_IQ_AGC_INV_SH) / RA_IQ_AGC_TARGET_DEF);
static volatile int32_t s_agc_gain_max = RA_IQ_AGC_GAIN_MAX_DEF;
static int32_t s_agc_env;                        /* last computed RMS envelope   */
static uint32_t s_agc_clips;                     /* limiter saturation count     */

/* Master output volume applied after the AGC, Q15.  Control-plane owned. */
static volatile int32_t s_vol_q15 = RA_IQ_AGC_GAIN_UNITY;

/* Integer sqrt (Newton-free, bitwise).  Exact floor(sqrt(x)) for uint32. */
static uint32_t ra_iq_isqrt32(uint32_t x) {
    uint32_t rem = 0U;
    uint32_t root = 0U;
    for (uint32_t i = 0U; i < 16U; ++i) {
        root <<= 1;
        rem = (rem << 2) | (x >> 30);
        x <<= 2;
        if (root < rem) {
            rem -= root | 1U;
            root += 2U;
        }
    }
    return root >> 1;
}

/* Shared audio-output tail: AGC servo + master volume + peak limiter.  Takes the
 * signed audio the demod case produced and returns the clamped DAC code in
 * 0..4095.  One implementation for all demod modes (Step 0 refactor). */
static inline uint16_t ra_iq_audio_stage(int32_t audio) {
    uint8_t mode = s_agc_mode;

    int32_t eff_gain;
    if ((mode != RA_IQ_AGC_MODE_OFF) && (mode != RA_IQ_AGC_MODE_MANUAL)) {
        int32_t p = audio * audio;                          /* |audio| < ~2048  */
        s_agc_ms += (p - s_agc_ms) >> s_agc_ms_sh;
        int32_t env = (int32_t)ra_iq_isqrt32((uint32_t)(s_agc_ms < 0 ? 0 : s_agc_ms));
        s_agc_env = env;
        int32_t out_env = (env * s_agc_gain_q15) >> 15;

        /* PI gain servo in the log domain.  rel = (target - out_env)/target in Q15
         * is the relative (dB-like) error.  Integral: gain *= 1 + rel/2^sh, with
         * asymmetric attack (out too loud, e<0 -> att_sh, fast) vs decay (quiet,
         * e>0 -> dec_sh, slow); the error-proportional step settles smoothly with
         * no bang-bang dither.  Proportional: an un-accumulated feed-forward of
         * rel/2^kp_sh added to the applied gain for faster transient response. */
        int32_t e = s_agc_target - out_env;
        int32_t rel = (int32_t)(((int64_t)e * s_agc_inv_target) >> (RA_IQ_AGC_INV_SH - 15));
        uint8_t sh = (e < 0) ? s_agc_att_sh : s_agc_dec_sh;
        s_agc_gain_q15 += (int32_t)(((int64_t)s_agc_gain_q15 * rel) >> (15 + sh));
        if (s_agc_gain_q15 < RA_IQ_AGC_GAIN_MIN) {
            s_agc_gain_q15 = RA_IQ_AGC_GAIN_MIN;
        } else if (s_agc_gain_q15 > s_agc_gain_max) {
            s_agc_gain_q15 = s_agc_gain_max;
        }
        eff_gain = s_agc_gain_q15 + (int32_t)(((int64_t)s_agc_gain_q15 * rel) >> (15 + s_agc_kp_sh));
        if (eff_gain < RA_IQ_AGC_GAIN_MIN) {
            eff_gain = RA_IQ_AGC_GAIN_MIN;
        } else if (eff_gain > s_agc_gain_max) {
            eff_gain = s_agc_gain_max;
        }
    } else if (mode == RA_IQ_AGC_MODE_OFF) {
        s_agc_gain_q15 = RA_IQ_AGC_GAIN_UNITY;
        eff_gain = RA_IQ_AGC_GAIN_UNITY;
    } else {
        eff_gain = s_agc_gain_q15;   /* manual: operator-set constant, applied as-is */
    }

    audio = (audio * eff_gain) >> 15;
    audio = (audio * s_vol_q15) >> 15;

    if (audio > 2047) {
        audio = 2047;
        s_agc_clips++;
    } else if (audio < -2048) {
        audio = -2048;
        s_agc_clips++;
    }
    return (uint16_t)(2048 + audio);
}

/* SSB phasing demodulator (USB/LSB).  31-tap Type-III (odd length, antisymmetric)
 * Hilbert transformer, Q15.  Generated as:
 *
 *   center tap n = 15 is 0; all even offsets from center are 0; for odd offset
 *   k in {1,3,5,7,9,11,13,15}:
 *       h[15+k] = -(2/(pi*k)) * w[15+k]
 *       h[15-k] = +(2/(pi*k)) * w[15-k]
 *   with the Blackman window
 *       w[n] = 0.42 - 0.5*cos(2*pi*n/30) + 0.08*cos(4*pi*n/30), n = 0..30.
 *   Each value * 32768, rounded to nearest int.
 *
 * The window is symmetric (w[15+k] == w[15-k]) so the taps come out antisymmetric.
 * The k = 15 endpoint taps (n = 0, 30) vanish because w[0] = w[30] = 0, so the
 * effective non-zero taps are at offsets +/-{1,3,5,7,9,11,13}.  Full 31-entry array
 * (index n = tap position, offset = n - 15) is stored for clarity:
 *
 *   n:  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14
 *   h:  0   0  +27  0 +146  0 +465  0 +1174 0 +2628 0 +5905 0 +20489
 *   n: 15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30
 *   h:  0 -20489 0 -5905 0 -2628 0 -1174 0 -465 0 -146 0 -27  0   0
 *
 * Group delay is (31-1)/2 = 15 taps; the I path is delayed by 15 to align with H(Q).
 * Convolution: hq = sum_n h[n] * q[pos - n]; the newest sample sits at q[pos]. */
static const int16_t s_hil_taps[31] = {
    0,     0,     27,    0,     146,   0,     465,   0,
    1174,  0,     2628,  0,     5905,  0,     20489, 0,
    -20489, 0,    -5905, 0,     -2628, 0,     -1174, 0,
    -465,  0,     -146,  0,     -27,   0,     0,
};

/* Circular histories of the last 31 decimated I and Q samples (int16), and the
 * write index.  These persist across blocks because the FIR spans block
 * boundaries.  Single-producer (block callback) owned, so no lock is needed. */
static int16_t s_hil_i[31];
static int16_t s_hil_q[31];
static uint8_t s_hil_pos;

/* Selected demod mode (ra_iq_demod_mode_t).  OFF = the producer touches neither the
 * ring nor the DC blocker.  Written from the control plane, read in the block
 * callback. */
static volatile uint8_t s_demod_mode;   /* RA_IQ_DEMOD_OFF by default */

/* CW BFO NCO.  A 32-bit phase accumulator advanced by s_cw_inc per audio sample;
 * the top 8 bits index a 256-entry Q15 sine table.  s_cw_inc is set from a fixed
 * 700 Hz beat and the audio rate (sample_rate_hz/2) in ra_iq_adc_set_demod.  Owned
 * by the producer (block callback); s_cw_inc is written from the control plane. */
static uint32_t s_cw_phase;
static uint32_t s_cw_inc;

/* Q15 sine table, s_sin256[n] = round(32767 * sin(2*pi*n/256)), n = 0..255.
 * Cosine is s_sin256[(idx + 64) & 255]. */
static const int16_t s_sin256[256] = {
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
};

typedef struct {
    uint32_t audio_underruns;
    uint32_t ring_overruns;
} ra_iq_audio_private_t;

static ra_iq_audio_private_t s_audio;

/* --------------------------------------------------------------------------
 * Spectrum (FFT) path.  The block callback (ISR) only COPIES decimated I/Q into
 * a ping-pong integer accumulator; the float window + CMSIS FFT + magnitude run
 * later inside the Python iq.spectrum() call (control plane, FPU is fine there).
 * Accumulation is gated by s_spec_enable so there is zero ISR cost when the
 * spectrum is not in use (REQ-RT-002/003: no FPU/alloc/Python in the ISR).
 * ------------------------------------------------------------------------ */
#define RA_IQ_SPEC_N 256

static int16_t s_spec_i[2][RA_IQ_SPEC_N];
static int16_t s_spec_q[2][RA_IQ_SPEC_N];
static volatile uint16_t s_spec_wr;         /* producer-owned fill index         */
static volatile uint8_t s_spec_half;        /* producer-owned active half         */
static volatile int8_t s_spec_ready = -1;   /* completed half, -1 = none          */
static volatile uint8_t s_spec_enable;      /* gate: accumulate only when in use  */

/* Control-plane FFT work buffers.  s_spec_fft is interleaved complex (re, im). */
static float s_spec_fft[2 * RA_IQ_SPEC_N];
static float s_spec_win[RA_IQ_SPEC_N];      /* Hann window                        */
static float s_spec_mag[RA_IQ_SPEC_N];      /* |FFT| before fftshift              */
static uint8_t s_spec_win_init;

static elc_event_t ra_iq_agt_event(uint32_t ch) {
    switch (ch) {
        case 0:
            return ELC_EVENT_AGT0_INT;
        case 1:
            return ELC_EVENT_AGT1_INT;
        #if BSP_FEATURE_AGT_MAX_CHANNEL_NUM >= 2
        case 2:
            return ELC_EVENT_AGT2_INT;
        #endif
        #if BSP_FEATURE_AGT_MAX_CHANNEL_NUM >= 3
        case 3:
            return ELC_EVENT_AGT3_INT;
        #endif
        default:
            return ELC_EVENT_NONE;
    }
}

static bool ra_iq_reserve_timer(uint8_t *timer_ch) {
    for (uint32_t ch = 0; ch <= BSP_FEATURE_AGT_MAX_CHANNEL_NUM; ++ch) {
        if (ra_agt_timer_reserve(ch)) {
            *timer_ch = (uint8_t)ch;
            return true;
        }
    }
    return false;
}

/* One event, both units.  This is the whole of ARCH-TRIG-002.  The ELC output
 * slots that feed the ADC units (47.5.2) come from the capability layer; on
 * RA6M3 they resolve to ELSR8 (ELC_AD00) and ELSR10 (ELC_AD10). */
static void ra_iq_elc_enable(elc_event_t event) {
    const ra_sdr_caps_t *caps = ra_sdr_caps_get();
    uint8_t elsr_adc0 = caps->mcu->elc_adc0_slot;
    uint8_t elsr_adc1 = caps->mcu->elc_adc1_slot;
    ra_mstpcrc_start(R_MSTP_MSTPCRC_MSTPC14_Msk);
    R_ELC->ELSR[elsr_adc0].HA = (uint16_t)event;
    R_ELC->ELSR[elsr_adc1].HA = (uint16_t)event;
    FSP_REGISTER_READ(R_ELC->ELSR[elsr_adc1].HA);
    R_ELC->ELCR = R_ELC_ELCR_ELCON_Msk;
    FSP_REGISTER_READ(R_ELC->ELCR);
}

static void ra_iq_elc_disable(void) {
    const ra_sdr_caps_t *caps = ra_sdr_caps_get();
    uint8_t elsr_adc0 = caps->mcu->elc_adc0_slot;
    uint8_t elsr_adc1 = caps->mcu->elc_adc1_slot;
    R_ELC->ELSR[elsr_adc0].HA = 0U;
    R_ELC->ELSR[elsr_adc1].HA = 0U;
    FSP_REGISTER_READ(R_ELC->ELSR[elsr_adc1].HA);
}

/* ADSHCR / ADSHMSR / ADSSTR, identical on both units (ARCH-ADC-002).
 * unit_ch is 0..2 and selects which SHANS bit is raised. */
static void ra_iq_sh_setup(R_ADC0_Type *reg, uint8_t unit_ch) {
    uint16_t adshcr;

    /* SHANS and SHMD may only move while ADST is 0 (47.2.15, 47.2.16). */
    reg->ADCSR_b.ADST = 0U;
    reg->ADSHMSR_b.SHMD = 0U;

    reg->ADSSTR[unit_ch] = RA_IQ_ADSSTR;

    adshcr = reg->ADSHCR;
    adshcr &= (uint16_t) ~(uint16_t)(R_ADC0_ADSHCR_SSTSH_Msk | R_ADC0_ADSHCR_SHANS0_Msk
        | R_ADC0_ADSHCR_SHANS1_Msk | R_ADC0_ADSHCR_SHANS2_Msk);
    adshcr |= (uint16_t)RA_IQ_SSTSH;
    adshcr |= (uint16_t)(1U << (R_ADC0_ADSHCR_SHANS0_Pos + unit_ch));
    reg->ADSHCR = adshcr;

    /* Continuous sampling: the S&H tracks while the converter is idle and holds
     * while it runs, so the aperture is defined by the trigger, not by the scan
     * order.  ARCH-AFE-001 applies from here on: source impedance below 1 kohm
     * and a sampling period of at least 400 ns. */
    reg->ADSHMSR_b.SHMD = 1U;
}

static void ra_iq_sh_teardown(R_ADC0_Type *reg, uint8_t unit_ch) {
    uint16_t adshcr;
    reg->ADCSR_b.ADST = 0U;
    reg->ADSHMSR_b.SHMD = 0U;
    adshcr = reg->ADSHCR;
    adshcr &= (uint16_t) ~(uint16_t)(1U << (R_ADC0_ADSHCR_SHANS0_Pos + unit_ch));
    reg->ADSHCR = adshcr;
}

/* Was there a scan-end from unit 1 during the block just finished?  The IELSR
 * slot for ADC1_SCAN_END exists with its NVIC interrupt disabled, so the IR
 * flag latches without ever reaching the CPU.  This is a liveness check for
 * the Q unit, not per-sample proof: the flag says at least one scan ended. */
static bool ra_iq_unit1_alive_and_clear(void) {
    volatile uint32_t *ielsr = &R_ICU->IELSR[VECTOR_NUMBER_ADC1_SCAN_END];
    bool alive = (*ielsr & R_ICU_IELSR_IR_Msk) != 0U;
    if (alive) {
        *ielsr &= ~(uint32_t)R_ICU_IELSR_IR_Msk;
    }
    return alive;
}

/* Zero the cascade accumulators.  Called on a cutoff change and on a demod
 * (re)selection so a coefficient change starts from silence and does not click.
 * The producer also reads these, but a demod is either OFF (producer idle) or the
 * caller quiesces via the control plane; a transient zero is harmless (REQ-RT). */
static void ra_iq_filt_reset(void) {
    memset(s_filt_i, 0, sizeof(s_filt_i));
    memset(s_filt_q, 0, sizeof(s_filt_q));
}

/* Control-plane: turn a cutoff in Hz into the one-pole Q15 coefficient at the
 * current audio rate (sample_rate_hz/2) and reset the section state.  For fc << fs
 * the one-pole -3 dB point is at alpha = 2*pi*fc/fs; clamped to [1, 32768].
 * hz == 0, hz >= fs/2, or an unknown rate => alpha = 32768 (unity pole = bypass).
 * Integer/float math here is fine: this is not the ISR. */
static void ra_iq_filt_compute_alpha(uint32_t hz) {
    uint32_t fs = s_status.sample_rate_hz >> 1;
    int32_t alpha;

    s_filt_bw_hz = hz;

    if ((hz == 0U) || (fs == 0U) || (hz >= (fs >> 1))) {
        alpha = 32768;   /* bypass */
    } else {
        /* alpha_q15 = round(2*pi*fc/fs * 32768).  Single precision (the port builds
         * with -fsingle-precision-constant); the result is bounded so no overflow. */
        float a = (2.0f * 3.14159265f * (float)hz / (float)fs) * 32768.0f;
        alpha = (int32_t)(a + 0.5f);
        if (alpha < 1) {
            alpha = 1;
        } else if (alpha > 32768) {
            alpha = 32768;
        }
    }

    s_filt_alpha_q15 = alpha;
    ra_iq_filt_reset();
}

/* Half-band FIR x2 decimation.  Causal convolution: output j convolves the newest
 * raw sample at even position 2j back through the RA_IQ_DEC_TAPS most-recent raw
 * samples -- acc = sum_t h[t]*raw[2j - t], t = 0..RA_IQ_DEC_TAPS-1.  This is the
 * linear-phase FIR evaluated at 2j with its fixed (RA_IQ_DEC_TAPS-1)/2 = 5-raw-sample
 * group delay; it never reads a future (not-yet-captured) sample, so the effective
 * index e = 2j - t stays in [-(RA_IQ_DEC_TAPS-1) .. 2(m-1)] = [-10 .. n-2], always in
 * bounds.  raw[] returns a sample by effective index e over the stream
 * [history(RA_IQ_DEC_TAPS-1) ++ current block]: e < 0 reads the history, e >= 0 reads
 * the current block.  Only the five non-zero taps contribute (s_dec_hb has zeros at
 * offsets +/-2, +/-4, +/-5); the loop runs all 11 for clarity and skips the zeros.
 * int32 acc is ample: |raw| <= 4095, sum|h| == 32768, so |acc| < 4095*32768 < 2^28. */
static inline int32_t ra_iq_dec_raw(const uint16_t *blk, const int16_t *hist,
    int32_t e) {
    return (e < 0) ? (int32_t)hist[(RA_IQ_DEC_TAPS - 1) + e] : (int32_t)blk[e];
}

/* Phase-3 block DSP: x2 half-band-FIR decimation + DC removal, in C, no allocation,
 * no Python (REQ-RT-002/003).  Runs on the just-filled half, safe until the block
 * after next.  The former two-sample average (x[2j]+x[2j+1])>>1 is replaced by an
 * 11-tap half-band anti-alias FIR (s_dec_hb) with a cross-block history delay line.
 * DC removal moves AFTER decimation: the mean is computed over the m decimated
 * samples and subtracted, which is equivalent to the old raw-block mean because the
 * FIR is linear and unity-DC-gain, and it keeps the history in the raw domain.
 * Integer only: no FPU state in the ISR.  An odd block_samples drops its last raw
 * sample (m = n>>1, as before). */
static void ra_iq_dsp_process(uint8_t half) {
    const uint16_t *ip = s_i_buf[half];
    const uint16_t *qp = s_q_buf[half];
    uint16_t n = s_status.block_samples;
    uint16_t m = (uint16_t)(n >> 1);

    uint8_t iqc = s_iqc_enable;
    int32_t amp = s_iqc_amp_q15;
    int32_t phase = s_iqc_phase_q15;

    /* Pass 1: decimate raw -> Q0 into s_i_dc/s_q_dc, accumulate the decimated mean. */
    int32_t si = 0;
    int32_t sq = 0;
    for (uint16_t j = 0U; j < m; ++j) {
        int32_t center = (int32_t)(2U * j);
        int32_t acci = 0;
        int32_t accq = 0;
        for (uint8_t t = 0U; t < RA_IQ_DEC_TAPS; ++t) {
            int32_t h = (int32_t)s_dec_hb[t];
            if (h == 0) {
                continue;
            }
            int32_t e = center - (int32_t)t;
            acci += h * ra_iq_dec_raw(ip, s_dec_hist_i, e);
            accq += h * ra_iq_dec_raw(qp, s_dec_hist_q, e);
        }
        int32_t di = acci >> 15;
        int32_t dq = accq >> 15;
        s_i_dc[j] = (int16_t)di;
        s_q_dc[j] = (int16_t)dq;
        si += di;
        sq += dq;
    }
    int32_t mi = (m != 0U) ? (si / (int32_t)m) : 0;
    int32_t mq = (m != 0U) ? (sq / (int32_t)m) : 0;

    /* Carry the last RA_IQ_DEC_TAPS-1 raw samples of this block into the history for
     * the next call, so the FIR support is continuous across the block boundary.
     * Copied from the raw domain (matching ra_iq_dec_raw). */
    if (n >= (RA_IQ_DEC_TAPS - 1U)) {
        for (uint8_t k = 0U; k < (RA_IQ_DEC_TAPS - 1U); ++k) {
            uint16_t src = (uint16_t)(n - (RA_IQ_DEC_TAPS - 1U) + k);
            s_dec_hist_i[k] = (int16_t)ip[src];
            s_dec_hist_q[k] = (int16_t)qp[src];
        }
    }

    /* Pass 2: DC removal on the decimated samples, then Q imbalance correction.
     * Identical downstream math to before, only the mean source changed. */
    for (uint16_t j = 0U; j < m; ++j) {
        int32_t ci = (int32_t)s_i_dc[j] - mi;
        int32_t cq = (int32_t)s_q_dc[j] - mq;
        if (iqc) {
            /* I is the reference; correct Q only.  Q15 fixed point. */
            cq = (amp * cq + phase * ci) >> 15;
            if (cq > 32767) {
                cq = 32767;
            } else if (cq < -32768) {
                cq = -32768;
            }
        }
        s_i_dc[j] = (int16_t)ci;
        s_q_dc[j] = (int16_t)cq;
    }

    /* Channel low-pass filter, in place, AFTER imbalance and BEFORE the demod (and
     * before the spectrum copy, so the analyser shows the channel actually heard).
     * Bypass (alpha == 32768) skips the cascade so s_i_dc/s_q_dc are untouched and
     * the path is bit-identical to no filter.  N one-pole sections per channel,
     * integer only (REQ-RT-002/003). */
    int32_t alpha = s_filt_alpha_q15;
    if (alpha != 32768) {
        for (uint16_t j = 0U; j < m; ++j) {
            int32_t xi = s_i_dc[j];
            int32_t xq = s_q_dc[j];
            for (uint8_t s = 0U; s < RA_IQ_FILT_SECTIONS; ++s) {
                s_filt_i[s] += ((xi - s_filt_i[s]) * alpha) >> 15;
                xi = s_filt_i[s];
                s_filt_q[s] += ((xq - s_filt_q[s]) * alpha) >> 15;
                xq = s_filt_q[s];
            }
            if (xi > 32767) {
                xi = 32767;
            } else if (xi < -32768) {
                xi = -32768;
            }
            if (xq > 32767) {
                xq = 32767;
            } else if (xq < -32768) {
                xq = -32768;
            }
            s_i_dc[j] = (int16_t)xi;
            s_q_dc[j] = (int16_t)xq;
        }
    }

    /* Spectrum accumulator: gated int16 copy of the decimated I/Q into the active
     * ping-pong half.  Integer only, no FPU (REQ-RT-002/003).  When a half fills,
     * publish it as ready and flip to the other half; a not-yet-consumed ready
     * half is simply overwritten by the flip (newest snapshot wins). */
    if (s_spec_enable) {
        uint8_t h = s_spec_half;
        uint16_t wr = s_spec_wr;
        for (uint16_t j = 0U; j < m; ++j) {
            s_spec_i[h][wr] = s_i_dc[j];
            s_spec_q[h][wr] = s_q_dc[j];
            if (++wr == RA_IQ_SPEC_N) {
                s_spec_ready = (int8_t)h;
                h ^= 1U;
                wr = 0U;
            }
        }
        s_spec_half = h;
        s_spec_wr = wr;
    }

    s_dsp.i_mean = (int16_t)mi;
    s_dsp.q_mean = (int16_t)mq;
    s_dsp.dsp_samples = m;
    s_dsp.dsp_blocks++;
}

/* Phase-4 demod producer.  Runs in the block callback (ADC0_SCAN_END IRQ) right
 * after ra_iq_dsp_process, reading the decimated s_i_dc/s_q_dc it just produced.
 * Integer only, no FPU, no allocation, no Python (REQ-RT-002/003).  Dispatches on
 * the selected demod mode; each mode centers its output around mid-scale and pushes
 * to the SPSC ring, where a full ring drops the sample and counts an overrun.
 * AM: envelope = alpha-max-beta-min(|i|,|q|) with an IIR DC blocker.
 * USB/LSB: phasing method.  audio = I_delayed -/+ H(Q), centered at mid-scale with
 * no DC blocker (SSB audio carries no DC term).  USB = I_delayed - H(Q); if a known
 * signal shows the opposite sideband on hardware, the operator can swap USB/LSB. */
static void ra_iq_demod_produce(uint8_t half) {
    uint16_t m = s_dsp.dsp_samples;
    uint32_t head = s_ring_head;

    (void)half;

    switch (s_demod_mode) {
        case RA_IQ_DEMOD_AM:
            for (uint16_t j = 0U; j < m; ++j) {
                int32_t i = s_i_dc[j];
                int32_t q = s_q_dc[j];
                int32_t ai = (i < 0) ? -i : i;
                int32_t aq = (q < 0) ? -q : q;
                int32_t mx = (ai > aq) ? ai : aq;
                int32_t mn = (ai > aq) ? aq : ai;
                int32_t mag = mx + ((3 * mn) >> 3);

                /* Q8 slow LPF: s_env_mean tracks (mag << 8). */
                s_env_mean += (((int32_t)mag << 8) - s_env_mean) >> 8;

                int32_t audio = mag - (s_env_mean >> 8);

                uint16_t dac = ra_iq_audio_stage(audio);
                uint32_t next = (head + 1U) & RA_IQ_AUDIO_RING_MASK;
                if (next == (s_ring_tail & RA_IQ_AUDIO_RING_MASK)) {
                    s_audio.ring_overruns++;
                    continue;
                }
                s_audio_ring[head & RA_IQ_AUDIO_RING_MASK] = dac;
                head = next;
            }
            break;
        case RA_IQ_DEMOD_USB:
        case RA_IQ_DEMOD_LSB: {
            uint8_t is_lsb = (s_demod_mode == (uint8_t)RA_IQ_DEMOD_LSB);
            uint8_t pos = s_hil_pos;
            for (uint16_t j = 0U; j < m; ++j) {
                s_hil_i[pos] = s_i_dc[j];
                s_hil_q[pos] = s_q_dc[j];

                /* Hilbert of Q: hq = sum_n taps[n] * q[pos - n], 31-modulo index
                 * (31 is not a power of two, so wrap with a subtract). */
                int32_t hq = 0;
                for (uint8_t n = 0U; n < 31U; ++n) {
                    if (s_hil_taps[n] == 0) {
                        continue;
                    }
                    int8_t idx = (int8_t)pos - (int8_t)n;
                    if (idx < 0) {
                        idx += 31;
                    }
                    hq += (int32_t)s_hil_taps[n] * (int32_t)s_hil_q[idx];
                }
                hq >>= 15;

                /* I delayed by the group delay (15 taps) to align with H(Q). */
                int8_t di = (int8_t)pos - 15;
                if (di < 0) {
                    di += 31;
                }
                int32_t id = s_hil_i[di];

                int32_t audio = is_lsb ? (id + hq) : (id - hq);

                pos = (uint8_t)((pos + 1U) % 31U);

                uint16_t dac = ra_iq_audio_stage(audio);
                uint32_t next = (head + 1U) & RA_IQ_AUDIO_RING_MASK;
                if (next == (s_ring_tail & RA_IQ_AUDIO_RING_MASK)) {
                    s_audio.ring_overruns++;
                    continue;
                }
                s_audio_ring[head & RA_IQ_AUDIO_RING_MASK] = dac;
                head = next;
            }
            s_hil_pos = pos;
            break;
        }
        case RA_IQ_DEMOD_CW: {
            /* Mix the complex baseband up to a fixed 700 Hz beat and take the real
             * part: audio = Re{(i + jq) * (cos + j sin)} = i*cos - q*sin.  A carrier
             * at 0 Hz baseband becomes an on/off-keyed 700 Hz tone.  No DC blocker:
             * the output is already AC.  The NCO phase advances every sample, even
             * on a ring-full drop, so the tone stays continuous. */
            for (uint16_t j = 0U; j < m; ++j) {
                int32_t i = s_i_dc[j];
                int32_t q = s_q_dc[j];
                uint8_t idx = (uint8_t)(s_cw_phase >> 24);
                int32_t c = s_sin256[(idx + 64U) & 255U];
                int32_t s = s_sin256[idx];
                int32_t audio = ((i * c) - (q * s)) >> 15;
                s_cw_phase += s_cw_inc;

                uint16_t dac = ra_iq_audio_stage(audio);
                uint32_t next = (head + 1U) & RA_IQ_AUDIO_RING_MASK;
                if (next == (s_ring_tail & RA_IQ_AUDIO_RING_MASK)) {
                    s_audio.ring_overruns++;
                    continue;
                }
                s_audio_ring[head & RA_IQ_AUDIO_RING_MASK] = dac;
                head = next;
            }
            break;
        }
        default:
            return;
    }

    /* Publish the sample writes before the head update (SPSC ordering). */
    __DMB();
    s_ring_head = head;
}

/* Block boundary.  Runs from FSP's adc_scan_end_isr, which the DTC lets through
 * only when the chain has filled a whole block.  No allocation, no Python, no
 * I/O here (REQ-RT-002, REQ-RT-003). */
static void ra_iq_block_callback(adc_callback_args_t *p_args) {
    uint8_t finished = s_status.active_half;
    uint8_t next = (uint8_t)(finished ^ 1U);

    (void)p_args;

    /* Point the chain at the other half and reload both counters, then re-arm.
     * In normal mode the DTC has just completed and cleared its own DTCE, so
     * nothing moves again until R_DTC_Reconfigure() re-enables it.  Raw sample
     * count is the correct length for normal mode (repeat/block encoding is not
     * used on this path). */
    s_dtc_info[0].p_dest = s_i_buf[next];
    s_dtc_info[1].p_dest = s_q_buf[next];
    s_dtc_info[0].length = s_status.block_samples;
    s_dtc_info[1].length = s_status.block_samples;
    (void)R_DTC_Reconfigure((transfer_ctrl_t *)&s_iq.dtc_ctrl, s_dtc_info);

    if (s_status.ready) {
        /* The previous block was never taken; it is gone now. */
        s_status.overruns++;
        s_status.last_error = RA_IQ_ADC_ERR_OVERRUN;
    }

    if (!ra_iq_unit1_alive_and_clear()) {
        s_status.unit1_stalls++;
        s_status.last_error = RA_IQ_ADC_ERR_UNIT1_STALL;
    }

    s_iq.ready_half = finished;
    s_status.active_half = next;
    s_status.blocks++;
    s_iq.sequence++;
    s_status.ready = 1U;

    /* Phase-3 DSP on the block just captured (finished half); timed with DWT. */
    uint32_t t0 = DWT->CYCCNT;
    ra_iq_dsp_process(finished);

    /* Phase-4 demod producer, only when a demod mode is selected.  Reads the
     * s_i_dc/s_q_dc the DSP call above just filled. */
    if (s_demod_mode != RA_IQ_DEMOD_OFF) {
        ra_iq_demod_produce(finished);
    }
    uint32_t dt = DWT->CYCCNT - t0;
    s_proc_last = dt;
    if (dt > s_proc_max) {
        s_proc_max = dt;
    }
    s_proc_sum += dt;
    s_proc_count++;
}

static bool ra_iq_adc_open_unit(bool unit1, uint8_t ch) {
    adc_cfg_t *cfg = unit1 ? &s_iq.adc1_cfg : &s_iq.adc0_cfg;
    adc_extended_cfg_t *ext = unit1 ? &s_iq.adc1_ext : &s_iq.adc0_ext;
    adc_channel_cfg_t *ch_cfg = unit1 ? &s_iq.adc1_ch_cfg : &s_iq.adc0_ch_cfg;
    adc_instance_ctrl_t *ctrl = unit1 ? &s_iq.adc1_ctrl : &s_iq.adc0_ctrl;

    /* Copy-and-override: the generated instances carry ADC_TRIGGER_SOFTWARE and
     * must not be edited, they are FSP output (REQ-GIT-007). */
    *cfg = unit1 ? g_adc1_cfg : g_adc0_cfg;
    *ext = *(adc_extended_cfg_t *)(unit1 ? g_adc1_cfg.p_extend : g_adc0_cfg.p_extend);
    *ch_cfg = unit1 ? g_adc1_channel_cfg : g_adc0_channel_cfg;

    cfg->p_extend = ext;
    cfg->mode = ADC_MODE_SINGLE_SCAN;
    cfg->trigger = ADC_TRIGGER_SYNC_ELC;
    cfg->scan_end_b_irq = FSP_INVALID_VECTOR;
    cfg->scan_end_b_ipl = BSP_IRQ_DISABLED;

    if (unit1) {
        /* Diagnostic slot only: latched, never delivered to the CPU. */
        cfg->p_callback = NULL;
        cfg->p_context = NULL;
        cfg->scan_end_irq = VECTOR_NUMBER_ADC1_SCAN_END;
        cfg->scan_end_ipl = BSP_IRQ_DISABLED;
    } else {
        /* The DTC suppresses this interrupt until the chain has filled a block,
         * so it arrives once per block and carries the ping-pong swap. */
        cfg->p_callback = ra_iq_block_callback;
        cfg->p_context = NULL;
        cfg->scan_end_irq = VECTOR_NUMBER_ADC0_SCAN_END;
        cfg->scan_end_ipl = RA_IQ_ADC_IRQ_PRIORITY;
    }

    ch_cfg->scan_mask = (1UL << (ch % RA_IQ_ADC_CHANNELS_PER_UNIT));
    ch_cfg->scan_mask_group_b = 0U;

    if (R_ADC_Open((adc_ctrl_t *)ctrl, cfg) != FSP_SUCCESS) {
        return false;
    }
    if (unit1) {
        R_BSP_IrqDisable(VECTOR_NUMBER_ADC1_SCAN_END);
        R_BSP_IrqStatusClear(VECTOR_NUMBER_ADC1_SCAN_END);
    }
    if (R_ADC_ScanCfg((adc_ctrl_t *)ctrl, ch_cfg) != FSP_SUCCESS) {
        R_ADC_Close((adc_ctrl_t *)ctrl);
        return false;
    }
    return true;
}

static void ra_iq_dtc_build(void) {
    R_ADC0_Type *reg0 = R_ADC0;
    R_ADC0_Type *reg1 = R_ADC1;
    uint8_t i_unit_ch = (uint8_t)(s_iq.i_ch % RA_IQ_ADC_CHANNELS_PER_UNIT);
    uint8_t q_unit_ch = (uint8_t)(s_iq.q_ch % RA_IQ_ADC_CHANNELS_PER_UNIT);

    memset(s_dtc_info, 0, sizeof(s_dtc_info));

    /* Descriptor 0: I, and chain straight into descriptor 1. */
    s_dtc_info[0].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    s_dtc_info[0].transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION;
    s_dtc_info[0].transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    s_dtc_info[0].transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_EACH;
    s_dtc_info[0].transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    s_dtc_info[0].transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
    s_dtc_info[0].transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    s_dtc_info[0].p_src = (void *)&reg0->ADDR[i_unit_ch];
    s_dtc_info[0].p_dest = s_i_buf[0];
    s_dtc_info[0].num_blocks = 0U;
    s_dtc_info[0].length = s_status.block_samples;

    /* Descriptor 1: Q, end of chain, raises the block interrupt. */
    s_dtc_info[1].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    s_dtc_info[1].transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION;
    s_dtc_info[1].transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    s_dtc_info[1].transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    s_dtc_info[1].transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    s_dtc_info[1].transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
    s_dtc_info[1].transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    s_dtc_info[1].p_src = (void *)&reg1->ADDR[q_unit_ch];
    s_dtc_info[1].p_dest = s_q_buf[0];
    s_dtc_info[1].num_blocks = 0U;
    s_dtc_info[1].length = s_status.block_samples;

    s_iq.dtc_ext.activation_source = VECTOR_NUMBER_ADC0_SCAN_END;
    s_iq.dtc_cfg.p_info = s_dtc_info;
    s_iq.dtc_cfg.p_extend = &s_iq.dtc_ext;
}

bool ra_iq_adc_init(uint32_t i_pin, uint32_t q_pin, uint32_t sample_rate_hz,
    size_t block_samples, ra_adc_pga_mode_t pga_mode, uint8_t pga_gain) {
    uint8_t i_ch;
    uint8_t q_ch;
    uint8_t timer_ch;
    elc_event_t agt_event;
    const ra_sdr_caps_t *caps = ra_sdr_caps_get();
    uint8_t channels_per_unit = caps->mcu->adc_channels_per_unit;

    if ((block_samples == 0U) || (block_samples > RA_IQ_ADC_MAX_BLOCK_SAMPLES)
        || (sample_rate_hz == 0U)) {
        return false;
    }
    if (!ra_adc_pin_to_ch(i_pin, &i_ch) || !ra_adc_pin_to_ch(q_pin, &q_ch)) {
        return false;
    }
    /* ARCH-ADC-001 and ARCH-ADC-002: I on unit 0, Q on unit 1, both on channels
     * that carry a dedicated sample-and-hold. */
    if ((i_ch >= channels_per_unit) || (q_ch < channels_per_unit)) {
        return false;
    }
    if (!ra_adc_pga_supported_ch(i_ch) || !ra_adc_pga_supported_ch(q_ch)) {
        return false;
    }
    /* ADC12 does not work on these six channels with ADPGACR at its initial
     * value (Table 47.14), so OFF is not a usable request here. */
    if (pga_mode == RA_ADC_PGA_OFF) {
        return false;
    }
    if (!ra_iq_reserve_timer(&timer_ch)) {
        return false;
    }

    memset(&s_iq, 0, sizeof(s_iq));
    memset(&s_status, 0, sizeof(s_status));
    memset(s_i_buf, 0, sizeof(s_i_buf));
    memset(s_q_buf, 0, sizeof(s_q_buf));

    s_iq.timer_reserved = true;
    s_iq.timer_ch = timer_ch;
    s_iq.i_ch = i_ch;
    s_iq.q_ch = q_ch;

    s_status.i_pin = i_pin;
    s_status.q_pin = q_pin;
    s_status.sample_rate_hz = sample_rate_hz;
    s_status.block_samples = (uint16_t)block_samples;

    agt_event = ra_iq_agt_event(timer_ch);
    if (agt_event == ELC_EVENT_NONE) {
        ra_iq_adc_deinit();
        return false;
    }

    ra_adc_enable(i_pin);
    s_iq.i_pin_enabled = true;
    ra_adc_enable(q_pin);
    s_iq.q_pin_enabled = true;

    if (!ra_iq_adc_open_unit(false, i_ch)) {
        ra_iq_adc_deinit();
        return false;
    }
    s_iq.opened0 = true;
    if (!ra_iq_adc_open_unit(true, q_ch)) {
        ra_iq_adc_deinit();
        return false;
    }
    s_iq.opened1 = true;

    /* The PGA path has to be selected after the units are open, because the
     * module-stop bits must already be clear. */
    if (!ra_adc_pga_config_ch(i_ch, pga_mode, pga_gain)
        || !ra_adc_pga_config_ch(q_ch, pga_mode, pga_gain)) {
        ra_iq_adc_deinit();
        return false;
    }

    ra_iq_sh_setup(R_ADC0, (uint8_t)(i_ch % RA_IQ_ADC_CHANNELS_PER_UNIT));
    ra_iq_sh_setup(R_ADC1, (uint8_t)(q_ch % RA_IQ_ADC_CHANNELS_PER_UNIT));

    ra_iq_dtc_build();
    if (R_DTC_Open((transfer_ctrl_t *)&s_iq.dtc_ctrl, &s_iq.dtc_cfg) != FSP_SUCCESS) {
        ra_iq_adc_deinit();
        return false;
    }
    s_iq.dtc_open = true;

    /* The timer is the ELC source.  Its own interrupt is not wanted: the event
     * goes to the ADC units, not to the CPU. */
    ra_agt_timer_init(s_iq.timer_ch, (float)sample_rate_hz);
    R_BSP_IrqDisable((IRQn_Type)(VECTOR_NUMBER_AGT0_INT + s_iq.timer_ch));
    R_BSP_IrqStatusClear((IRQn_Type)(VECTOR_NUMBER_AGT0_INT + s_iq.timer_ch));

    R_BSP_IrqStatusClear(VECTOR_NUMBER_ADC1_SCAN_END);
    R_BSP_IrqDisable(VECTOR_NUMBER_ADC1_SCAN_END);

    ra_iq_elc_enable(agt_event);
    s_status.initialised = 1U;
    return true;
}

void ra_iq_adc_deinit(void) {
    ra_iq_adc_stop();

    if (s_iq.dtc_open) {
        R_DTC_Disable((transfer_ctrl_t *)&s_iq.dtc_ctrl);
        R_DTC_Close((transfer_ctrl_t *)&s_iq.dtc_ctrl);
    }
    if (s_iq.opened0) {
        ra_iq_sh_teardown(R_ADC0, (uint8_t)(s_iq.i_ch % RA_IQ_ADC_CHANNELS_PER_UNIT));
        ra_adc_pga_config_ch(s_iq.i_ch, RA_ADC_PGA_OFF, 0U);
        R_ADC_Close((adc_ctrl_t *)&s_iq.adc0_ctrl);
    }
    if (s_iq.opened1) {
        ra_iq_sh_teardown(R_ADC1, (uint8_t)(s_iq.q_ch % RA_IQ_ADC_CHANNELS_PER_UNIT));
        ra_adc_pga_config_ch(s_iq.q_ch, RA_ADC_PGA_OFF, 0U);
        R_ADC_Close((adc_ctrl_t *)&s_iq.adc1_ctrl);
    }
    if (s_iq.timer_reserved) {
        ra_agt_timer_deinit(s_iq.timer_ch);
    }
    ra_iq_elc_disable();

    if (s_iq.i_pin_enabled) {
        ra_adc_disable(s_status.i_pin);
    }
    if (s_iq.q_pin_enabled) {
        ra_adc_disable(s_status.q_pin);
    }

    memset(&s_iq, 0, sizeof(s_iq));
    memset(&s_status, 0, sizeof(s_status));
}

bool ra_iq_adc_start(void) {
    if (!s_status.initialised || s_status.running) {
        return false;
    }

    s_status.ready = 0U;
    s_status.active_half = 0U;
    s_status.blocks = 0U;
    s_status.overruns = 0U;
    s_status.unit1_stalls = 0U;
    s_status.last_error = RA_IQ_ADC_ERR_NONE;
    s_iq.sequence = 0U;
    s_iq.ready_half = 0U;
    memset(&s_dsp, 0, sizeof(s_dsp));

    /* Reset the per-block DSP timing and enable the DWT cycle counter. */
    s_proc_last = 0U;
    s_proc_max = 0U;
    s_proc_sum = 0U;
    s_proc_count = 0U;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Fresh decimation-FIR history at capture start; carried across blocks while
     * running.  A zeroed line means the first output block's leading taps see raw 0
     * (a brief startup transient of a few decimated samples), which is harmless. */
    memset(s_dec_hist_i, 0, sizeof(s_dec_hist_i));
    memset(s_dec_hist_q, 0, sizeof(s_dec_hist_q));

    /* One-time ring flush at capture start: the ring must be empty before the
     * producer runs, but must NOT be flushed on a mode switch (see set_demod). */
    s_ring_head = 0U;
    s_ring_tail = 0U;
    __DMB();

    R_DTC_Disable((transfer_ctrl_t *)&s_iq.dtc_ctrl);
    ra_iq_dtc_build();
    R_BSP_IrqStatusClear(VECTOR_NUMBER_ADC0_SCAN_END);
    R_BSP_IrqStatusClear(VECTOR_NUMBER_ADC1_SCAN_END);
    if (R_DTC_Reconfigure((transfer_ctrl_t *)&s_iq.dtc_ctrl, s_dtc_info) != FSP_SUCCESS) {
        return false;
    }

    /* TRGE goes up here; the units then wait for the ELC event.  Both units are
     * armed before the timer runs, so neither can miss the first trigger. */
    if (R_ADC_ScanStart((adc_ctrl_t *)&s_iq.adc1_ctrl) != FSP_SUCCESS) {
        R_DTC_Disable((transfer_ctrl_t *)&s_iq.dtc_ctrl);
        return false;
    }
    if (R_ADC_ScanStart((adc_ctrl_t *)&s_iq.adc0_ctrl) != FSP_SUCCESS) {
        R_ADC_ScanStop((adc_ctrl_t *)&s_iq.adc1_ctrl);
        R_DTC_Disable((transfer_ctrl_t *)&s_iq.dtc_ctrl);
        return false;
    }

    s_status.running = 1U;
    ra_agt_timer_start(s_iq.timer_ch);
    return true;
}

void ra_iq_adc_stop(void) {
    if (!s_status.running) {
        return;
    }
    /* Stop the demod producer first: it runs from this capture's block callback,
     * so it must be quiesced before the capture itself stops.  The DAC stream is
     * owned by machine.DAC and its consumer (ra_iq_adc_audio_pull) tolerates an
     * empty ring, so it is left for machine.DAC to stop. */
    s_demod_mode = RA_IQ_DEMOD_OFF;
    ra_agt_timer_stop(s_iq.timer_ch);
    if (s_iq.opened0) {
        R_ADC_ScanStop((adc_ctrl_t *)&s_iq.adc0_ctrl);
    }
    if (s_iq.opened1) {
        R_ADC_ScanStop((adc_ctrl_t *)&s_iq.adc1_ctrl);
    }
    R_DTC_Disable((transfer_ctrl_t *)&s_iq.dtc_ctrl);
    s_status.running = 0U;
    s_status.ready = 0U;
}

bool ra_iq_adc_acquire(const uint16_t **i_block, const uint16_t **q_block,
    size_t *block_samples, uint32_t *sequence) {
    uint8_t half;
    uint32_t seq;

    R_BSP_IrqDisable(VECTOR_NUMBER_ADC0_SCAN_END);
    if (!s_status.ready) {
        R_BSP_IrqEnable(VECTOR_NUMBER_ADC0_SCAN_END);
        return false;
    }
    half = s_iq.ready_half;
    seq = s_iq.sequence;
    s_status.ready = 0U;
    R_BSP_IrqEnable(VECTOR_NUMBER_ADC0_SCAN_END);

    if (i_block != NULL) {
        *i_block = s_i_buf[half];
    }
    if (q_block != NULL) {
        *q_block = s_q_buf[half];
    }
    if (block_samples != NULL) {
        *block_samples = s_status.block_samples;
    }
    if (sequence != NULL) {
        *sequence = seq;
    }
    return true;
}

void ra_iq_adc_get_status(ra_iq_adc_status_t *status) {
    if (status != NULL) {
        *status = s_status;
    }
}

void ra_iq_adc_get_dsp_status(ra_iq_dsp_status_t *status) {
    if (status != NULL) {
        *status = s_dsp;
    }
}

void ra_iq_adc_get_timing(uint32_t *last_cyc, uint32_t *max_cyc, uint32_t *avg_cyc,
    uint32_t *cpu_hz) {
    if (last_cyc != NULL) {
        *last_cyc = s_proc_last;
    }
    if (max_cyc != NULL) {
        *max_cyc = s_proc_max;
    }
    if (avg_cyc != NULL) {
        *avg_cyc = (s_proc_count != 0U) ? (s_proc_sum / s_proc_count) : 0U;
    }
    if (cpu_hz != NULL) {
        *cpu_hz = SystemCoreClock;
    }
}

void ra_iq_adc_set_demod(uint8_t mode) {
    if (mode > (uint8_t)RA_IQ_DEMOD_CW) {
        mode = (uint8_t)RA_IQ_DEMOD_OFF;
    }

    /* Reset the producer DSP state and audio counters so the new mode starts
     * clean, then publish the mode after those writes (SPSC ordering).  The ring
     * pointers are deliberately NOT touched here: between two active modes the ring
     * must keep flowing (old samples drain, new fill) so the DAC never underruns on
     * a mode switch.  The one-time ring flush lives in ra_iq_adc_start(). */
    s_env_mean = 0;
    memset(s_hil_i, 0, sizeof(s_hil_i));
    memset(s_hil_q, 0, sizeof(s_hil_q));
    s_hil_pos = 0U;
    s_cw_phase = 0U;

    /* Fresh AGC servo per demod selection; the configured mode/target/shifts/gain
     * cap and the master volume persist across mode changes. */
    s_agc_ms = 0;
    s_agc_gain_q15 = RA_IQ_AGC_GAIN_UNITY;
    if ((mode == (uint8_t)RA_IQ_DEMOD_CW) && (s_status.sample_rate_hz != 0U)) {
        uint32_t fs = s_status.sample_rate_hz >> 1;
        s_cw_inc = (fs != 0U) ? (uint32_t)(((uint64_t)700 << 32) / fs) : 0U;
    } else {
        s_cw_inc = 0U;
    }
    s_audio.audio_underruns = 0U;
    s_audio.ring_overruns = 0U;

    /* Per-mode default channel bandwidth.  AM 5 kHz, USB/LSB 3 kHz, CW 1 kHz, OFF
     * bypass.  compute_alpha recomputes the pole at the current audio rate and
     * resets the section state so the change does not click.  The operator can
     * override afterwards with ra_iq_adc_set_bandwidth. */
    uint32_t bw;
    switch (mode) {
        case (uint8_t)RA_IQ_DEMOD_AM:
            bw = 5000U;
            break;
        case (uint8_t)RA_IQ_DEMOD_USB:
        case (uint8_t)RA_IQ_DEMOD_LSB:
            bw = 3000U;
            break;
        case (uint8_t)RA_IQ_DEMOD_CW:
            bw = 1000U;
            break;
        default:
            bw = 0U;   /* OFF: bypass */
            break;
    }
    ra_iq_filt_compute_alpha(bw);

    __DMB();
    s_demod_mode = mode;
}

uint8_t ra_iq_adc_get_demod(void) {
    return s_demod_mode;
}

void ra_iq_adc_get_audio_params(uint32_t *freq_hz, size_t *sample_count) {
    if (freq_hz != NULL) {
        *freq_hz = s_status.sample_rate_hz >> 1;
    }
    if (sample_count != NULL) {
        *sample_count = (size_t)(s_status.block_samples >> 1);
    }
}

size_t ra_iq_adc_audio_pull(uint16_t *buf, size_t n) {
    uint32_t tail = s_ring_tail;
    uint32_t head = s_ring_head;

    for (size_t k = 0U; k < n; ++k) {
        if ((tail & RA_IQ_AUDIO_RING_MASK) == (head & RA_IQ_AUDIO_RING_MASK)) {
            buf[k] = 2048U;
            s_audio.audio_underruns++;
            continue;
        }
        buf[k] = s_audio_ring[tail & RA_IQ_AUDIO_RING_MASK];
        tail = (tail + 1U) & RA_IQ_AUDIO_RING_MASK;
    }

    __DMB();
    s_ring_tail = tail;
    return n;
}

void ra_iq_adc_get_audio_status(ra_iq_audio_status_t *status) {
    if (status != NULL) {
        status->audio_underruns = s_audio.audio_underruns;
        status->ring_overruns = s_audio.ring_overruns;
        status->demod_mode = s_demod_mode;
    }
}

void ra_iq_adc_set_iq_correction(uint8_t enable, int32_t amp_q15, int32_t phase_q15) {
    s_iqc_amp_q15 = amp_q15;
    s_iqc_phase_q15 = phase_q15;
    s_iqc_enable = enable ? 1U : 0U;
}

void ra_iq_adc_get_iq_correction(uint8_t *enable, int32_t *amp_q15, int32_t *phase_q15) {
    if (enable != NULL) {
        *enable = s_iqc_enable;
    }
    if (amp_q15 != NULL) {
        *amp_q15 = s_iqc_amp_q15;
    }
    if (phase_q15 != NULL) {
        *phase_q15 = s_iqc_phase_q15;
    }
}

void ra_iq_adc_set_bandwidth(uint32_t hz) {
    ra_iq_filt_compute_alpha(hz);
}

uint32_t ra_iq_adc_get_bandwidth(void) {
    return s_filt_bw_hz;
}

uint8_t ra_iq_adc_filter_bypassed(void) {
    return (s_filt_alpha_q15 == 32768) ? 1U : 0U;
}

void ra_iq_adc_set_agc(uint8_t mode, int32_t gain_q15, int32_t target) {
    if (mode > RA_IQ_AGC_MODE_MANUAL) {
        mode = RA_IQ_AGC_MODE_OFF;
    }

    /* target <= 0 keeps the current target; clamp so 1/target stays bounded, then
     * precompute the reciprocal the PI servo uses instead of a per-sample divide. */
    if (target > 0) {
        if (target < RA_IQ_AGC_TARGET_MIN) {
            target = RA_IQ_AGC_TARGET_MIN;
        }
        s_agc_target = target;
        s_agc_inv_target = (int32_t)((1 << RA_IQ_AGC_INV_SH) / target);
    }

    switch (mode) {
        case RA_IQ_AGC_MODE_SLOW:
            s_agc_ms_sh = RA_IQ_AGC_SLOW_MS_SH;
            s_agc_att_sh = RA_IQ_AGC_SLOW_ATT_SH;
            s_agc_dec_sh = RA_IQ_AGC_SLOW_DEC_SH;
            s_agc_kp_sh = RA_IQ_AGC_SLOW_KP_SH;
            break;
        case RA_IQ_AGC_MODE_FAST:
            s_agc_ms_sh = RA_IQ_AGC_FAST_MS_SH;
            s_agc_att_sh = RA_IQ_AGC_FAST_ATT_SH;
            s_agc_dec_sh = RA_IQ_AGC_FAST_DEC_SH;
            s_agc_kp_sh = RA_IQ_AGC_FAST_KP_SH;
            break;
        default:
            break;
    }

    if (mode == RA_IQ_AGC_MODE_MANUAL) {
        if (gain_q15 < RA_IQ_AGC_GAIN_MIN) {
            gain_q15 = RA_IQ_AGC_GAIN_MIN;
        }
        s_agc_gain_q15 = gain_q15;
    } else if (mode == RA_IQ_AGC_MODE_OFF) {
        s_agc_gain_q15 = RA_IQ_AGC_GAIN_UNITY;
    } else {
        /* Re-seed the servo so an fast<->slow switch adapts from unity. */
        s_agc_ms = 0;
        s_agc_gain_q15 = RA_IQ_AGC_GAIN_UNITY;
    }

    __DMB();
    s_agc_mode = mode;
}

void ra_iq_adc_get_agc(uint8_t *mode, int32_t *gain_q15, int32_t *target,
    int32_t *env, uint32_t *clips) {
    if (mode != NULL) {
        *mode = s_agc_mode;
    }
    if (gain_q15 != NULL) {
        *gain_q15 = s_agc_gain_q15;
    }
    if (target != NULL) {
        *target = s_agc_target;
    }
    if (env != NULL) {
        *env = s_agc_env;
    }
    if (clips != NULL) {
        *clips = s_agc_clips;
    }
}

void ra_iq_adc_set_volume(int32_t vol_q15) {
    if (vol_q15 < 0) {
        vol_q15 = 0;
    }
    s_vol_q15 = vol_q15;
}

int32_t ra_iq_adc_get_volume(void) {
    return s_vol_q15;
}

/* Enable/disable spectrum accumulation.  On enable, reset the producer indices so
 * the first snapshot after enabling starts on a fresh half boundary.  Control
 * plane; the flag is read by the block callback. */
void ra_iq_adc_spectrum_enable(uint8_t on) {
    if (on) {
        /* Only reset the accumulator on the 0->1 transition.  spectrum() calls this
         * on every invocation; resetting each time would restart the fill and, when
         * polled faster than one FFT frame accumulates, never produce a snapshot. */
        if (!s_spec_enable) {
            s_spec_wr = 0U;
            s_spec_half = 0U;
            s_spec_ready = -1;
            __DMB();
            s_spec_enable = 1U;
        }
    } else {
        s_spec_enable = 0U;
    }
}

size_t ra_iq_adc_spectrum_size(void) {
    return (size_t)RA_IQ_SPEC_N;
}

/* Bin spacing in integer Hz for the UI frequency axis: the spectrum is taken on
 * the decimated complex baseband, whose rate is sample_rate_hz/2, so bin_hz =
 * (sample_rate_hz/2) / N. */
uint32_t ra_iq_adc_spectrum_bin_hz(void) {
    uint32_t audio_rate = s_status.sample_rate_hz >> 1;
    return audio_rate / (uint32_t)RA_IQ_SPEC_N;
}

/* Control-plane FFT.  Consumes the most recent completed accumulator half, applies
 * a Hann window, runs the CMSIS radix complex FFT, takes the magnitude, and writes
 * it fftshift-ed so DC lands at out[N/2].  Returns false when no fresh snapshot is
 * ready.  Not callable from an ISR (uses the FPU). */
bool ra_iq_adc_spectrum(float *out, size_t n) {
    int8_t h = s_spec_ready;
    if (h < 0) {
        return false;
    }
    s_spec_ready = -1;

    if (!s_spec_win_init) {
        for (uint32_t k = 0U; k < (uint32_t)RA_IQ_SPEC_N; ++k) {
            s_spec_win[k] = 0.5f - 0.5f * arm_cos_f32(
                (2.0f * PI * (float)k) / (float)(RA_IQ_SPEC_N - 1));
        }
        s_spec_win_init = 1U;
    }

    for (uint32_t k = 0U; k < (uint32_t)RA_IQ_SPEC_N; ++k) {
        float w = s_spec_win[k];
        s_spec_fft[2U * k] = (float)s_spec_i[h][k] * w;
        s_spec_fft[2U * k + 1U] = (float)s_spec_q[h][k] * w;
    }

    arm_cfft_f32(&arm_cfft_sR_f32_len256, s_spec_fft, 0, 1);
    arm_cmplx_mag_f32(s_spec_fft, s_spec_mag, RA_IQ_SPEC_N);

    size_t count = (n < (size_t)RA_IQ_SPEC_N) ? n : (size_t)RA_IQ_SPEC_N;
    for (size_t i = 0U; i < count; ++i) {
        out[i] = s_spec_mag[(i + (RA_IQ_SPEC_N / 2)) & (RA_IQ_SPEC_N - 1)];
    }
    return true;
}

#endif /* MICROPY_HW_ENABLE_IQ_ADC */
