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
#include "ra_dac.h"
#include "ra_iq_adc.h"
#include "ra_timer.h"
#include "ra_utils.h"
#include "vector_data.h"

#if defined(RA6M3)

/* ELC output slots that feed the ADC units (47.5.2). */
#define RA_IQ_ELSR_ADC0 (8U)
#define RA_IQ_ELSR_ADC1 (10U)

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

/* Phase-4 AM-to-DAC.  Single-producer (ADC0_SCAN_END block callback) /
 * single-consumer (DAC DMAC fill callback) lock-free ring of DAC codes.  Power of
 * two so count/wrap are masks.  s_ring_head is owned by the producer only,
 * s_ring_tail by the consumer only; each publishes its index after the data write
 * so no critical section is needed (REQ-RT-002).  count = (head - tail) & MASK. */
#define RA_IQ_AUDIO_RING (1024U)
#define RA_IQ_AUDIO_RING_MASK (RA_IQ_AUDIO_RING - 1U)

static uint16_t s_audio_ring[RA_IQ_AUDIO_RING];
static volatile uint32_t s_ring_head;   /* producer owns */
static volatile uint32_t s_ring_tail;   /* consumer owns */

/* Q8 slow LPF of the envelope, used as an IIR DC blocker so the audio is
 * AC-coupled around mid-scale.  Owned by the producer. */
static int32_t s_env_mean;

typedef struct {
    uint8_t active;             /* AM->DAC path is running                       */
    uint8_t dac_ch;
    uint32_t dac_pin;
    uint32_t audio_underruns;
    uint32_t ring_overruns;
} ra_iq_am_private_t;

static ra_iq_am_private_t s_audio;

/* Two DAC ping-pong buffers, decimated-block sized.  DMAC clocks these to DADR;
 * they must outlive the transfer, hence static (REQ-RT-004). */
static uint16_t BSP_ALIGN_VARIABLE(4) s_dac_buf_a[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2];
static uint16_t BSP_ALIGN_VARIABLE(4) s_dac_buf_b[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2];

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

/* One event, both units.  This is the whole of ARCH-TRIG-002. */
static void ra_iq_elc_enable(elc_event_t event) {
    ra_mstpcrc_start(R_MSTP_MSTPCRC_MSTPC14_Msk);
    R_ELC->ELSR[RA_IQ_ELSR_ADC0].HA = (uint16_t)event;
    R_ELC->ELSR[RA_IQ_ELSR_ADC1].HA = (uint16_t)event;
    FSP_REGISTER_READ(R_ELC->ELSR[RA_IQ_ELSR_ADC1].HA);
    R_ELC->ELCR = R_ELC_ELCR_ELCON_Msk;
    FSP_REGISTER_READ(R_ELC->ELCR);
}

static void ra_iq_elc_disable(void) {
    R_ELC->ELSR[RA_IQ_ELSR_ADC0].HA = 0U;
    R_ELC->ELSR[RA_IQ_ELSR_ADC1].HA = 0U;
    FSP_REGISTER_READ(R_ELC->ELSR[RA_IQ_ELSR_ADC1].HA);
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

/* Phase-3 block DSP: DC removal + x2 decimation, in C, no allocation, no Python
 * (REQ-RT-002/003).  Runs on the just-filled half, which is safe until the block
 * after next.  centered = (x[2j] + x[2j+1]) / 2 - mean, which equals averaging
 * two mean-removed samples (mean removal is linear).  Integer only: no FPU state
 * in the ISR.  An odd block_samples drops its last raw sample. */
static void ra_iq_dsp_process(uint8_t half) {
    const uint16_t *ip = s_i_buf[half];
    const uint16_t *qp = s_q_buf[half];
    uint16_t n = s_status.block_samples;
    uint16_t m = (uint16_t)(n >> 1);
    uint32_t si = 0U;
    uint32_t sq = 0U;

    for (uint16_t k = 0U; k < n; ++k) {
        si += ip[k];
        sq += qp[k];
    }
    int32_t mi = (n != 0U) ? (int32_t)(si / n) : 0;
    int32_t mq = (n != 0U) ? (int32_t)(sq / n) : 0;

    for (uint16_t j = 0U; j < m; ++j) {
        int32_t ai = ((int32_t)ip[2U * j] + (int32_t)ip[2U * j + 1U]) >> 1;
        int32_t aq = ((int32_t)qp[2U * j] + (int32_t)qp[2U * j + 1U]) >> 1;
        s_i_dc[j] = (int16_t)(ai - mi);
        s_q_dc[j] = (int16_t)(aq - mq);
    }

    s_dsp.i_mean = (int16_t)mi;
    s_dsp.q_mean = (int16_t)mq;
    s_dsp.dsp_samples = m;
    s_dsp.dsp_blocks++;
}

/* Phase-4 AM demod producer.  Runs in the block callback (ADC0_SCAN_END IRQ)
 * right after ra_iq_dsp_process, reading the decimated s_i_dc/s_q_dc it just
 * produced.  Integer only, no FPU, no allocation, no Python (REQ-RT-002/003).
 * Envelope = alpha-max-beta-min(|i|,|q|); an IIR DC blocker centers it at
 * mid-scale so the DAC output is AC-coupled audio.  Pushes to the SPSC ring;
 * a full ring drops the sample and counts an overrun. */
static void ra_iq_am_produce(uint8_t half) {
    uint16_t m = s_dsp.dsp_samples;
    uint32_t head = s_ring_head;

    (void)half;

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

        int32_t audio = 2048 + (mag - (s_env_mean >> 8));
        if (audio < 0) {
            audio = 0;
        } else if (audio > 4095) {
            audio = 4095;
        }

        uint32_t next = (head + 1U) & RA_IQ_AUDIO_RING_MASK;
        if (next == (s_ring_tail & RA_IQ_AUDIO_RING_MASK)) {
            s_audio.ring_overruns++;
            continue;
        }
        s_audio_ring[head & RA_IQ_AUDIO_RING_MASK] = (uint16_t)audio;
        head = next;
    }

    /* Publish the sample writes before the head update (SPSC ordering). */
    __DMB();
    s_ring_head = head;
}

/* DAC ping-pong refill.  Runs in the DMAC ISR.  Pops n samples from the ring
 * into buf; on underrun writes mid-scale silence and counts it, so the stream
 * never stops (see the ra_dac fill_cb contract).  Always returns true.  No
 * allocation, no Python (REQ-RT-002/003). */
static bool ra_iq_dac_fill(void *ctx, uint16_t *buf, size_t n) {
    uint32_t tail = s_ring_tail;
    uint32_t head = s_ring_head;

    (void)ctx;

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
    return true;
}

static void ra_iq_dac_stop_cb(void *ctx) {
    (void)ctx;
    s_audio.active = 0U;
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

    /* Phase-3 DSP on the block just captured (finished half). */
    ra_iq_dsp_process(finished);

    /* Phase-4 AM demod producer, only when the AM->DAC path is running.  Reads
     * the s_i_dc/s_q_dc the DSP call above just filled. */
    if (s_audio.active) {
        ra_iq_am_produce(finished);
    }
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

    if ((block_samples == 0U) || (block_samples > RA_IQ_ADC_MAX_BLOCK_SAMPLES)
        || (sample_rate_hz == 0U)) {
        return false;
    }
    if (!ra_adc_pin_to_ch(i_pin, &i_ch) || !ra_adc_pin_to_ch(q_pin, &q_ch)) {
        return false;
    }
    /* ARCH-ADC-001 and ARCH-ADC-002: I on unit 0, Q on unit 1, both on channels
     * that carry a dedicated sample-and-hold. */
    if ((i_ch >= RA_IQ_ADC_CHANNELS_PER_UNIT) || (q_ch < RA_IQ_ADC_CHANNELS_PER_UNIT)) {
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
    /* Tear the AM->DAC path down first: its producer runs from this capture's
     * block callback and its consumer clocks from a separate DMAC/AGT, both of
     * which must be quiesced before the capture itself stops. */
    ra_iq_adc_am_dac_stop();
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

bool ra_iq_adc_am_dac_start(uint32_t dac_pin, uint8_t dac_ch) {
    if (!s_status.running) {
        return false;
    }
    if (!ra_dac_is_dac_pin(dac_pin)) {
        return false;
    }
    if (s_audio.active) {
        ra_iq_adc_am_dac_stop();
    }

    size_t sample_count = (size_t)(s_status.block_samples >> 1);
    uint32_t freq = s_status.sample_rate_hz >> 1;
    if ((sample_count == 0U) || (freq == 0U)) {
        return false;
    }

    /* Reset the ring, DC blocker and counters before the producer can run. */
    s_ring_head = 0U;
    s_ring_tail = 0U;
    s_env_mean = 0;
    s_audio.dac_ch = dac_ch;
    s_audio.dac_pin = dac_pin;
    s_audio.audio_underruns = 0U;
    s_audio.ring_overruns = 0U;

    for (size_t k = 0U; k < sample_count; ++k) {
        s_dac_buf_a[k] = 2048U;
        s_dac_buf_b[k] = 2048U;
    }

    /* Arm the producer before starting the stream so the ring begins filling. */
    __DMB();
    s_audio.active = 1U;

    ra_dac_init(dac_pin, dac_ch);

    ra_dac_stream_status_t st = ra_dac_write_timed_double_buffered(
        dac_ch, s_dac_buf_a, s_dac_buf_b, true, sample_count, freq,
        ra_iq_dac_fill, ra_iq_dac_stop_cb, NULL, -1);
    if (st != RA_DAC_STREAM_STATUS_OK) {
        s_audio.active = 0U;
        ra_dac_deinit(dac_pin, dac_ch);
        return false;
    }

    return true;
}

void ra_iq_adc_am_dac_stop(void) {
    if (!s_audio.active) {
        return;
    }
    ra_dac_stream_stop(s_audio.dac_ch);
    s_audio.active = 0U;
    ra_dac_deinit(s_audio.dac_pin, s_audio.dac_ch);
}

void ra_iq_adc_get_am_status(ra_iq_am_status_t *status) {
    if (status != NULL) {
        status->audio_underruns = s_audio.audio_underruns;
        status->ring_overruns = s_audio.ring_overruns;
        status->am_active = s_audio.active;
    }
}

#endif /* RA6M3 */
