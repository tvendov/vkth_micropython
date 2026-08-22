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
static volatile uint32_t s_dsp_seq;

/* Streaming I/Q DC removal.  The ADC is unsigned 12-bit around 2048; after the
 * unity-DC-gain half-band decimator, subtract that fixed midpoint and track only
 * the residual channel offset with one persistent sample-by-sample servo per
 * channel.  Q16 state plus alpha=1/4096 leaves four fractional guard bits below
 * the servo step, so a one-count offset still converges (Q12 state would stall).
 * At Fs/2=24 kHz the equivalent high-pass corner is about 0.93 Hz.  State is
 * producer-owned and deliberately survives every ordinary block boundary. */
#define RA_IQ_ADC_MIDPOINT      (2048)
#define RA_IQ_DC_FRAC_BITS      (16U)
#define RA_IQ_DC_SCALE          (65536)
#define RA_IQ_DC_ALPHA_SHIFT    (12U)
static int32_t s_dc_i_q16;
static int32_t s_dc_q_q16;

/* C's signed right-shift is implementation-defined and an arithmetic shift rounds
 * negative values toward -infinity.  The DC servo must be sign-symmetric, so use
 * explicit truncation toward zero for both its Q16 output and alpha update.  The
 * proven ADC/FIR range keeps value far from INT32_MIN, making -value safe here. */
static inline int32_t ra_iq_dc_shift_toward_zero(int32_t value, uint8_t bits) {
    return (value >= 0) ? (value >> bits) : -((-value) >> bits);
}

static inline int32_t ra_iq_dc_round_nearest(int32_t value, uint8_t bits) {
    int32_t half = (int32_t)1 << (bits - 1U);
    return (value >= 0) ? ((value + half) >> bits) : -(((-value) + half) >> bits);
}

/* S-meter: smoothed RMS magnitude of the channel-filtered complex baseband, computed
 * per block in ra_iq_dsp_process.  Independent of the demod mode and the AGC, so it
 * reflects the actual in-channel signal level for a UI / waterfall.  One-pole smoothed
 * over blocks; the control plane turns it into dBFS.  Producer-owned. */
#define RA_IQ_SMETER_SH (2U)
static int32_t s_smeter_rms;

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
 * to the unsigned ADC midpoint at capture start and carried across blocks while
 * running.  Midpoint priming avoids a false start-up impulse from a zero history. */
static int16_t s_dec_hist_i[RA_IQ_DEC_TAPS - 1U];
static int16_t s_dec_hist_q[RA_IQ_DEC_TAPS - 1U];

/* Hybrid CMSIS-DSP stage 1: the same half-band decimation as an arm_fir_decimate_q15
 * kernel, selectable at runtime (iq.dec_kernel) for an A/B timing/behaviour compare
 * against the hand loop above; the hand path stays the default and the fallback.  The
 * coefficients are symmetric so CMSIS's time-reversed ordering is identical to s_dec_hb;
 * with sum(taps) == 32768 (Q15 unity) and |raw| <= 4095 the Q15 output never saturates.
 * State is numTaps + blockSize - 1 q15 words, kept across blocks inside the instance
 * (its own cross-block delay line, so no s_dec_hist needed on this path).  Not ISR-
 * reconfigured: (re)initialised in ra_iq_adc_start and on the control-plane toggle. */
static uint8_t s_dec_use_cmsis;             /* producer-active kernel */
static volatile uint8_t s_dec_requested_cmsis; /* control-plane request */
static arm_fir_decimate_instance_q15 s_dec_cmsis_i;
static arm_fir_decimate_instance_q15 s_dec_cmsis_q;
static q15_t s_dec_state_i[RA_IQ_DEC_TAPS + RA_IQ_ADC_MAX_BLOCK_SAMPLES - 1U];
static q15_t s_dec_state_q[RA_IQ_DEC_TAPS + RA_IQ_ADC_MAX_BLOCK_SAMPLES - 1U];

/* (Re)initialise both CMSIS decimator instances for the current block length.  The
 * first numTaps-1 state entries are the preceding input history, so prime them to
 * the unsigned ADC midpoint just like the hand path instead of inventing a zero-to-
 * 2048 step.  blockSize is the INPUT block (== block_samples), a multiple of M=2.
 * Control-plane only. */
static bool ra_iq_dec_cmsis_init(uint16_t block_samples,
    const int16_t *history_i, const int16_t *history_q) {
    arm_status si = arm_fir_decimate_init_q15(&s_dec_cmsis_i, RA_IQ_DEC_TAPS, 2U,
        s_dec_hb, s_dec_state_i, block_samples);
    arm_status sq = arm_fir_decimate_init_q15(&s_dec_cmsis_q, RA_IQ_DEC_TAPS, 2U,
        s_dec_hb, s_dec_state_q, block_samples);
    if ((si != ARM_MATH_SUCCESS) || (sq != ARM_MATH_SUCCESS)) {
        return false;
    }
    for (uint8_t k = 0U; k < (RA_IQ_DEC_TAPS - 1U); ++k) {
        s_dec_state_i[k] = (q15_t)(history_i != NULL
            ? history_i[k] : RA_IQ_ADC_MIDPOINT);
        s_dec_state_q[k] = (q15_t)(history_q != NULL
            ? history_q[k] : RA_IQ_ADC_MIDPOINT);
    }
    return true;
}

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

/* Hybrid CMSIS-DSP stage 3: the channel low-pass as a 4-pole Butterworth run by
 * arm_biquad_cascade_df1_f32 (two 2nd-order sections), selectable at runtime
 * (iq.chf_kernel).  f32 was chosen over q15 here on purpose: the channel filter is not
 * a timing bottleneck, but it must hold an accurate response across a wide cutoff range
 * (CW ~1 kHz .. AM ~5 kHz), and a Q15 biquad quantises its near-unit-circle feedback
 * coefficients badly at the narrow end (limit cycles / instability).  f32 also removes
 * the integer one-pole's alpha = 2*pi*fc/fs >= 1 saturation at fc >= fs/(2*pi) ~ 3820 Hz,
 * so a real 5 kHz AM skirt becomes possible.  The integer one-pole cascade stays the
 * default and the fallback.  I and Q share one coefficient set; each keeps its own df1
 * state (4 f32 per stage).  Coefficients + bypass are computed in the control plane in
 * ra_iq_chf_compute (called from ra_iq_filt_compute_alpha), never in the ISR. */
#define RA_IQ_CHF_STAGES (2U)
static uint8_t s_chf_use_f32;              /* 0 = integer one-pole cascade (default)     */
static uint8_t s_chf_f32_bypass = 1U;      /* 1 = f32 path passes through unfiltered      */
static float s_chf_coeffs[5U * RA_IQ_CHF_STAGES];   /* {b0,b1,b2,a1,a2} per stage        */
static float s_chf_state_i[4U * RA_IQ_CHF_STAGES];
static float s_chf_state_q[4U * RA_IQ_CHF_STAGES];
static arm_biquad_casd_df1_inst_f32 s_chf_bq_i;
static arm_biquad_casd_df1_inst_f32 s_chf_bq_q;
static float s_chf_buf_i[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U];
static float s_chf_buf_q[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U];

/* Control plane: design the two Butterworth biquad stages for cutoff hz at the audio
 * rate fs and write them into s_chf_coeffs in CMSIS df1 order {b0,b1,b2,a1,a2} (a1,a2
 * are the NEGATED normalised denominator, CMSIS convention).  4th-order Butterworth =
 * two sections with Q = 0.5411961 and 1.3065630.  Sets s_chf_f32_bypass when hz == 0,
 * hz >= fs/2 or the rate is unknown.  Never called from the ISR. */
static void ra_iq_chf_compute(uint32_t hz, uint32_t fs) {
    if ((hz == 0U) || (fs == 0U) || (hz >= (fs >> 1))) {
        s_chf_f32_bypass = 1U;
        return;
    }
    static const float q[RA_IQ_CHF_STAGES] = { 0.5411961f, 1.3065630f };
    float w0 = 2.0f * 3.14159265f * (float)hz / (float)fs;
    float cosw = arm_cos_f32(w0);
    /* w0 is in (0, pi) here (hz < fs/2), so sin(w0) > 0; derive it from cos to avoid
     * pulling in arm_sin_f32 (only arm_cos_f32 is in the DSP build). */
    float sinw = sqrtf(1.0f - (cosw * cosw));
    for (uint8_t s = 0U; s < RA_IQ_CHF_STAGES; ++s) {
        float alpha = sinw / (2.0f * q[s]);
        float a0 = 1.0f + alpha;
        float b0 = (1.0f - cosw) * 0.5f;
        float b1 = 1.0f - cosw;
        float b2 = b0;
        float *c = &s_chf_coeffs[5U * s];
        c[0] = b0 / a0;                    /* b0 */
        c[1] = b1 / a0;                    /* b1 */
        c[2] = b2 / a0;                    /* b2 */
        c[3] = (2.0f * cosw) / a0;         /* a1 = -(-2cosw)/a0 */
        c[4] = -(1.0f - alpha) / a0;       /* a2 = -(1-alpha)/a0 */
    }
    s_chf_f32_bypass = 0U;
}

/* Phase-4 demod audio.  Single-producer (ADC0_SCAN_END block callback) /
 * single-consumer (DAC DMAC fill callback via ra_iq_adc_audio_pull) lock-free ring
 * of DAC codes.  Power of two so count/wrap are masks.  s_ring_head is owned by the
 * producer only, s_ring_tail by the consumer only; each publishes its index after
 * the data write so no critical section is needed (REQ-RT-002).  count = (head -
 * tail) & MASK.  The DAC itself is owned by machine.DAC, not this file. */
/* 512 entries = 8 blocks of buffering at the default 64-sample decimated block, ample
 * for the DMAC refill cadence; halved from 1024 to make room for the parallel scope Q
 * ring (s_scope_ring_q, same size) without growing total SRAM past the RA6M3 budget. */
#define RA_IQ_AUDIO_RING (512U)
#define RA_IQ_AUDIO_RING_MASK (RA_IQ_AUDIO_RING - 1U)

static uint16_t s_audio_ring[RA_IQ_AUDIO_RING];
static volatile uint32_t s_ring_head;   /* producer owns */
static volatile uint32_t s_ring_tail;   /* consumer owns */

/* Per-block scope ROUTING to the DACs.  s_scope_stage selects one DSP block whose
 * output is streamed to the DAC hardware for oscilloscope inspection (0 = off).
 * For a PRE-demod block (1..RA_IQ_LAST_PREDEMOD_BLK) the decimated I is written to
 * s_audio_ring (DAC0) and Q to s_scope_ring_q (DAC1) at the decimated rate, and the
 * demod producer is skipped so it does not double-write s_audio_ring.  For a
 * POST-demod block the mono audio uses s_audio_ring as it does today and the Q ring
 * is left idle (its consumer reads mid-scale).  The Q ring is a second SPSC ring with
 * the identical head/tail discipline as s_audio_ring: producer owns head, consumer
 * (DAC1 fill callback via ra_iq_adc_scope_pull_q) owns tail.  All producer writes
 * happen in the one block-callback ISR, so I->audio_ring and Q->scope_ring_q come
 * from a single producer context. */
static volatile uint8_t s_scope_stage;  /* 0 = off, else RA_IQ_BLK_* */
static uint16_t s_scope_ring_q[RA_IQ_AUDIO_RING];
static volatile uint32_t s_scope_q_head;   /* producer owns */
static volatile uint32_t s_scope_q_tail;   /* consumer owns */

/* Convert a signed baseband/audio sample to a 0..4095 DAC code centred at mid-scale,
 * the same 2048 + clamp mapping the audio tail uses.  Producer-side, integer only. */
static inline uint16_t ra_iq_scope_code(int32_t s) {
    if (s > 2047) {
        s = 2047;
    } else if (s < -2048) {
        s = -2048;
    }
    return (uint16_t)(2048 + s);
}

/* Q8 slow LPF of the envelope, used as an IIR DC blocker so the audio is
 * AC-coupled around mid-scale.  Owned by the producer.  AM-only. */
static int32_t s_env_mean;

/* Hybrid CMSIS-DSP stage 4: the AM envelope as arm_cmplx_mag_f32 (exact sqrt(i^2+q^2))
 * instead of the integer alpha-max-beta-min approximation (which carries a ~4% ripple).
 * Selectable at runtime (iq.mag_kernel); the integer approximation stays default and
 * fallback.  Only the magnitude changes -- the same integer Q8 DC blocker and audio
 * stage run afterwards, so the output scale is unchanged.  s_mag_in is interleaved
 * [i0,q0,i1,q1,...] for the CMSIS call; s_mag_out holds the m magnitudes. */
static uint8_t s_mag_use_f32;    /* 0 = alpha-max-beta-min (default), 1 = arm_cmplx_mag_f32 */
static float s_mag_in[2U * (RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U)];
static float s_mag_out[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U];

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

/* Squelch: mute the audio (and freeze the AGC) while the pre-AGC audio envelope stays
 * below a threshold, so the servo does not ramp gain up and amplify noise on silence.
 * s_sq_thresh == 0 disables it (default).  s_sq_env is a one-pole |audio| envelope; the
 * gate has hysteresis (opens at thresh, closes below 3/4 thresh) to stop chatter.  All
 * producer-owned; threshold is written from the control plane. */
#define RA_IQ_SQ_ENV_SH (5U)                 /* |audio| one-pole smoothing shift */
static volatile int32_t s_sq_thresh;         /* 0 = squelch off                  */
static int32_t s_sq_env;                     /* smoothed |audio| envelope        */
static uint8_t s_sq_open = 1U;               /* 1 = passing audio, 0 = muted     */

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

/* Post-demod AUDIO filter: band-limits the demodulated audio BEFORE the AGC so the
 * servo and the DAC only see in-band energy.  This is the audio-domain complement to
 * the channel filter (which shapes the complex I/Q before demod).  Up to two 2nd-order
 * biquad sections, run per-sample in the block callback as transposed direct-form II
 * (a1,a2 here are the STANDARD +denominator, not the CMSIS df1 negated form):
 *   y  = b0*x + z1;  z1 = b1*x - a1*y + z2;  z2 = b2*x - a2*y.
 * f32 (FPU, lazy-stacked in the ISR like the stage-3 biquad).  Presets, selected per
 * demod or by iq.audio_filter, designed from the audio rate in the control plane:
 *   OFF   - bypass.
 *   AM    - 4th-order Butterworth low-pass at 4.5 kHz (two LP sections).
 *   VOICE - band-pass ~300..2700 Hz for SSB (HP 300 + LP 2700).
 *   CW    - two cascaded band-pass peaks at the 700 Hz BFO, Q=8 (a narrow CW ring). */
/* Per-block ON/OFF for verification: a set bit BYPASSES that block (short to the
 * next), so a stage can be removed from the chain to isolate it.  These definitions
 * must precede ra_iq_audio_stage(), which applies the squelch/AGC/volume bypasses. */
#define RA_IQ_BLK_PGA     1U
#define RA_IQ_BLK_DECIM   2U
#define RA_IQ_BLK_IQCORR  3U
#define RA_IQ_BLK_NCO     4U
#define RA_IQ_BLK_CHFILT  5U
#define RA_IQ_BLK_DEMOD   6U
#define RA_IQ_BLK_AF      7U
#define RA_IQ_BLK_SQUELCH 8U
#define RA_IQ_BLK_AGC     9U
#define RA_IQ_BLK_VOL     10U
#define RA_IQ_BLK_LIMITER 11U
static volatile uint16_t s_block_bypass;
#define RA_IQ_BYPASSED(id) ((s_block_bypass & (1U << (id))) != 0U)

/* Highest block id that is still in the complex-I/Q (pre-demod) domain; ids above
 * it are mono audio (post-demod).  Used by the scope routing to decide whether the
 * selected block feeds I->DAC0 / Q->DAC1 (pre-demod) or mono->DAC0 (post-demod). */
#define RA_IQ_LAST_PREDEMOD_BLK RA_IQ_BLK_CHFILT

/* RA_IQ_AF_OFF/AM/VOICE/CW are defined in ra_iq_adc.h (public preset ids). */
#define RA_IQ_AF_STAGES (2U)

static uint8_t s_af_mode;            /* current RA_IQ_AF_* preset                     */
static uint8_t s_af_active;          /* number of active biquad sections (0 = bypass) */
static float s_af_b0[RA_IQ_AF_STAGES], s_af_b1[RA_IQ_AF_STAGES], s_af_b2[RA_IQ_AF_STAGES];
static float s_af_a1[RA_IQ_AF_STAGES], s_af_a2[RA_IQ_AF_STAGES];
static float s_af_z1[RA_IQ_AF_STAGES], s_af_z2[RA_IQ_AF_STAGES];

/* Biquad kinds for the audio presets. */
#define RA_IQ_AF_LP (0U)
#define RA_IQ_AF_HP (1U)
#define RA_IQ_AF_BP (2U)

/* Control plane: design one RBJ biquad section (kind at f0/Q, audio rate fs) into the
 * stage arrays, normalised by a0, standard (non-negated) a1/a2 for the transposed
 * form.  Not the ISR. */
static void ra_iq_af_design(uint8_t stage, uint8_t kind, float f0, float qf, float fs) {
    float w0 = 2.0f * 3.14159265f * f0 / fs;
    float cosw = arm_cos_f32(w0);
    float sinw = sqrtf(1.0f - (cosw * cosw));   /* w0 in (0, pi) => sin > 0 */
    float alpha = sinw / (2.0f * qf);
    float a0 = 1.0f + alpha;
    float b0, b1, b2;
    if (kind == RA_IQ_AF_HP) {
        b0 = (1.0f + cosw) * 0.5f;
        b1 = -(1.0f + cosw);
        b2 = b0;
    } else if (kind == RA_IQ_AF_BP) {
        b0 = alpha;                 /* constant 0 dB peak-gain band-pass */
        b1 = 0.0f;
        b2 = -alpha;
    } else { /* RA_IQ_AF_LP */
        b0 = (1.0f - cosw) * 0.5f;
        b1 = 1.0f - cosw;
        b2 = b0;
    }
    s_af_b0[stage] = b0 / a0;
    s_af_b1[stage] = b1 / a0;
    s_af_b2[stage] = b2 / a0;
    s_af_a1[stage] = (-2.0f * cosw) / a0;
    s_af_a2[stage] = (1.0f - alpha) / a0;
}

/* Zero the audio-filter delay lines (on a preset or demod change) to avoid a click. */
static void ra_iq_af_reset(void) {
    memset(s_af_z1, 0, sizeof(s_af_z1));
    memset(s_af_z2, 0, sizeof(s_af_z2));
}

/* Per-sample audio filter: run the active biquad sections.  Bypass returns x. */
static inline int32_t ra_iq_audio_filter(int32_t x) {
    if (s_af_active == 0U || RA_IQ_BYPASSED(RA_IQ_BLK_AF)) {
        return x;
    }
    float xf = (float)x;
    for (uint8_t s = 0U; s < s_af_active; ++s) {
        float y = s_af_b0[s] * xf + s_af_z1[s];
        s_af_z1[s] = s_af_b1[s] * xf - s_af_a1[s] * y + s_af_z2[s];
        s_af_z2[s] = s_af_b2[s] * xf - s_af_a2[s] * y;
        xf = y;
    }
    return (int32_t)(xf + (xf >= 0.0f ? 0.5f : -0.5f));
}

/* Shared audio-output tail: AUDIO filter -> AGC servo -> master volume -> peak limiter.
 * Takes the signed audio the demod case produced and returns the clamped DAC code in
 * 0..4095.  One implementation for all demod modes (Step 0 refactor). */
static inline uint16_t ra_iq_audio_stage(int32_t audio) {
    /* Scope routing, post-demod stages.  When s_scope_stage selects a mono block (6..11)
     * this returns the DAC code AT that stage instead of the fully-processed tail, so
     * DAC0 shows exactly the chosen post-demod point (the caller pushes the return value
     * to s_audio_ring, and DAC0's pull carries it).  s_scope is snapshot once so a
     * mid-sample control-plane change cannot pick two different stages.  scope == 0 or a
     * pre-demod id leaves this a single compare, no cost. */
    uint8_t scope = s_scope_stage;
    if (scope == RA_IQ_BLK_DEMOD) {
        return ra_iq_scope_code(audio);
    }

    audio = ra_iq_audio_filter(audio);
    if (scope == RA_IQ_BLK_AF) {
        return ra_iq_scope_code(audio);
    }

    /* Squelch gate on the pre-AGC envelope.  While closed, output mid-scale silence and
     * return early -- which also freezes the AGC servo and its mean-square, so the gain
     * does not ramp up to amplify noise during the quiet.  Hysteresis: open at thresh,
     * close below 3/4 thresh. */
    if ((s_sq_thresh > 0) && !RA_IQ_BYPASSED(RA_IQ_BLK_SQUELCH)) {
        int32_t a = (audio < 0) ? -audio : audio;
        s_sq_env += (a - s_sq_env) >> RA_IQ_SQ_ENV_SH;
        if (s_sq_open) {
            if (s_sq_env < (s_sq_thresh - (s_sq_thresh >> 2))) {
                s_sq_open = 0U;
            }
        } else if (s_sq_env >= s_sq_thresh) {
            s_sq_open = 1U;
        }
        if (!s_sq_open) {
            return 2048U;
        }
    }
    if (scope == RA_IQ_BLK_SQUELCH) {
        return ra_iq_scope_code(audio);
    }

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

    if (!RA_IQ_BYPASSED(RA_IQ_BLK_AGC)) {
        audio = (audio * eff_gain) >> 15;
    }
    if (scope == RA_IQ_BLK_AGC) {
        return ra_iq_scope_code(audio);
    }
    if (!RA_IQ_BYPASSED(RA_IQ_BLK_VOL)) {
        audio = (audio * s_vol_q15) >> 15;
    }
    if (scope == RA_IQ_BLK_VOL) {
        return ra_iq_scope_code(audio);
    }

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

/* Hybrid CMSIS-DSP stage 2: the same Q Hilbert transform as an arm_fir_q15 kernel,
 * selectable at runtime (iq.hil_kernel) for an A/B against the hand loop; the hand
 * path stays the default and fallback.  CMSIS wants the coefficients in time-reversed
 * order; the Type-III transformer is ANTIsymmetric, so the reversed 31-tap array is the
 * element-wise negation of s_hil_taps.
 *
 * arm_fir_q15 REQUIRES numTaps even and >= 4, so the odd 31-tap filter is padded to
 * 32 by PREPENDING one zero coefficient (index 0 of the time-reversed array = the
 * OLDEST tap c[31]).  Prepending (not the doc's trailing zero) keeps the effective
 * group delay at (31-1)/2 = 15 -- a trailing zero would make the newest tap zero and
 * push the delay to 16.  With delay 15 the CMSIS H(Q) lines up with I delayed by 15
 * exactly like the hand path.  State is numTaps + blockSize q15 words when ARM_MATH_DSP
 * is defined (Cortex-M4), carried inside the instance across blocks. */
#define RA_IQ_HIL_TAPS (32U)
static const int16_t s_hil_taps_rev[RA_IQ_HIL_TAPS] = {
    0, /* prepended pad: oldest tap c[31] = 0, keeps group delay at 15 */
    0,      0,      -27,    0,      -146,   0,      -465,   0,
    -1174,  0,      -2628,  0,      -5905,  0,      -20489, 0,
    20489,  0,      5905,   0,      2628,   0,      1174,   0,
    465,    0,      146,    0,      27,     0,      0,
};
#define RA_IQ_HIL_DELAY (15U)
/* CMSIS wins this stage on hardware (~15% faster: 50021 vs 59224 cyc/block, dense
 * 32-tap SIMD FIR beats the hand modulo-31 loop), so it is the default here; the hand
 * loop stays reachable via iq.hil_kernel(False) as the fallback.  The two paths are
 * numerically identical (same taps, same 15-sample delay), so this does not change the
 * SSB output vs the hand path -- the absolute sideband sign is the same operator-
 * swappable convention as before. */
static uint8_t s_hil_use_cmsis = 1U;    /* 1 = CMSIS arm_fir_q15 (default), 0 = hand loop */
static arm_fir_instance_q15 s_hil_cmsis;
static q15_t s_hil_state[RA_IQ_HIL_TAPS + (RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U)];
static int16_t s_hil_hq[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U];
static int16_t s_hil_i_hist[RA_IQ_HIL_DELAY];

/* (Re)initialise the CMSIS Hilbert instance for the current decimated block length m
 * (== block_samples/2), clearing its state and the I delay history.  Control-plane. */
static void ra_iq_hil_cmsis_init(uint16_t m) {
    (void)arm_fir_init_q15(&s_hil_cmsis, RA_IQ_HIL_TAPS, s_hil_taps_rev, s_hil_state, m);
    memset(s_hil_i_hist, 0, sizeof(s_hil_i_hist));
}

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

/* Tuning NCO (digital down-conversion).  Shifts a chosen frequency inside the captured
 * baseband (-fs/2 .. +fs/2, fs = sample_rate_hz/2) down to 0 Hz by a complex multiply,
 * BEFORE the channel filter and demod, so a signal offset from the ADC centre can be
 * tuned in.  Same 32-bit phase / top-8-bits sine-table scheme as the CW BFO.  The mix
 * for a shift of -f_tune is (i + jq)*(cos - j sin):
 *   i' = (i*cos + q*sin) >> 15;  q' = (q*cos - i*sin) >> 15.
 * s_tune_hz == 0 skips the mix entirely (bit-identical to no NCO).  s_tune_step is the
 * per-sample phase increment set from f_tune and fs in the control plane (two's-complement
 * so a negative f_tune decrements the phase).  Producer-owned phase. */
static uint32_t s_tune_phase;
static volatile uint32_t s_tune_step;
static volatile int32_t s_tune_hz;

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

/* Push one routed pre-demod I/Q PAIR into the DAC0 (I) and DAC1 (Q) rings as DAC
 * codes.  SPSC: I and Q must advance together or not at all — if only one head moved
 * when its partner ring was full, the two heads would drift by one sample and the
 * scope constellation would rotate.  So probe BOTH rings first; if either is full,
 * drop the whole pair (count one overrun) and leave both heads unchanged.  Producer-
 * owned heads passed by pointer so the caller batches a whole block, publishes once. */
static inline void ra_iq_scope_push_iq(uint32_t *i_head, uint32_t *q_head,
    int32_t i, int32_t q) {
    uint32_t ni = (*i_head + 1U) & RA_IQ_AUDIO_RING_MASK;
    uint32_t nq = (*q_head + 1U) & RA_IQ_AUDIO_RING_MASK;
    if (ni == (s_ring_tail & RA_IQ_AUDIO_RING_MASK) ||
        nq == (s_scope_q_tail & RA_IQ_AUDIO_RING_MASK)) {
        s_audio.ring_overruns++;
        return;
    }
    s_audio_ring[*i_head & RA_IQ_AUDIO_RING_MASK] = ra_iq_scope_code(i);
    s_scope_ring_q[*q_head & RA_IQ_AUDIO_RING_MASK] = ra_iq_scope_code(q);
    *i_head = ni;
    *q_head = nq;
}

/* --------------------------------------------------------------------------
 * Spectrum (FFT) path.  The block callback (ISR) only COPIES decimated I/Q into
 * a ping-pong integer accumulator; the float window + CMSIS FFT + magnitude run
 * later in the foreground spectrum consumer (control plane, FPU is fine there).
 * Accumulation is gated by s_spec_enable so there is zero ISR cost when the
 * spectrum is not in use (REQ-RT-002/003: no FPU/alloc/Python in the ISR).
 * ------------------------------------------------------------------------ */
#define RA_IQ_SPEC_N RA_IQ_SPECTRUM_N

static int16_t s_spec_i[2][RA_IQ_SPEC_N];
static int16_t s_spec_q[2][RA_IQ_SPEC_N];
static volatile uint16_t s_spec_wr;         /* producer-owned fill index         */
static volatile uint8_t s_spec_half;        /* producer-owned active half         */
static volatile int8_t s_spec_ready = -1;   /* completed half, -1 = none          */
static volatile uint8_t s_spec_enable;      /* gate: accumulate only when in use  */

/* Simultaneous DAC oscilloscope capture.  Unlike the earlier alternative-view
 * prototype this has its own 2x512 signed ping-pong: ADC spectrum and played DAC
 * waveform can now be displayed side by side without two ISR producers sharing
 * indices.  It is static BSS, not MicroPython heap. */
static int16_t s_scope_audio[2][RA_IQ_SPEC_N];
static volatile uint16_t s_scope_wr;
static volatile uint8_t s_scope_half;
static volatile int8_t s_scope_ready = -1;
static volatile uint8_t s_scope_enable;

/* Control-plane FFT work buffers.  s_spec_fft is interleaved complex (re, im). */
static float s_spec_fft[2 * RA_IQ_SPEC_N];
static float s_spec_win[RA_IQ_SPEC_N];      /* Hann window                        */
static float s_spec_mag[RA_IQ_SPEC_N];      /* |FFT| before fftshift              */
static uint8_t s_spec_win_init;

/* Display-domain stabilisation.  The former per-frame peak normalisation made every
 * bar breathe when the largest FFT bin moved by even a small amount.  Keep a slowly
 * released reference peak and Q8 height state instead: a new signal rises quickly,
 * while noise and disappearing signals decay without 3-pixel visual jumps.  The state
 * is producer-independent and is touched only by the foreground spectrum consumer. */
static float s_spec_ref_peak;
static int16_t s_spec_smooth_q8[RA_IQ_SPEC_N];
static size_t s_spec_smooth_n;
static uint8_t s_spec_smooth_valid;

/* Per-stage verification tap.  When s_tap_stage != OFF, ra_iq_dsp_process snapshots the
 * decimated I/Q at that boundary into s_tap_i/q for iq.tap() to read back over UART and
 * compare numerically.  ISR cost is one copy of the decimated block only when the stage
 * matches; disarmed (OFF) it is free.  Control-plane arms/reads it. */
#define RA_IQ_TAP_OFF    0U
#define RA_IQ_TAP_DECIM  1U   /* after x2 decimation + DC removal + I/Q correction */
#define RA_IQ_TAP_NCO    2U   /* after the tuning NCO down-conversion               */
#define RA_IQ_TAP_CHFILT 3U   /* after the channel low-pass filter (pre-demod)      */
static volatile uint8_t s_tap_stage = RA_IQ_TAP_OFF;
static volatile uint8_t s_tap_ready;
static volatile uint16_t s_tap_n;
static int16_t s_tap_i[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2];
static int16_t s_tap_q[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2];

static inline void ra_iq_tap_capture(uint8_t stage, uint16_t m) {
    if (s_tap_stage != stage) {
        return;
    }
    uint16_t nn = (m > (RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U)) ? (RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2U) : m;
    for (uint16_t j = 0U; j < nn; ++j) {
        s_tap_i[j] = s_i_dc[j];
        s_tap_q[j] = s_q_dc[j];
    }
    s_tap_n = nn;
    s_tap_ready = 1U;
}

/* Scope routing capture at a pre-demod boundary.  When s_scope_stage == blk, stream the
 * current decimated s_i_dc/s_q_dc pair-by-pair into the DAC0 (I) and DAC1 (Q) rings as
 * DAC codes and publish both heads once.  Only ONE stage matches per block, so this runs
 * at most once; when s_scope_stage == 0 (or selects another block) it is a single compare
 * and returns.  Zero ISR cost when routing is off. */
static inline void ra_iq_scope_capture_iq(uint8_t blk, uint16_t m) {
    if (s_scope_stage != blk) {
        return;
    }
    uint32_t ih = s_ring_head;
    uint32_t qh = s_scope_q_head;
    for (uint16_t j = 0U; j < m; ++j) {
        ra_iq_scope_push_iq(&ih, &qh, s_i_dc[j], s_q_dc[j]);
    }
    __DMB();
    s_ring_head = ih;
    s_scope_q_head = qh;
}

/* Bench signal injection: a synthetic complex tone written into the raw block in place of
 * the ADC samples, scaled by the CURRENT PGA gain so the whole front end (PGA -> decimate
 * -> IQ corr -> NCO -> channel filter -> demod -> AGC -> volume) is driven from a known,
 * amplitude-controlled input.  Vary the amplitude (or the PGA gain) and watch the AGC /
 * S-meter / stage taps respond.  Control-plane arms it; the ISR just generates the tone. */
static volatile uint8_t s_inject_enable;
static volatile uint8_t s_inject_requested_enable;
static volatile uint32_t s_inject_step;    /* phase increment per raw sample (freq/fs) */
static volatile int32_t s_inject_ampl;     /* base amplitude in ADC counts (pre-PGA)   */
static volatile uint32_t s_inject_requested_step;
static volatile int32_t s_inject_requested_ampl;
static volatile uint32_t s_inject_config_seq; /* even=stable, odd=writer in progress */
static uint32_t s_inject_phase;

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
    /* Also clear the f32 biquad delay lines so a cutoff/demod change on the CMSIS
     * stage-3 path starts from silence and does not click. */
    memset(s_chf_state_i, 0, sizeof(s_chf_state_i));
    memset(s_chf_state_q, 0, sizeof(s_chf_state_q));
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
    /* Design the f32 Butterworth biquads for the same cutoff so iq.chf_kernel can
     * toggle between the integer one-pole cascade and the CMSIS path without a recompute. */
    ra_iq_chf_compute(hz, fs);
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

/* Phase-3 block DSP: x2 half-band-FIR decimation + streaming DC removal, in C,
 * no allocation,
 * no Python (REQ-RT-002/003).  Runs on the just-filled half, safe until the block
 * after next.  The former two-sample average (x[2j]+x[2j+1])>>1 is replaced by an
 * 11-tap half-band anti-alias FIR (s_dec_hb) with a cross-block history delay line.
 * DC removal stays AFTER decimation but is a persistent sample-by-sample servo:
 * block boundaries never create a new signal origin.  The half-band is linear and
 * unity-DC-gain, so its unsigned midpoint remains exactly 2048.
 * Integer only: no FPU state in the ISR.  init enforces an even block size, so
 * m=n/2 is exact for both the hand and CMSIS decimators. */
static void ra_iq_dsp_process(uint8_t half) {
    const uint16_t *ip = s_i_buf[half];
    const uint16_t *qp = s_q_buf[half];
    uint16_t n = s_status.block_samples;
    uint16_t m = (uint16_t)(n >> 1);

    /* Apply the requested decimator on the producer's own block boundary.  This is
     * safer than disabling/re-enabling ADC0 from the control plane: FSP's ordinary
     * IrqEnable clears a pending IRQ and could lose a completed block.  At this point
     * s_dec_hist still contains the previous raw block's final ten samples, exactly
     * the history CMSIS needs before processing the current block. */
    uint8_t requested_dec = s_dec_requested_cmsis;
    if (requested_dec != s_dec_use_cmsis) {
        bool ready = (requested_dec == 0U) ||
            ra_iq_dec_cmsis_init(n, s_dec_hist_i, s_dec_hist_q);
        if (ready) {
            s_dec_use_cmsis = requested_dec;
            s_proc_last = 0U;
            s_proc_max = 0U;
            s_proc_sum = 0U;
            s_proc_count = 0U;
        } else {
            /* Validation makes this unreachable in normal operation.  Publish the
             * still-active kernel back to the control plane if CMSIS rejects it. */
            s_dec_requested_cmsis = s_dec_use_cmsis;
        }
    }

    /* Apply a real ADC<->synthetic source transition at a block boundary.  Reset
     * only the residual DC servo; ordinary blocks, demod/tune changes and live
     * injection frequency/amplitude changes keep the streaming state intact. */
    uint32_t inject_seq0 = s_inject_config_seq;
    if ((inject_seq0 & 1U) == 0U) {
        __DMB();
        uint8_t requested_inject = s_inject_requested_enable;
        uint32_t requested_step = s_inject_requested_step;
        int32_t requested_ampl = s_inject_requested_ampl;
        __DMB();
        if (inject_seq0 == s_inject_config_seq) {
            if (requested_inject != s_inject_enable) {
                s_dc_i_q16 = 0;
                s_dc_q_q16 = 0;
                if (!requested_inject) {
                    s_inject_phase = 0U;
                }
                s_inject_enable = requested_inject;
            }
            s_inject_step = requested_step;
            s_inject_ampl = requested_ampl;
        }
    }

    if (s_inject_enable) {
        /* Overwrite the raw ADC block with a synthetic complex tone scaled by the current
         * PGA gain, so the whole chain runs on a known, amplitude-controlled input. */
        ra_adc_pga_mode_t md;
        uint8_t code = 0U;
        (void)ra_adc_pga_get_ch(s_iq.i_ch, &md, &code);
        int32_t a = RA_IQ_BYPASSED(RA_IQ_BLK_PGA) ? s_inject_ampl
            : (int32_t)(((int64_t)s_inject_ampl *
            (int64_t)ra_adc_pga_gain_milli(md, code)) / 1000);
        uint32_t ph = s_inject_phase;
        uint32_t step = s_inject_step;
        for (uint16_t k = 0U; k < n; ++k) {
            uint8_t idx = (uint8_t)(ph >> 24);
            int32_t c = s_sin256[(idx + 64U) & 255U];   /* cos, Q15 */
            int32_t s = s_sin256[idx];                  /* sin, Q15 */
            int32_t iv = 2048 + (int32_t)(((int64_t)a * c) >> 15);
            int32_t qv = 2048 + (int32_t)(((int64_t)a * s) >> 15);
            if (iv < 0) { iv = 0; } else if (iv > 4095) { iv = 4095; }
            if (qv < 0) { qv = 0; } else if (qv > 4095) { qv = 4095; }
            s_i_buf[half][k] = (uint16_t)iv;
            s_q_buf[half][k] = (uint16_t)qv;
            ph += step;
        }
        s_inject_phase = ph;
    }

    /* Scope routing, PGA stage: the raw front-end block BEFORE decimation.  The scope
     * rings run at the decimated rate, so feed every other raw sample (drop-decimation
     * is enough to watch the front-end waveform).  raw is 0..4095, re-centred to signed
     * by -2048.  Only when RA_IQ_BLK_PGA is the selected stage; the decimated-domain
     * stages are captured further down at their tap boundaries. */
    if (s_scope_stage == RA_IQ_BLK_PGA) {
        uint32_t ih = s_ring_head;
        uint32_t qh = s_scope_q_head;
        for (uint16_t j = 0U; j < m; ++j) {
            ra_iq_scope_push_iq(&ih, &qh,
                (int32_t)ip[2U * j] - 2048, (int32_t)qp[2U * j] - 2048);
        }
        __DMB();
        s_ring_head = ih;
        s_scope_q_head = qh;
    }

    uint8_t iqc = s_iqc_enable;
    int32_t amp = s_iqc_amp_q15;
    int32_t phase = s_iqc_phase_q15;

    /* Pass 1: decimate raw -> Q0 into s_i_dc/s_q_dc.
     * Two interchangeable kernels produce identical output (same symmetric half-band
     * taps, same causal cross-block state); the hand loop is the default/fallback and
     * the CMSIS kernel is the hybrid stage-1 A/B path.  raw is 0..4095 so the uint16
     * block reinterprets as non-negative q15 for the CMSIS call without saturation. */
    if (s_dec_use_cmsis) {
        arm_fir_decimate_q15(&s_dec_cmsis_i, (const q15_t *)ip, s_i_dc, n);
        arm_fir_decimate_q15(&s_dec_cmsis_q, (const q15_t *)qp, s_q_dc, n);
    } else {
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
        }
    }

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

    /* Pass 2: continuous DC removal, then Q imbalance correction.  The output uses
     * the PREVIOUS estimate and only then advances it, giving the standard one-pole
     * high-pass H(z)=(1-z^-1)/(1-(1-alpha)z^-1).  Q16 keeps sub-count estimator
     * precision while every output remains Q0 int16.  No state is reset here. */
    int32_t dc_i = s_dc_i_q16;
    int32_t dc_q = s_dc_q_q16;
    for (uint16_t j = 0U; j < m; ++j) {
        int32_t ui = (int32_t)s_i_dc[j] - RA_IQ_ADC_MIDPOINT;
        int32_t uq = (int32_t)s_q_dc[j] - RA_IQ_ADC_MIDPOINT;
        int32_t target_i = ui * RA_IQ_DC_SCALE;
        int32_t target_q = uq * RA_IQ_DC_SCALE;
        int32_t error_i = target_i - dc_i;
        int32_t error_q = target_q - dc_q;
        int32_t ci = ra_iq_dc_shift_toward_zero(error_i, RA_IQ_DC_FRAC_BITS);
        int32_t cq = ra_iq_dc_shift_toward_zero(error_q, RA_IQ_DC_FRAC_BITS);
        dc_i += ra_iq_dc_shift_toward_zero(error_i, RA_IQ_DC_ALPHA_SHIFT);
        dc_q += ra_iq_dc_shift_toward_zero(error_q, RA_IQ_DC_ALPHA_SHIFT);
        if (iqc && !RA_IQ_BYPASSED(RA_IQ_BLK_IQCORR)) {
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
    s_dc_i_q16 = dc_i;
    s_dc_q_q16 = dc_q;
    ra_iq_tap_capture(RA_IQ_TAP_DECIM, m);
    /* Decim and iqcorr share this boundary: the DC-removed, IQ-corrected decimated
     * pair.  With correction disabled the two are numerically identical, which is the
     * expected scope view (iqcorr routes the same samples decim does). */
    ra_iq_scope_capture_iq(RA_IQ_BLK_DECIM, m);
    ra_iq_scope_capture_iq(RA_IQ_BLK_IQCORR, m);

    /* Wide spectrum tap: copy the DC-removed / IQ-corrected capture BEFORE the tuning
     * NCO and channel filter.  The UI reducer recentres these raw capture bins around
     * s_tune_hz, so fine tuning visibly scrolls the panorama under its fixed centre
     * marker instead of analysing an already-centred, bandwidth-limited channel. */
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

    /* Tuning NCO: complex down-conversion of the decimated I/Q, in place, AFTER the DC
     * removal + imbalance and BEFORE the channel filter, so a signal offset from the ADC
     * centre lands at 0 Hz for the fixed channel filter + demod.  Skipped when tuning is
     * off (bit-identical to no NCO).  Phase is continuous across blocks. */
    if ((s_tune_hz != 0) && !RA_IQ_BYPASSED(RA_IQ_BLK_NCO)) {
        uint32_t ph = s_tune_phase;
        uint32_t step = s_tune_step;
        for (uint16_t j = 0U; j < m; ++j) {
            uint8_t idx = (uint8_t)(ph >> 24);
            int32_t c = s_sin256[(idx + 64U) & 255U];   /* cos */
            int32_t s = s_sin256[idx];                  /* sin */
            int32_t i = s_i_dc[j];
            int32_t q = s_q_dc[j];
            s_i_dc[j] = (int16_t)((i * c + q * s) >> 15);
            s_q_dc[j] = (int16_t)((q * c - i * s) >> 15);
            ph += step;
        }
        s_tune_phase = ph;
    }
    ra_iq_tap_capture(RA_IQ_TAP_NCO, m);
    ra_iq_scope_capture_iq(RA_IQ_BLK_NCO, m);

    /* Channel low-pass filter, in place, AFTER imbalance and BEFORE the demod.  The
     * wide spectrum tap above intentionally precedes this filter.
     * Bypass (alpha == 32768) skips the cascade so s_i_dc/s_q_dc are untouched and
     * the path is bit-identical to no filter.  N one-pole sections per channel,
     * integer only (REQ-RT-002/003). */
    if (s_chf_use_f32) {
        /* CMSIS stage-3 path: 4-pole Butterworth via arm_biquad_cascade_df1_f32, I and Q
         * through their own state with the shared coefficient set.  int16 -> f32 -> int16
         * with rounding and a 16-bit clamp; bypass leaves s_i_dc/s_q_dc untouched. */
        if (!s_chf_f32_bypass && !RA_IQ_BYPASSED(RA_IQ_BLK_CHFILT)) {
            for (uint16_t j = 0U; j < m; ++j) {
                s_chf_buf_i[j] = (float)s_i_dc[j];
                s_chf_buf_q[j] = (float)s_q_dc[j];
            }
            arm_biquad_cascade_df1_f32(&s_chf_bq_i, s_chf_buf_i, s_chf_buf_i, m);
            arm_biquad_cascade_df1_f32(&s_chf_bq_q, s_chf_buf_q, s_chf_buf_q, m);
            for (uint16_t j = 0U; j < m; ++j) {
                float yi = s_chf_buf_i[j];
                float yq = s_chf_buf_q[j];
                int32_t oi = (int32_t)(yi + (yi >= 0.0f ? 0.5f : -0.5f));
                int32_t oq = (int32_t)(yq + (yq >= 0.0f ? 0.5f : -0.5f));
                if (oi > 32767) {
                    oi = 32767;
                } else if (oi < -32768) {
                    oi = -32768;
                }
                if (oq > 32767) {
                    oq = 32767;
                } else if (oq < -32768) {
                    oq = -32768;
                }
                s_i_dc[j] = (int16_t)oi;
                s_q_dc[j] = (int16_t)oq;
            }
        }
    } else {
        int32_t alpha = s_filt_alpha_q15;
        if ((alpha != 32768) && !RA_IQ_BYPASSED(RA_IQ_BLK_CHFILT)) {
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
    }
    ra_iq_tap_capture(RA_IQ_TAP_CHFILT, m);
    ra_iq_scope_capture_iq(RA_IQ_BLK_CHFILT, m);

    /* S-meter: block RMS of the channel-filtered complex baseband, one-pole smoothed
     * over blocks.  |i|,|q| <= 4095 so i*i+q*q <= ~3.4e7 and the int64 sum over up to
     * 128 samples stays well inside range; ra_iq_isqrt32 gives the per-block RMS. */
    if (m != 0U) {
        int64_t pow = 0;
        for (uint16_t j = 0U; j < m; ++j) {
            int32_t ii = s_i_dc[j];
            int32_t qq = s_q_dc[j];
            pow += (int64_t)ii * ii + (int64_t)qq * qq;
        }
        int32_t rms = (int32_t)ra_iq_isqrt32((uint32_t)(pow / (int64_t)m));
        s_smeter_rms += (rms - s_smeter_rms) >> RA_IQ_SMETER_SH;
    }

    /* Publish the four-field status as one coherent snapshot.  The control-plane
     * reader retries if it overlaps this short odd/even sequence. */
    s_dsp_seq++;
    __DMB();
    s_dsp.i_mean = (int16_t)(RA_IQ_ADC_MIDPOINT +
        ra_iq_dc_round_nearest(s_dc_i_q16, RA_IQ_DC_FRAC_BITS));
    s_dsp.q_mean = (int16_t)(RA_IQ_ADC_MIDPOINT +
        ra_iq_dc_round_nearest(s_dc_q_q16, RA_IQ_DC_FRAC_BITS));
    s_dsp.dsp_samples = m;
    s_dsp.dsp_blocks++;
    __DMB();
    s_dsp_seq++;
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
            /* CMSIS stage-4 path: exact magnitude for the whole block in one call, then
             * the same integer DC blocker + audio stage per sample. */
            if (s_mag_use_f32) {
                for (uint16_t j = 0U; j < m; ++j) {
                    s_mag_in[2U * j] = (float)s_i_dc[j];
                    s_mag_in[2U * j + 1U] = (float)s_q_dc[j];
                }
                arm_cmplx_mag_f32(s_mag_in, s_mag_out, m);
            }
            for (uint16_t j = 0U; j < m; ++j) {
                int32_t mag;
                if (s_mag_use_f32) {
                    mag = (int32_t)(s_mag_out[j] + 0.5f);
                } else {
                    int32_t i = s_i_dc[j];
                    int32_t q = s_q_dc[j];
                    int32_t ai = (i < 0) ? -i : i;
                    int32_t aq = (q < 0) ? -q : q;
                    int32_t mx = (ai > aq) ? ai : aq;
                    int32_t mn = (ai > aq) ? aq : ai;
                    mag = mx + ((3 * mn) >> 3);
                }

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
            if (s_hil_use_cmsis) {
                /* CMSIS stage-2 path: H(Q) for the whole block in one arm_fir_q15, then
                 * combine with I delayed by RA_IQ_HIL_DELAY across the s_hil_i_hist line.
                 * hq[j] and the hand loop's hq are the same (same taps, same 15 delay). */
                arm_fir_q15(&s_hil_cmsis, (const q15_t *)s_q_dc, s_hil_hq, m);
                for (uint16_t j = 0U; j < m; ++j) {
                    int32_t e = (int32_t)j - (int32_t)RA_IQ_HIL_DELAY;
                    int32_t id = (e >= 0) ? (int32_t)s_i_dc[e]
                                          : (int32_t)s_hil_i_hist[RA_IQ_HIL_DELAY + e];
                    int32_t hq = (int32_t)s_hil_hq[j];
                    int32_t audio = is_lsb ? (id + hq) : (id - hq);

                    uint16_t dac = ra_iq_audio_stage(audio);
                    uint32_t next = (head + 1U) & RA_IQ_AUDIO_RING_MASK;
                    if (next == (s_ring_tail & RA_IQ_AUDIO_RING_MASK)) {
                        s_audio.ring_overruns++;
                        continue;
                    }
                    s_audio_ring[head & RA_IQ_AUDIO_RING_MASK] = dac;
                    head = next;
                }
                /* Carry the last RA_IQ_HIL_DELAY decimated I samples for the next block. */
                if (m >= RA_IQ_HIL_DELAY) {
                    for (uint8_t k = 0U; k < RA_IQ_HIL_DELAY; ++k) {
                        s_hil_i_hist[k] = s_i_dc[m - RA_IQ_HIL_DELAY + k];
                    }
                }
                break;
            }
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
        case RA_IQ_DEMOD_PASS: {
            /* Verification passthrough: the channel-filtered real part straight into the
             * audio stage (no demod), so the pre-demod baseband can be heard / scoped and
             * the AGC/volume tail is still exercised on a known input. */
            for (uint16_t j = 0U; j < m; ++j) {
                uint16_t dac = ra_iq_audio_stage(s_i_dc[j]);
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
     * s_i_dc/s_q_dc the DSP call above just filled.  Skipped while a PRE-demod stage
     * is routed to the DACs: that scope capture is then the sole producer of the audio
     * ring (I) and the Q ring, and running the demod too would double-write s_audio_ring
     * (two producers of one SPSC ring).  A post-demod scope stage still runs the demod
     * (its output is what feeds the audio ring). */
    uint8_t scope = s_scope_stage;
    bool predemod_routed = (scope != 0U) && (scope <= RA_IQ_LAST_PREDEMOD_BLK);
    if ((s_demod_mode != RA_IQ_DEMOD_OFF) && !predemod_routed) {
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

    if ((block_samples < RA_IQ_ADC_MIN_BLOCK_SAMPLES)
        || (block_samples > RA_IQ_ADC_MAX_BLOCK_SAMPLES)
        || ((block_samples & 1U) != 0U)
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
    memset(&s_dsp, 0, sizeof(s_dsp));
    s_dsp.i_mean = RA_IQ_ADC_MIDPOINT;
    s_dsp.q_mean = RA_IQ_ADC_MIDPOINT;
    s_dsp_seq = 0U;
    s_dc_i_q16 = 0;
    s_dc_q_q16 = 0;
    s_dec_use_cmsis = 0U;
    s_dec_requested_cmsis = 0U;
    s_inject_enable = 0U;
    s_inject_requested_enable = 0U;
    s_inject_step = 0U;
    s_inject_requested_step = 0U;
    s_inject_ampl = 0;
    s_inject_requested_ampl = 0;
    s_inject_config_seq = 0U;
    s_inject_phase = 0U;

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
    s_dsp.i_mean = RA_IQ_ADC_MIDPOINT;
    s_dsp.q_mean = RA_IQ_ADC_MIDPOINT;
    s_dsp_seq = 0U;

    /* Reset the per-block DSP timing and enable the DWT cycle counter. */
    s_proc_last = 0U;
    s_proc_max = 0U;
    s_proc_sum = 0U;
    s_proc_count = 0U;
    s_smeter_rms = 0;
    s_tune_phase = 0U;
    s_dc_i_q16 = 0;
    s_dc_q_q16 = 0;
    s_inject_enable = s_inject_requested_enable;
    s_inject_phase = 0U;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Fresh decimation-FIR history at capture start; carried across blocks while
     * running.  The raw ADC is unsigned around 2048, so prime the delay line to that
     * midpoint rather than synthesising a large zero-to-midpoint startup edge. */
    for (uint8_t k = 0U; k < (RA_IQ_DEC_TAPS - 1U); ++k) {
        s_dec_hist_i[k] = (int16_t)RA_IQ_ADC_MIDPOINT;
        s_dec_hist_q[k] = (int16_t)RA_IQ_ADC_MIDPOINT;
    }
    /* Arm the CMSIS decimator for this block length so iq.dec_kernel can toggle it
     * live.  At a fresh start there is no preceding capture, so both kernels begin
     * from the same unsigned-ADC midpoint history. */
    if (!ra_iq_dec_cmsis_init(s_status.block_samples, NULL, NULL)) {
        return false;
    }
    /* Arm the f32 channel-filter biquads (stage 3).  Coefficients are (re)designed by
     * set_bandwidth/set_demod; init just binds the shared coeff set + per-channel state
     * and clears the delay lines so iq.chf_kernel can toggle the path live. */
    arm_biquad_cascade_df1_init_f32(&s_chf_bq_i, RA_IQ_CHF_STAGES, s_chf_coeffs, s_chf_state_i);
    arm_biquad_cascade_df1_init_f32(&s_chf_bq_q, RA_IQ_CHF_STAGES, s_chf_coeffs, s_chf_state_q);

    /* One-time ring flush at capture start: the ring must be empty before the
     * producer runs, but must NOT be flushed on a mode switch (see set_demod). */
    s_ring_head = 0U;
    s_ring_tail = 0U;
    s_scope_q_head = 0U;
    s_scope_q_tail = 0U;
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
        ra_iq_dsp_status_t snapshot;
        uint32_t before;
        uint32_t after;
        for (;;) {
            before = s_dsp_seq;
            if ((before & 1U) != 0U) {
                continue;
            }
            __DMB();
            snapshot.dsp_blocks = s_dsp.dsp_blocks;
            snapshot.dsp_samples = s_dsp.dsp_samples;
            snapshot.i_mean = s_dsp.i_mean;
            snapshot.q_mean = s_dsp.q_mean;
            __DMB();
            after = s_dsp_seq;
            if ((before == after) && ((after & 1U) == 0U)) {
                break;
            }
        }
        *status = snapshot;
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

/* Select the decimation kernel: 0 = hand integer half-band FIR (default/fallback),
 * 1 = CMSIS arm_fir_decimate_q15.  While running, the control plane publishes only a
 * request; the producer applies it at the next block boundary and seeds CMSIS from
 * the continuously maintained hand history.  No IRQ is disabled and no pending DTC
 * completion can be lost.  Control-plane. */
void ra_iq_adc_set_dec_kernel(uint8_t use_cmsis) {
    uint8_t requested = use_cmsis ? 1U : 0U;
    s_dec_requested_cmsis = requested;
    if (!s_status.running) {
        s_dec_use_cmsis = requested;
        s_proc_last = 0U;
        s_proc_max = 0U;
        s_proc_sum = 0U;
        s_proc_count = 0U;
    }
}

uint8_t ra_iq_adc_get_dec_kernel(void) {
    return s_dec_requested_cmsis;
}

/* Hybrid CMSIS stage 2: select the SSB Hilbert kernel.  0 = hand loop (default,
 * fallback), 1 = CMSIS arm_fir_q15.  Same taps and 15-sample group delay, so the two
 * paths produce equivalent audio (up to the sideband sign, which is operator-swappable
 * either way).  Toggling live re-arms the CMSIS instance and its I delay line and
 * resets iq.timing() so the next read reflects the selected kernel.  Control-plane. */
void ra_iq_adc_set_hil_kernel(uint8_t use_cmsis) {
    s_hil_use_cmsis = use_cmsis ? 1U : 0U;
    if (s_status.running) {
        ra_iq_hil_cmsis_init((uint16_t)(s_status.block_samples >> 1));
        s_proc_last = 0U;
        s_proc_max = 0U;
        s_proc_sum = 0U;
        s_proc_count = 0U;
    }
}

uint8_t ra_iq_adc_get_hil_kernel(void) {
    return s_hil_use_cmsis;
}

/* Hybrid CMSIS stage 3: select the channel-filter kernel.  0 = integer one-pole
 * cascade (default, fallback), 1 = f32 Butterworth arm_biquad_cascade_df1_f32.  The
 * two are NOT bit-identical (a 4-pole Butterworth vs 4 cascaded one-poles), but both
 * are ~24 dB/oct channel low-passes; the f32 path additionally holds up past the
 * integer alpha saturation at ~3820 Hz.  Toggling live re-arms the biquad state and
 * resets iq.timing() so the next read reflects the selected kernel.  Control-plane. */
void ra_iq_adc_set_chf_kernel(uint8_t use_f32) {
    s_chf_use_f32 = use_f32 ? 1U : 0U;
    if (s_status.running) {
        arm_biquad_cascade_df1_init_f32(&s_chf_bq_i, RA_IQ_CHF_STAGES, s_chf_coeffs, s_chf_state_i);
        arm_biquad_cascade_df1_init_f32(&s_chf_bq_q, RA_IQ_CHF_STAGES, s_chf_coeffs, s_chf_state_q);
        ra_iq_filt_reset();
        s_proc_last = 0U;
        s_proc_max = 0U;
        s_proc_sum = 0U;
        s_proc_count = 0U;
    }
}

uint8_t ra_iq_adc_get_chf_kernel(void) {
    return s_chf_use_f32;
}

/* Hybrid CMSIS stage 4: select the AM envelope kernel.  0 = integer alpha-max-beta-min
 * (default, fallback), 1 = f32 arm_cmplx_mag_f32 (exact sqrt(i^2+q^2), removes the ~4%
 * approximation ripple).  Stateless, so a live toggle just resets iq.timing() so the
 * next read reflects the selected kernel.  Control-plane. */
void ra_iq_adc_set_mag_kernel(uint8_t use_f32) {
    s_mag_use_f32 = use_f32 ? 1U : 0U;
    if (s_status.running) {
        s_proc_last = 0U;
        s_proc_max = 0U;
        s_proc_sum = 0U;
        s_proc_count = 0U;
    }
}

uint8_t ra_iq_adc_get_mag_kernel(void) {
    return s_mag_use_f32;
}

/* Select the post-demod audio filter preset (RA_IQ_AF_*).  Designs the sections from
 * the current audio rate and resets the delay lines.  Any unknown value clamps to OFF.
 * set_demod applies a per-mode default; iq.audio_filter overrides it.  Control-plane. */
void ra_iq_adc_set_audio_filter(uint8_t mode) {
    float fs = (float)(s_status.sample_rate_hz >> 1);
    float nyq = 0.45f * fs;   /* keep every design frequency safely below fs/2 */

    if ((fs <= 0.0f) || (mode > RA_IQ_AF_CW)) {
        mode = RA_IQ_AF_OFF;
    }

    switch (mode) {
        case RA_IQ_AF_AM: {
            float f = (4500.0f < nyq) ? 4500.0f : nyq;
            ra_iq_af_design(0U, RA_IQ_AF_LP, f, 0.5411961f, fs);
            ra_iq_af_design(1U, RA_IQ_AF_LP, f, 1.3065630f, fs);
            s_af_active = 2U;
            break;
        }
        case RA_IQ_AF_VOICE: {
            /* SSB voice band-pass ~300..2400 Hz (2.4k, matching the SDR UI FILTERS). */
            float fhi = (2400.0f < nyq) ? 2400.0f : nyq;
            ra_iq_af_design(0U, RA_IQ_AF_HP, 300.0f, 0.7071068f, fs);
            ra_iq_af_design(1U, RA_IQ_AF_LP, fhi, 0.7071068f, fs);
            s_af_active = 2U;
            break;
        }
        case RA_IQ_AF_CW: {
            float f = (700.0f < nyq) ? 700.0f : nyq;
            ra_iq_af_design(0U, RA_IQ_AF_BP, f, 8.0f, fs);
            ra_iq_af_design(1U, RA_IQ_AF_BP, f, 8.0f, fs);
            s_af_active = 2U;
            break;
        }
        default:
            mode = RA_IQ_AF_OFF;
            s_af_active = 0U;
            break;
    }
    s_af_mode = mode;
    ra_iq_af_reset();
}

uint8_t ra_iq_adc_get_audio_filter(void) {
    return s_af_mode;
}

/* Squelch threshold in pre-AGC audio-envelope units; 0 disables (default).  A rough
 * scale: the |audio| envelope of a clean signal sits in the hundreds..~2000, so a few
 * hundred mutes weak noise while passing real signals.  Control-plane. */
void ra_iq_adc_set_squelch(int32_t thresh) {
    if (thresh < 0) {
        thresh = 0;
    }
    s_sq_thresh = thresh;
    /* Re-open on a threshold change so a raised gate does not stick muted until the
     * next loud sample; the envelope logic re-closes within a few samples if needed. */
    s_sq_open = 1U;
}

/* S-meter readout: the smoothed channel RMS and its level in dBFS relative to the
 * ~2047 full-scale of the DC-removed 12-bit baseband.  rms == 0 floors dbfs at -120.
 * Control-plane (uses the FPU for the log). */
void ra_iq_adc_get_smeter(int32_t *rms, float *dbfs) {
    int32_t r = s_smeter_rms;
    if (rms != NULL) {
        *rms = r;
    }
    if (dbfs != NULL) {
        *dbfs = (r > 0) ? (20.0f * log10f((float)r / 2047.0f)) : -120.0f;
    }
}

/* Tune to an offset hz inside the captured baseband (-fs/2 .. +fs/2, fs =
 * sample_rate_hz/2); hz == 0 is centre (no shift, default).  |hz| is clamped just inside
 * fs/2.  Sets the NCO phase step and zeroes the phase so the shift is deterministic.
 * Control-plane. */
void ra_iq_adc_set_tune(int32_t hz) {
    int32_t fs = (int32_t)(s_status.sample_rate_hz >> 1);
    if (fs <= 0) {
        s_tune_hz = 0;
        s_tune_step = 0U;
        s_tune_phase = 0U;
        return;
    }
    int32_t lim = (fs >> 1) - 1;              /* stay just inside +/- Nyquist */
    if (hz > lim) {
        hz = lim;
    } else if (hz < -lim) {
        hz = -lim;
    }
    /* step = round(hz / fs * 2^32), two's-complement for negative hz. */
    int64_t step = (((int64_t)hz << 32) + (int64_t)(hz >= 0 ? fs : -fs) / 2) / (int64_t)fs;
    s_tune_step = (uint32_t)(int32_t)step;
    s_tune_phase = 0U;
    s_tune_hz = hz;
}

int32_t ra_iq_adc_get_tune(void) {
    return s_tune_hz;
}

/* Live PGA (RF front-end) gain, applied to BOTH I and Q channels so they stay matched.
 * gain_code is an RA_ADC_PGA_GAIN_* enum (0x0..0xE = x2.0..x13.3). Only has effect when
 * the unit was created in a PGA mode other than BYPASS -- ra_adc_pga_set_gain_ch returns
 * false otherwise, so this is a safe no-op in bypass. Control-plane only, never the ISR. */
bool ra_iq_adc_set_pga_gain(uint8_t gain_code) {
    return ra_adc_pga_set_gain_ch(s_iq.i_ch, gain_code)
           && ra_adc_pga_set_gain_ch(s_iq.q_ch, gain_code);
}

bool ra_iq_adc_get_pga_gain(uint8_t *gain_code) {
    ra_adc_pga_mode_t mode;
    return ra_adc_pga_get_ch(s_iq.i_ch, &mode, gain_code);
}

/* Arm the per-stage verification tap (0 = off).  Control-plane. */
void ra_iq_adc_set_tap(uint8_t stage) {
    s_tap_stage = stage;
    if (stage == RA_IQ_TAP_OFF) {
        s_tap_ready = 0U;
    }
}

/* Read the last tap snapshot as interleaved [i0,q0,i1,q1,...] into out (cap_pairs = how
 * many i/q pairs fit).  Returns the pair count, or 0 if no fresh snapshot; consumes it. */
uint16_t ra_iq_adc_tap_read(int16_t *out, size_t cap_pairs) {
    if (!s_tap_ready) {
        return 0U;
    }
    uint16_t n = s_tap_n;
    if ((size_t)n > cap_pairs) {
        n = (uint16_t)cap_pairs;
    }
    for (uint16_t j = 0U; j < n; ++j) {
        out[2U * j] = s_tap_i[j];
        out[2U * j + 1U] = s_tap_q[j];
    }
    s_tap_ready = 0U;
    return n;
}

/* Arm/disarm bench injection: a complex tone at freq_hz, base amplitude ampl (ADC counts),
 * scaled by the current PGA gain inside the ISR.  Control-plane. */
void ra_iq_adc_set_inject(uint8_t enable, uint32_t freq_hz, int32_t ampl) {
    enable = enable ? 1U : 0U;
    if (ampl < 0) {
        ampl = 0;
    } else if (ampl > 2047) {
        ampl = 2047;
    }
    uint32_t fs = s_status.sample_rate_hz;
    uint32_t step = (fs != 0U) ?
        (uint32_t)(((uint64_t)freq_hz << 32) / fs) : 0U;

    /* Publish enable/frequency/amplitude as one seqlock transaction.  If the block
     * ISR preempts this writer while the sequence is odd, it keeps the complete old
     * tuple for that block and applies the complete new tuple on the next boundary. */
    uint32_t seq = s_inject_config_seq;
    s_inject_config_seq = seq + 1U;
    __DMB();
    s_inject_requested_enable = enable;
    s_inject_requested_step = step;
    s_inject_requested_ampl = ampl;
    __DMB();
    s_inject_config_seq = seq + 2U;

    if (!s_status.running) {
        s_inject_enable = enable;
        s_inject_step = step;
        s_inject_ampl = ampl;
        s_dc_i_q16 = 0;
        s_dc_q_q16 = 0;
        if (!enable) {
            s_inject_phase = 0U;
        }
    }
}

/* Per-block ON/OFF: enable != 0 keeps block id in the chain, 0 bypasses it (short to the
 * next).  block id is RA_IQ_BLK_*.  Control-plane. */
void ra_iq_adc_set_block(uint8_t block, uint8_t enable) {
    if (block == 0U || block > 15U) {
        return;
    }
    if (enable) {
        s_block_bypass &= (uint16_t) ~(1U << block);
    } else {
        s_block_bypass |= (uint16_t)(1U << block);
    }
}

uint8_t ra_iq_adc_get_block(uint8_t block) {
    return (block == 0U || block > 15U) ? 1U : (RA_IQ_BYPASSED(block) ? 0U : 1U);
}

void ra_iq_adc_get_squelch(int32_t *thresh, uint8_t *open, int32_t *env) {
    if (thresh != NULL) {
        *thresh = s_sq_thresh;
    }
    if (open != NULL) {
        *open = (s_sq_thresh > 0) ? s_sq_open : 1U;
    }
    if (env != NULL) {
        *env = s_sq_env;
    }
}

void ra_iq_adc_set_demod(uint8_t mode) {
    if (mode > (uint8_t)RA_IQ_DEMOD_PASS) {
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
    /* Reset the CMSIS Hilbert instance + its I delay line on the same clean point. */
    ra_iq_hil_cmsis_init((uint16_t)(s_status.block_samples >> 1));
    s_cw_phase = 0U;
    s_sq_env = 0;
    s_sq_open = 1U;

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

    /* Per-mode default channel bandwidth, matching the SDR UI app FILTERS map (AM 6k,
     * USB/LSB 2.4k, CW 500 Hz, OFF bypass).  compute_alpha recomputes the pole at the
     * current audio rate and resets the section state so the change does not click.  The
     * operator can override afterwards with ra_iq_adc_set_bandwidth.  Note the integer
     * one-pole saturates above ~3820 Hz, so AM 6k passes through on that path (bypass=1);
     * iq.chf_kernel(True) selects the f32 biquad that actually realises the wide skirt. */
    uint32_t bw;
    switch (mode) {
        case (uint8_t)RA_IQ_DEMOD_AM:
            bw = 6000U;
            break;
        case (uint8_t)RA_IQ_DEMOD_USB:
        case (uint8_t)RA_IQ_DEMOD_LSB:
            bw = 2400U;
            break;
        case (uint8_t)RA_IQ_DEMOD_CW:
            bw = 500U;
            break;
        default:
            bw = 0U;   /* OFF: bypass */
            break;
    }
    ra_iq_filt_compute_alpha(bw);

    /* Per-mode default post-demod audio filter: AM low-pass, SSB voice band-pass, CW
     * peak, OFF bypass.  The operator can override with ra_iq_adc_set_audio_filter. */
    uint8_t af;
    switch (mode) {
        case (uint8_t)RA_IQ_DEMOD_AM:
            af = RA_IQ_AF_AM;
            break;
        case (uint8_t)RA_IQ_DEMOD_USB:
        case (uint8_t)RA_IQ_DEMOD_LSB:
            af = RA_IQ_AF_VOICE;
            break;
        case (uint8_t)RA_IQ_DEMOD_CW:
            af = RA_IQ_AF_CW;
            break;
        default:
            af = RA_IQ_AF_OFF;
            break;
    }
    ra_iq_adc_set_audio_filter(af);

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

/* Route a DSP block's output to the DACs (0 = off, else RA_IQ_BLK_* 1..LIMITER); an
 * out-of-range id clears routing.  Reset both ring indices so the freshly routed
 * signal starts from an empty ring and does not carry stale audio; DAC0 keeps pulling
 * s_audio_ring and DAC1 pulls s_scope_ring_q, so the change takes effect on the next
 * block.  Control-plane. */
void ra_iq_adc_set_scope(uint8_t stage) {
    if (stage > RA_IQ_BLK_LIMITER) {
        stage = 0U;
    }
    s_ring_head = 0U;
    s_ring_tail = 0U;
    s_scope_q_head = 0U;
    s_scope_q_tail = 0U;
    __DMB();
    s_scope_stage = stage;
}

uint8_t ra_iq_adc_get_scope(void) {
    return s_scope_stage;
}

/* SPSC consumer of the routed Q ring for DAC1.  Mirrors ra_iq_adc_audio_pull but from
 * s_scope_ring_q; mid-scale (2048) on an empty ring so DAC1 never stops.  When no
 * pre-demod stage is routed the producer never fills this ring, so every pull returns
 * mid-scale silence.  No allocation, no Python; callable from the DMAC ISR. */
size_t ra_iq_adc_scope_pull_q(uint16_t *buf, size_t n) {
    uint32_t tail = s_scope_q_tail;
    uint32_t head = s_scope_q_head;

    for (size_t k = 0U; k < n; ++k) {
        if ((tail & RA_IQ_AUDIO_RING_MASK) == (head & RA_IQ_AUDIO_RING_MASK)) {
            buf[k] = 2048U;
            continue;
        }
        buf[k] = s_scope_ring_q[tail & RA_IQ_AUDIO_RING_MASK];
        tail = (tail + 1U) & RA_IQ_AUDIO_RING_MASK;
    }

    __DMB();
    s_scope_q_tail = tail;
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
    /* Report bypass for whichever kernel is active: the integer one-pole bypasses at
     * alpha == unity (which also happens for cutoffs above its ~3820 Hz saturation),
     * the f32 biquad only at hz == 0 / hz >= fs/2. */
    if (s_chf_use_f32) {
        return s_chf_f32_bypass;
    }
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
            s_spec_ref_peak = 0.0f;
            s_spec_smooth_n = 0U;
            s_spec_smooth_valid = 0U;
            memset(s_spec_smooth_q8, 0, sizeof(s_spec_smooth_q8));
            __DMB();
            s_spec_enable = 1U;
        }
    } else {
        uint32_t irq_state = ra_disable_irq();
        s_spec_enable = 0U;
        s_scope_enable = 0U;
        s_spec_wr = 0U;
        s_spec_half = 0U;
        s_spec_ready = -1;
        s_scope_wr = 0U;
        s_scope_half = 0U;
        s_scope_ready = -1;
        ra_enable_irq(irq_state);
        s_spec_ref_peak = 0.0f;
        s_spec_smooth_n = 0U;
        s_spec_smooth_valid = 0U;
    }
}

/* Gate the independent 2x512 DAC capture.  Spectrum accumulation remains active,
 * allowing the two native plots to run simultaneously. */
void ra_iq_adc_scope_enable(uint8_t on) {
    uint32_t irq_state = ra_disable_irq();
    s_scope_enable = on ? 1U : 0U;
    s_scope_wr = 0U;
    s_scope_half = 0U;
    s_scope_ready = -1;
    ra_enable_irq(irq_state);
}

/* Called only by the DAC DMAC refill callback, immediately after audio_pull has
 * written the actual buffer to be played.  Convert 0..4095 DAC codes to signed
 * -2048..2047 samples and append them to the shared capture.  Fixed integer work,
 * no allocation and no Python in the ISR. */
void ra_iq_adc_scope_push(const uint16_t *samples, size_t n) {
    if (!s_spec_enable || !s_scope_enable || (samples == NULL)) {
        return;
    }

    uint8_t h = s_scope_half;
    uint16_t wr = s_scope_wr;
    for (size_t k = 0U; k < n; ++k) {
        s_scope_audio[h][wr] = (int16_t)((int32_t)samples[k] - 2048);
        if (++wr == RA_IQ_SPEC_N) {
            __DMB();
            s_scope_ready = (int8_t)h;
            h ^= 1U;
            wr = 0U;
        }
    }
    s_scope_half = h;
    s_scope_wr = wr;
}

/* Claim the newest completed DAC half.  The renderer reduces it to fixed screen
 * coordinates immediately, before waiting for VSYNC; the producer fills the
 * opposite half during that short foreground read. */
bool ra_iq_adc_scope_frame(const int16_t **samples, size_t *n) {
    if ((samples == NULL) || (n == NULL) || !s_scope_enable) {
        return false;
    }
    uint32_t irq_state = ra_disable_irq();
    int8_t h = s_scope_ready;
    if (h < 0) {
        ra_enable_irq(irq_state);
        return false;
    }
    s_scope_ready = -1;
    ra_enable_irq(irq_state);
    *samples = s_scope_audio[(uint8_t)h];
    *n = (size_t)RA_IQ_SPEC_N;
    return true;
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

    arm_cfft_f32(&arm_cfft_sR_f32_len512, s_spec_fft, 0, 1);
    arm_cmplx_mag_f32(s_spec_fft, s_spec_mag, RA_IQ_SPEC_N);

    size_t count = (n < (size_t)RA_IQ_SPEC_N) ? n : (size_t)RA_IQ_SPEC_N;
    for (size_t i = 0U; i < count; ++i) {
        out[i] = s_spec_mag[(i + (RA_IQ_SPEC_N / 2)) & (RA_IQ_SPEC_N - 1)];
    }
    return true;
}

/* Consume one completed capture and prepare the one shared FFT magnitude buffer.
 * release_alpha is cadence-dependent: 1/64 at 10 Hz and 1/192 at 30 Hz both keep
 * roughly the same six-second reference release. */
static bool ra_iq_adc_spectrum_prepare(float release_alpha, float *ref_peak_out,
    int32_t *shift_bins_out) {
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
    arm_cfft_f32(&arm_cfft_sR_f32_len512, s_spec_fft, 0, 1);
    arm_cmplx_mag_f32(s_spec_fft, s_spec_mag, RA_IQ_SPEC_N);

    /* Global peak for per-frame normalisation. */
    float peak = 1e-6f;
    for (uint32_t k = 0U; k < (uint32_t)RA_IQ_SPEC_N; ++k) {
        if (s_spec_mag[k] > peak) {
            peak = s_spec_mag[k];
        }
    }
    /* Reference peak: follow stronger signals quickly, but release over roughly six
     * seconds at the 10 Hz UI cadence.  Unlike normalising to the instantaneous peak,
     * this does not rescale all 27 bars together on every noisy FFT frame. */
    if (!(s_spec_ref_peak > 0.0f)) {
        s_spec_ref_peak = peak;
    } else if (peak > s_spec_ref_peak) {
        s_spec_ref_peak += (peak - s_spec_ref_peak) * 0.5f;
    } else {
        s_spec_ref_peak += (peak - s_spec_ref_peak) * release_alpha;
    }
    if (s_spec_ref_peak < 1e-6f) {
        s_spec_ref_peak = 1e-6f;
    }

    /* Shift the raw capture panorama so the selected NCO frequency is at the middle.
     * Both native consumers use this exact same tuned-centre mapping. */
    int32_t shift_bins = 0;
    int32_t fs = (int32_t)(s_status.sample_rate_hz >> 1);
    if (fs > 0) {
        int64_t num = (int64_t)s_tune_hz * (int64_t)RA_IQ_SPEC_N;
        num += (num >= 0) ? (int64_t)(fs / 2) : -(int64_t)(fs / 2);
        shift_bins = (int32_t)(num / (int64_t)fs);
    }
    if (ref_peak_out != NULL) {
        *ref_peak_out = s_spec_ref_peak;
    }
    if (shift_bins_out != NULL) {
        *shift_bins_out = shift_bins;
    }
    return true;
}

/* Allocation-free spectrum for the UI: the existing 512-bin magnitude buffer is
 * recentred by the current tuning offset and reduced in C to nbars peak-per-group
 * bars.  Python never boxes a float. */
bool ra_iq_adc_spectrum_bars(int16_t *out, size_t nbars, int16_t max_h) {
    if ((out == NULL) || (nbars == 0U) || (nbars > (size_t)RA_IQ_SPEC_N)) {
        return false;
    }
    float ref_peak;
    int32_t shift_bins;
    if (!ra_iq_adc_spectrum_prepare(1.0f / 64.0f, &ref_peak, &shift_bins)) {
        return false;
    }
    float inv = 1.0f / ref_peak;

    if (s_spec_smooth_n != nbars) {
        s_spec_smooth_n = nbars;
        s_spec_smooth_valid = 0U;
        memset(s_spec_smooth_q8, 0, sizeof(s_spec_smooth_q8));
    }

    /* Peak per bar over the tuned, fftshifted spectrum, dB-scaled to 0..max_h. */
    for (size_t b = 0U; b < nbars; ++b) {
        uint32_t lo = (uint32_t)((b * (size_t)RA_IQ_SPEC_N) / nbars);
        uint32_t hi = (uint32_t)(((b + 1U) * (size_t)RA_IQ_SPEC_N) / nbars);
        float mx = 0.0f;
        for (uint32_t k = lo; k < hi; ++k) {
            uint32_t src = (uint32_t)((int32_t)k + (RA_IQ_SPEC_N / 2) + shift_bins) &
                (RA_IQ_SPEC_N - 1U);
            float v = s_spec_mag[src];
            if (v > mx) {
                mx = v;
            }
        }
        float r = mx * inv;
        float db = (r > 1e-4f) ? (20.0f * log10f(r)) : -80.0f;
        float norm = (db + 50.0f) / 50.0f;
        if (norm < 0.0f) {
            norm = 0.0f;
        } else if (norm > 1.0f) {
            norm = 1.0f;
        }
        int32_t target_q8 = (int32_t)(norm * (float)(max_h * 256) + 0.5f);
        int32_t smooth_q8 = s_spec_smooth_q8[b];
        if (!s_spec_smooth_valid) {
            smooth_q8 = target_q8;
        } else {
            int32_t delta = target_q8 - smooth_q8;
            /* Fast attack (1/2 of the distance per frame), slow decay (1/8).  State
             * remains Q8 so sub-pixel motion accumulates instead of being discarded. */
            smooth_q8 += (delta >= 0) ? (delta / 2) : (delta / 8);
        }
        s_spec_smooth_q8[b] = (int16_t)smooth_q8;
        int32_t height = (smooth_q8 + 128) >> 8;
        if (height < 0) {
            height = 0;
        } else if (height > max_h) {
            height = max_h;
        }
        out[b] = (int16_t)height;
    }
    s_spec_smooth_valid = 1U;
    return true;
}

/* Native waterfall access to the SAME magnitude buffer used by spectrum_bars().
 * The LCD consumes it synchronously before another FFT can overwrite it, so there is
 * no second FFT buffer and no copy through Python/LVGL. */
bool ra_iq_adc_spectrum_frame(const float **magnitudes, float *ref_peak,
    int32_t *shift_bins) {
    if ((magnitudes == NULL) || (ref_peak == NULL) || (shift_bins == NULL)) {
        return false;
    }
    if (!ra_iq_adc_spectrum_prepare(1.0f / 192.0f, ref_peak, shift_bins)) {
        return false;
    }
    *magnitudes = s_spec_mag;
    return true;
}

/* Allocation-free counter snapshot for the UI: fills the caller's int32 buffer with
 * [blocks, overruns, unit1_stalls, audio_underruns, ring_overruns, agc_clips] (up to n).
 * Replaces the dict-returning status()/audio_status()/agc_status() in the poll loop so
 * the UI reads counters without allocating a single MicroPython object.  Returns the
 * number of fields written. */
size_t ra_iq_adc_get_counters(int32_t *out, size_t n) {
    int32_t vals[6];
    vals[0] = (int32_t)s_status.blocks;
    vals[1] = (int32_t)s_status.overruns;
    vals[2] = (int32_t)s_status.unit1_stalls;
    vals[3] = (int32_t)s_audio.audio_underruns;
    vals[4] = (int32_t)s_audio.ring_overruns;
    vals[5] = (int32_t)s_agc_clips;
    size_t count = (n < 6U) ? n : 6U;
    for (size_t i = 0U; i < count; ++i) {
        out[i] = vals[i];
    }
    return count;
}

#endif /* MICROPY_HW_ENABLE_IQ_ADC */
