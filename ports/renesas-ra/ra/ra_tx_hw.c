/*
 * RA6M3 baseband TX: AGT -> ADC0 -> DTC -> DOC -> LUT -> DAC0/1.
 * Legacy CW/AM/raw-FM: no sample callback, buffer refill or CPU NCO.
 * Audio-controlled AM, voice FM and USB/LSB: bounded ADC0 C ISR.
 * Manual R01UH0886EJ0120: sections 18 (DTC), 47 (ADC), 48 (DAC), 52 (DOC).
 */
#include <stddef.h>
#include <string.h>
#include "hal_data.h"
#include "r_adc.h"
#include "r_dtc.h"
#include "r_dmac.h"
#include "ra_adc.h"
#include "ra_dac.h"
#include "ra_gpio.h"
#include "ra_timer.h"
#include "ra_utils.h"
#include "ra_tx_hw.h"
#include "ra_tx_core.h"
#include "ra_tone.h"
#include "vector_data.h"
#if MICROPY_HW_ENABLE_IQ_ADC
#include "ra_iq_adc.h"
#endif
#if MICROPY_HW_ENABLE_AUDIOADC
#include "ra_storm_adc.h"
#endif

#if defined(RA6M3) && MICROPY_HW_ENABLE_TX

_Static_assert(sizeof(transfer_info_t) == 16, "DTC full-address descriptors required");
_Static_assert(offsetof(transfer_info_t, p_src) == 4, "DTC SAR layout");
_Static_assert(offsetof(transfer_info_t, num_blocks) == 12, "DTC CRB layout");
_Static_assert(offsetof(transfer_info_t, length) == 14, "DTC CRA layout");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "DTC byte patch requires LE");

#define TX_ELC_ADC0 (8U)
#define TX_WAIT_US (200U)
#define TX_MAX_TRANSFERS (15U)
#define TX_DAC_DISABLED (0x1fU) /* DACR reserved bits 4:0 must be written 1 */
#define TX_DAC_OUTPUT_MASK (0xc0U) /* RA6M3 DACR.DAOE1:DAOE0, manual 48.2.2 */

_Static_assert(sizeof(ra_tx_ssb_state_t) <= 2U * RA_TX_MAX_RAMP_SAMPLES * sizeof(uint16_t),
    "SSB history must reuse the existing CW workspace without growing it");
_Static_assert(sizeof(ra_tx_fm_state_t) <= 2U * RA_TX_MAX_RAMP_SAMPLES * sizeof(uint16_t),
    "FM state must reuse the existing CW workspace without growing it");

typedef struct {
    bool ready, timer_reserved, adc_open, dtc_open, dac_open, pin_enabled, elc_owned, doc_open;
    uint16_t saved_elc;
    IRQn_Type irq;
    ra_tx_config_t config;
    volatile ra_tx_status_t status;
    uint8_t *lut;
    adc_instance_ctrl_t adc;
    adc_cfg_t adc_cfg;
    adc_extended_cfg_t adc_ext;
    adc_channel_cfg_t adc_channels;
    dtc_instance_ctrl_t dtc;
    dtc_extended_cfg_t dtc_ext;
    transfer_cfg_t dtc_cfg;
    transfer_info_t info[TX_MAX_TRANSFERS];
    volatile uint16_t raw, phase;
    uint16_t bias, hold, lut_base, index, q_plane;
    union {
        struct {
            uint16_t ramp[RA_TX_MAX_RAMP_SAMPLES];
            uint16_t shape[RA_TX_MAX_RAMP_SAMPLES];
        } cw;
        ra_tx_ssb_state_t ssb;
        ra_tx_fm_state_t fm;
    } dsp; /* CW and SSB are exclusive: reuse the existing static RAM. */
    uint32_t hold_settings, hold_source, hold_counts;
    ra_tone_gen_t generator;
} tx_state_t;

static tx_state_t tx;
static uint32_t tx_adc_epoch;

/* Optional passive MIC observer. NORMAL DMA stops after one contiguous frame;
 * no DMA interrupt, no per-sample CPU work, no change to AM's DOC/DTC chain.
 * RX/TX are exclusive and lend the existing 2x512 scope workspace. */
static struct {
    dmac_instance_ctrl_t ctrl;
    dmac_extended_cfg_t ext;
    transfer_cfg_t cfg;
    transfer_info_t info;
    int16_t *half[2];
    size_t n;
    uint8_t writing;
    bool open, requested;
} tx_scope;

static bool tx_scope_close(void) {
    #if MICROPY_HW_ENABLE_IQ_ADC
    if (ra_tx_software_source(&tx.config)) {
        ra_iq_adc_scope_enable(0);
    }
    #endif
    if (tx_scope.open) {
        fsp_err_t err = R_DMAC_Disable((transfer_ctrl_t *)&tx_scope.ctrl);
        if (err != FSP_SUCCESS) {
            tx.status.af_error = err;
            return false;
        }
        unsigned wait;
        for (wait = 0; wait < TX_WAIT_US; ++wait) {
            __DSB();
            if (!tx_scope.ctrl.p_reg->DMSTS_b.ACT && !tx_scope.ctrl.p_reg->DMCNT_b.DTE) {
                break;
            }
            R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MICROSECONDS);
        }
        if (wait == TX_WAIT_US) {
            tx.status.af_error = FSP_ERR_TIMEOUT;
            return false; /* retain owner and workspace until checked teardown */
        }
        err = R_DMAC_Close((transfer_ctrl_t *)&tx_scope.ctrl);
        if (err != FSP_SUCCESS) {
            tx.status.af_error = err;
            return false;
        }
        ra_dmac_release(tx_scope.ext.channel);
    }
    memset(&tx_scope, 0, sizeof(tx_scope));
    tx.status.af_enabled = false;
    return true;
}

bool ra_tx_hw_scope_enable(bool on) {
    #if MICROPY_HW_ENABLE_IQ_ADC
    if (ra_tx_software_source(&tx.config)) {
        bool enable = on && tx.status.running && !tx.status.error;
        ra_iq_adc_scope_enable(enable);
        tx.status.af_enabled = enable;
        return true;
    }
    #endif
    if (!on || !tx.status.running || tx.status.error != RA_TX_ERROR_NONE || !tx.adc_open) {
        return tx_scope_close();
    }
    if (tx_scope.requested) {
        return tx_scope.open && tx.status.af_error == FSP_SUCCESS;
    }
    tx_scope.requested = true;
    tx.status.af_error = FSP_ERR_IN_USE;
    #if MICROPY_HW_ENABLE_IQ_ADC
    if (!ra_iq_adc_scope_workspace(&tx_scope.half[0], &tx_scope.half[1], &tx_scope.n)) {
        return false;
    }
    #else
    return false;
    #endif
    uint32_t ch;
    for (ch = 0; ch < BSP_FEATURE_DMAC_MAX_CHANNEL; ++ch) {
        if (ra_dmac_reserve(ch)) {
            break;
        }
    }
    if (ch == BSP_FEATURE_DMAC_MAX_CHANNEL) {
        return false;
    }
    tx_scope.ext.channel = ch;
    tx_scope.ext.irq = FSP_INVALID_VECTOR;
    tx_scope.ext.activation_source = ELC_EVENT_ADC0_SCAN_END;
    tx_scope.info.transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    tx_scope.info.transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    tx_scope.info.transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
    tx_scope.info.transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    tx_scope.info.p_src = (void *)&R_ADC0->ADDR[1];
    tx_scope.info.p_dest = tx_scope.half[0];
    tx_scope.info.length = tx_scope.n;
    tx_scope.cfg.p_info = &tx_scope.info;
    tx_scope.cfg.p_extend = &tx_scope.ext;
    fsp_err_t err = R_DMAC_Open((transfer_ctrl_t *)&tx_scope.ctrl, &tx_scope.cfg);
    if (err != FSP_SUCCESS) {
        ra_dmac_release(ch);
        tx.status.af_error = err;
        return false;
    }
    tx_scope.open = true;
    err = R_DMAC_Enable((transfer_ctrl_t *)&tx_scope.ctrl);
    tx.status.af_error = err;
    tx.status.af_enabled = err == FSP_SUCCESS;
    return err == FSP_SUCCESS;
}

bool ra_tx_hw_scope_frame(const int16_t **samples, size_t *n, uint32_t *rate_hz) {
    #if MICROPY_HW_ENABLE_IQ_ADC
    if (samples && n && rate_hz && ra_tx_software_source(&tx.config) && tx.status.running &&
        !tx.status.error && tx.status.af_enabled) {
        *rate_hz = (tx.status.timer_clock_hz + tx.status.timer_period / 2U) / tx.status.timer_period;
        bool ready = ra_iq_adc_scope_frame(samples, n);
        if (ready) {
            tx.status.af_frames++;
        }
        return ready;
    }
    #endif
    if (!samples || !n || !rate_hz || !tx.status.running ||
        tx.status.error != RA_TX_ERROR_NONE || !tx_scope.open ||
        tx.status.af_error != FSP_SUCCESS || tx_scope.ctrl.p_reg->DMCNT_b.DTE ||
        tx_scope.ctrl.p_reg->DMSTS_b.ACT || tx_scope.ctrl.p_reg->DMCRA_b.DMCRAL) {
        return false;
    }
    __DMB();
    uint8_t completed = tx_scope.writing;
    tx_scope.writing ^= 1U;
    /* The completed half cannot be overwritten by hardware: the next capture
     * is one-shot into the OTHER half. Only a subsequent foreground claim can
     * re-arm this half, so VSYNC waits do not endanger borrowed samples. */
    fsp_err_t err = R_DMAC_Reset((transfer_ctrl_t *)&tx_scope.ctrl,
        (void *)&R_ADC0->ADDR[1], tx_scope.half[tx_scope.writing], tx_scope.n);
    if (err != FSP_SUCCESS) {
        tx.status.af_error = err;
        tx.status.af_enabled = false;
        return false;
    }
    int16_t *frame = tx_scope.half[completed];
    for (size_t k = 0; k < tx_scope.n; ++k) {
        frame[k] = (int16_t)((int32_t)(uint16_t)frame[k] - tx.config.adc_mid);
    }
    *samples = frame;
    *n = tx_scope.n;
    *rate_hz = tx.status.timer_period ?
        (tx.status.timer_clock_hz + tx.status.timer_period / 2U) / tx.status.timer_period : 0;
    tx.status.af_frames++;
    return true;
}

/* A cutoff is not proof of quiescence and cannot replace an external RF mute.
 * Keep ownership and running/quiesced diagnostics until checked stop succeeds. */
static void tx_cutoff(void) {
    if (tx.timer_reserved) {
        ra_agt_timer_stop((uint32_t)tx.status.timer_channel);
    }
    if (tx.adc_open || tx.dtc_open || ra_tx_software_source(&tx.config)) {
        R_BSP_IrqDisable(tx.irq);
    }
    if (tx.dtc_open) {
        R_ICU->IELSR_b[tx.irq].DTCE = 0;
    }
    if (tx.dac_open) {
        R_DAC->DACR = TX_DAC_DISABLED;
    }
    tx.status.keyed = false;
    __DSB();
}

static bool tx_error(ra_tx_error_t error, fsp_err_t fsp_error) {
    tx.status.error = error;
    tx.status.fsp_error = (int32_t)fsp_error;
    if (tx.status.owned && error != RA_TX_ERROR_BUSY) {
        tx_cutoff();
    }
    return false;
}

static void tx_neutral(void) {
    if (tx.dac_open) {
        R_DAC->DADR[0] = tx.config.i_zero;
        R_DAC->DADR[1] = tx.config.q_zero;
    }
}

/* Fault-only callback: any CPU delivery is unexpected, never a sample service.
 * Do not close/free in an ISR. stop()/deinit() perform checked quiescence. */
static void tx_unexpected_irq(void *unused) {
    (void)unused;
    ++tx.status.unexpected_irqs;
    tx.status.error = RA_TX_ERROR_DTC;
    tx_cutoff();
}

static void tx_cpu_sample(uint16_t raw, uint32_t began) {
    uint16_t i_code, q_code;
    tx.raw = raw;
    if (tx.config.mode == RA_TX_MODE_AM) {
        uint32_t clips = tx.status.dsp_clips;
        i_code = ra_tx_core_am_sample(&tx.config, raw, &clips);
        tx.status.dsp_clips = clips;
        q_code = tx.config.q_zero;
    } else if (ra_tx_is_voice_fm(&tx.config)) {
        ra_tx_core_fm_sample(&tx.dsp.fm, &tx.config, tx.lut, tx.raw, &i_code, &q_code);
        tx.phase = (uint16_t)(tx.dsp.fm.phase >> 16);
        tx.status.dsp_clips = tx.dsp.fm.clips;
        tx.status.adc_rails = tx.dsp.fm.adc_rails;
        tx.status.dc_estimate = (uint16_t)((tx.dsp.fm.dc + 2048) / 4096);
        tx.status.audio_peak = tx.dsp.fm.audio_peak;
    } else {
        ra_tx_core_ssb_sample(&tx.dsp.ssb, &tx.config, tx.raw, &i_code, &q_code);
        tx.status.dsp_clips = tx.dsp.ssb.clips;
    }
    R_DAC->DADR[0] = i_code;
    R_DAC->DADR[1] = q_code;
    uint32_t elapsed = DWT->CYCCNT - began;
    ++tx.status.dsp_samples;
    tx.status.dsp_last_cycles = elapsed;
    if (elapsed > tx.status.dsp_max_cycles) {
        tx.status.dsp_max_cycles = elapsed;
    }
    /* This times the C DSP body, not interrupt entry/exit or missed ADC IRQs.
     * A measured over-budget body is a fault, never silently keep transmitting. */
    if (elapsed >= tx.status.dsp_budget_cycles) {
        ++tx.status.dsp_deadline_misses;
        (void)tx_error(RA_TX_ERROR_DSP_DEADLINE, FSP_ERR_TIMEOUT);
    }
}

static void tx_adc_callback(adc_callback_args_t *args) {
    if (!ra_tx_uses_cpu(&tx.config) || ra_tx_software_source(&tx.config) || args == NULL ||
        args->event != ADC_EVENT_SCAN_COMPLETE || !tx.ready || !tx.status.running || tx.status.error) {
        tx_unexpected_irq(NULL);
        return;
    }
    tx_cpu_sample(R_ADC0->ADDR[1], DWT->CYCCNT);
}

static void tx_file_callback(void *unused) {
    (void)unused;
    if (!tx.config.file_source || !tx.ready || !tx.status.running || tx.status.error) {
        tx_unexpected_irq(NULL);
        return;
    }
    uint16_t raw = 2048;
    #if MICROPY_HW_ENABLE_IQ_ADC
    if (!ra_iq_adc_file_audio_next(&raw, ra_tx_mode_is_ssb(tx.config.mode) ? 2 : 1)) {
        tx.status.file_underruns++;
    }
    /* Capture exactly the selected AF sample, before AM/FM/SSB modulation.
     * Both the oscilloscope and the modulator see this value, including silence. */
    ra_iq_adc_scope_push(&raw, 1);
    #endif
    tx_cpu_sample(raw, DWT->CYCCNT);
}

static void tx_gen_callback(void *unused) {
    (void)unused;
    uint32_t began = DWT->CYCCNT;
    if (!tx.config.gen_source || !tx.ready || !tx.status.running || tx.status.error) {
        tx_unexpected_irq(NULL);
        return;
    }
    uint16_t raw = 2048 + ra_tone_next(&tx.generator);
    #if MICROPY_HW_ENABLE_IQ_ADC
    ra_iq_adc_scope_push(&raw, 1); /* same premodulation AF, not a second oscillator */
    #endif
    tx_cpu_sample(raw, began); /* budget includes generator and scope work */
}

static void tx_transfer(unsigned index, void const *src, void *dest, transfer_size_t size, bool last) {
    transfer_info_t *p = &tx.info[index];
    memset(p, 0, sizeof(*p));
    p->transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    /* Only the destination is the repeat area: never restore a patched SAR. */
    p->transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION;
    p->transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    p->transfer_settings_word_b.chain_mode = last ? TRANSFER_CHAIN_MODE_DISABLED : TRANSFER_CHAIN_MODE_EACH;
    p->transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    p->transfer_settings_word_b.size = size;
    p->transfer_settings_word_b.mode = TRANSFER_MODE_REPEAT;
    p->p_src = src;
    p->p_dest = dest;
    /* FSP copies this 1 into both CRA bytes. Passing 0x0101 here is invalid. */
    p->length = 1;
}

static void tx_build_chain(void) {
    memset(tx.info, 0, sizeof(tx.info));
    if (tx.config.mode == RA_TX_MODE_CW) {
        tx.hold = tx.config.i_zero;
        tx_transfer(0, &tx.hold, (void *)&R_DAC->DADR[0], TRANSFER_SIZE_2_BYTE, true);
        tx.status.transfer_count = 1;
        return;
    }
    tx_transfer(0, (void *)&R_ADC0->ADDR[1], (void *)&tx.raw, TRANSFER_SIZE_2_BYTE, false);
    if (tx.config.mode == RA_TX_MODE_AM) {
        /* Form low16(lut + 2*ADC).  An 8-KiB-aligned 8-KiB table never
         * crosses a 64-KiB window, so the descriptor's upper half stays fixed. */
        tx_transfer(1, &tx.lut_base, (void *)&R_DOC->DODSR, TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(2, (void *)&tx.raw, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(3, (void *)&tx.raw, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(4, (void *)&R_DOC->DODSR, (void *)&tx.info[5].p_src, TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(5, tx.lut, (void *)&R_DAC->DADR[0], TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(6, &tx.config.q_zero, (void *)&R_DAC->DADR[1], TRANSFER_SIZE_2_BYTE, true);
        tx.status.transfer_count = 7;
        return;
    }
    unsigned n = 1;
    for (unsigned i = 0; i < tx.config.fm_gain; ++i) {
        tx_transfer(n++, (void *)&tx.raw, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
    }
    tx_transfer(n++, &tx.bias, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
    tx_transfer(n++, (void *)&R_DOC->DODSR, (void *)&tx.phase, TRANSFER_SIZE_2_BYTE, false);
    /* Compact planar FM addressing.  Preserve the full phase, turn its high
     * byte into a zero-extended index, and form low16(lut + 2*index).  I lives
     * in the first 512-byte plane and Q in the second.  Patch both future SARs
     * and finally restore the phase accumulator for the next ADC event. */
    tx_transfer(n++, (uint8_t *)&tx.phase + 1, (uint8_t *)&tx.index, TRANSFER_SIZE_1_BYTE, false);
    tx_transfer(n++, &tx.lut_base, (void *)&R_DOC->DODSR, TRANSFER_SIZE_2_BYTE, false);
    for (unsigned i = 0; i < 2; ++i) {
        tx_transfer(n++, &tx.index, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
    }
    unsigned patch_i = n++;
    unsigned add_q_plane = n++;
    unsigned patch_q = n++;
    unsigned i_out = n++;
    unsigned q_out = n++;
    unsigned restore_phase = n++;
    tx_transfer(patch_i, (void *)&R_DOC->DODSR, (void *)&tx.info[i_out].p_src, TRANSFER_SIZE_2_BYTE, false);
    tx_transfer(add_q_plane, &tx.q_plane, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
    tx_transfer(patch_q, (void *)&R_DOC->DODSR, (void *)&tx.info[q_out].p_src, TRANSFER_SIZE_2_BYTE, false);
    tx_transfer(i_out, tx.lut, (void *)&R_DAC->DADR[0], TRANSFER_SIZE_2_BYTE, false);
    tx_transfer(q_out, tx.lut + 0x200U, (void *)&R_DAC->DADR[1], TRANSFER_SIZE_2_BYTE, false);
    tx_transfer(restore_phase, (void *)&tx.phase, (void *)&R_DOC->DODSR, TRANSFER_SIZE_2_BYTE, true);
    tx.status.transfer_count = n;
}

/* Source is already stopped before this check. VECN is the IRQ index, NOT
 * IELSR.IELS (event number). No unbounded FSP Reconfigure wait is used. */
static bool tx_quiesce(void) {
    if (tx.timer_reserved && !ra_agt_timer_stop_wait((uint32_t)tx.status.timer_channel)) {
        return tx_error(RA_TX_ERROR_STOP_TIMEOUT, FSP_ERR_TIMEOUT);
    }
    if (ra_tx_software_source(&tx.config) && tx.timer_reserved) {
        R_BSP_IrqDisable(tx.irq);
        R_BSP_IrqStatusClear(tx.irq);
        NVIC_ClearPendingIRQ(tx.irq);
    }
    if (tx.adc_open) {
        unsigned wait;
        for (wait = 0; wait < TX_WAIT_US && R_ADC0->ADCSR_b.ADST; ++wait) {
            R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MICROSECONDS);
        }
        if (R_ADC0->ADCSR_b.ADST) {
            return tx_error(RA_TX_ERROR_STOP_TIMEOUT, FSP_ERR_TIMEOUT);
        }
        /* No further AGT event can start a scan. */
        (void)R_ADC_ScanStop((adc_ctrl_t *)&tx.adc);
        if (ra_tx_uses_cpu(&tx.config)) {
            R_BSP_IrqDisable(tx.irq);
            R_BSP_IrqStatusClear(tx.irq);
            NVIC_ClearPendingIRQ(tx.irq);
        }
    }
    if (tx.dtc_open) {
        R_BSP_IrqDisable(tx.irq);
        fsp_err_t err = R_DTC_Disable((transfer_ctrl_t *)&tx.dtc);
        if (err != FSP_SUCCESS) {
            return tx_error(RA_TX_ERROR_DTC, err);
        }
        __DSB();
        unsigned wait;
        for (wait = 0; wait < TX_WAIT_US; ++wait) {
            uint16_t sts = R_DTC->DTCSTS;
            if (!(sts & R_DTC_DTCSTS_ACT_Msk) || (sts & R_DTC_DTCSTS_VECN_Msk) != (uint16_t)tx.irq) {
                break;
            }
            R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MICROSECONDS);
        }
        if (wait == TX_WAIT_US) {
            return tx_error(RA_TX_ERROR_STOP_TIMEOUT, FSP_ERR_TIMEOUT);
        }
        R_BSP_IrqStatusClear(tx.irq);
        NVIC_ClearPendingIRQ(tx.irq);
    }
    if (!tx_scope_close()) {
        return tx_error(RA_TX_ERROR_STOP_TIMEOUT, (fsp_err_t)tx.status.af_error);
    }
    tx.status.running = false;
    tx.status.quiesced = true;
    return true;
}

/* Only call after quiescing. Close/Open invalidates cached DTC information,
 * validates every descriptor and avoids FSP's unbounded Reconfigure loop. */
static bool tx_open_chain(void) {
    fsp_err_t err;
    if (tx.dtc_open) {
        err = R_DTC_Close((transfer_ctrl_t *)&tx.dtc);
        if (err != FSP_SUCCESS) {
            return tx_error(RA_TX_ERROR_DTC, err);
        }
        tx.dtc_open = false;
    }
    tx.dtc_ext.activation_source = tx.irq;
    tx.dtc_cfg.p_info = tx.info;
    tx.dtc_cfg.p_extend = &tx.dtc_ext;
    err = R_DTC_Open((transfer_ctrl_t *)&tx.dtc, &tx.dtc_cfg);
    if (err != FSP_SUCCESS) {
        return tx_error(RA_TX_ERROR_DTC, err);
    }
    tx.dtc_open = true;
    return true;
}

static bool tx_arm(void) {
    if (!ra_agt_timer_set_counter((uint32_t)tx.status.timer_channel, tx.status.timer_period - 1U)) {
        return tx_error(RA_TX_ERROR_TIMER, FSP_ERR_INVALID_ARGUMENT);
    }
    fsp_err_t err;
    if (tx.dtc_open) {
        err = R_DTC_Enable((transfer_ctrl_t *)&tx.dtc);
        if (err != FSP_SUCCESS) {
            return tx_error(RA_TX_ERROR_DTC, err);
        }
    }
    if (tx.adc_open) {
        err = R_ADC_ScanStart((adc_ctrl_t *)&tx.adc);
        if (err != FSP_SUCCESS) {
            if (tx.dtc_open) {
                (void)R_DTC_Disable((transfer_ctrl_t *)&tx.dtc);
            }
            return tx_error(RA_TX_ERROR_ADC, err);
        }
    }
    R_BSP_IrqStatusClear(tx.irq);
    NVIC_ClearPendingIRQ(tx.irq);
    __DMB();
    tx.status.running = true;
    tx.status.quiesced = false;
    R_BSP_IrqEnable(tx.irq);
    ra_agt_timer_start((uint32_t)tx.status.timer_channel);
    return true;
}

bool ra_tx_hw_init(const ra_tx_config_t *config, uint8_t *lut, size_t lut_bytes) {
    if (tx.status.owned) {
        return tx_error(RA_TX_ERROR_BUSY, FSP_ERR_IN_USE);
    }
    size_t required = ra_tx_core_lut_bytes(config);
    size_t alignment = ra_tx_core_lut_alignment(config);
    uintptr_t address = (uintptr_t)lut;
    if (!ra_tx_core_validate(config) || (required != 0 &&
        (lut == NULL || (address & (alignment - 1U)) || lut_bytes < required ||
        (address & 0xffffU) + required > 0x10000U))) {
        return tx_error(RA_TX_ERROR_CONFIG, FSP_ERR_INVALID_ARGUMENT);
    }
    #if MICROPY_HW_ENABLE_IQ_ADC
    if (ra_iq_adc_owns_adc()) {
        return tx_error(RA_TX_ERROR_BUSY, FSP_ERR_IN_USE);
    }
    #endif
    #if MICROPY_HW_ENABLE_AUDIOADC
    if (ra_storm_adc_owns_adc()) {
        return tx_error(RA_TX_ERROR_BUSY, FSP_ERR_IN_USE);
    }
    #endif
    if (ra_dac_pair_is_owned() || ra_dac_stream_is_active(0) || ra_dac_stream_is_active(1)) {
        return tx_error(RA_TX_ERROR_BUSY, FSP_ERR_IN_USE);
    }
    /* Stream status may have completed a deferred cleanup and gated the DAC. */
    bool dac_clocked = !(R_MSTP->MSTPCRD & R_MSTP_MSTPCRD_MSTPD20_Msk);
    if (dac_clocked && (ra_dac_is_running(0) || ra_dac_is_running(1))) {
        return tx_error(RA_TX_ERROR_BUSY, FSP_ERR_IN_USE);
    }
    memset(&tx, 0, sizeof(tx));
    tx.config = *config;
    tx.lut = lut;
    tx.lut_base = (uint16_t)(uintptr_t)lut;
    tx.index = 0;
    tx.q_plane = 0x200U;
    tx.status.mode = config->mode;
    tx.status.cpu_dsp = ra_tx_uses_cpu(config);
    tx.status.file_source = config->file_source;
    tx.status.requested_rate_hz = config->sample_rate_hz;
    tx.status.timer_channel = -1;
    tx.status.owned = true; /* includes partial initialization and stopped state */
    ++tx_adc_epoch; /* also tags FILE/CW restarts for stale-display invalidation */
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (ra_agt_timer_reserve(ch)) {
            tx.status.timer_channel = ch;
            tx.timer_reserved = true;
            break;
        }
    }
    if (!tx.timer_reserved) {
        tx.status.owned = false;
        return tx_error(RA_TX_ERROR_TIMER, FSP_ERR_IN_USE);
    }
    unsigned ch = (unsigned)tx.status.timer_channel;
    tx.irq = (config->mode == RA_TX_MODE_CW || ra_tx_software_source(config)) ?
        (IRQn_Type)(VECTOR_NUMBER_AGT0_INT + ch) : VECTOR_NUMBER_ADC0_SCAN_END;
    ra_agt_timer_init(ch, (float)config->sample_rate_hz);
    R_BSP_IrqDisable((IRQn_Type)(VECTOR_NUMBER_AGT0_INT + ch));
    if (!ra_agt_timer_stop_wait(ch)) {
        return tx_error(RA_TX_ERROR_STOP_TIMEOUT, FSP_ERR_TIMEOUT);
    }
    ra_agt_timer_set_callback(ch, config->gen_source ? tx_gen_callback :
        config->file_source ? tx_file_callback : tx_unexpected_irq, NULL);
    tx.status.timer_clock_hz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / 2U;
    uint32_t period = (tx.status.timer_clock_hz + config->sample_rate_hz / 2U) / config->sample_rate_hz;
    if (!ra_agt_timer_set_period(ch, period)) {
        return tx_error(RA_TX_ERROR_TIMER, FSP_ERR_INVALID_ARGUMENT);
    }
    tx.status.timer_period = ra_agt_timer_get_period(ch);
    if (config->gen_source && !ra_tone_configure(&tx.generator, tx.status.timer_clock_hz,
        tx.status.timer_period, config->gen_frequency_dhz,
        (2047U * config->gen_level + 50U) / 100U, config->gen_wave)) {
        return tx_error(RA_TX_ERROR_CONFIG, FSP_ERR_INVALID_ARGUMENT);
    }
    tx.status.quiesced = true;
    if (ra_tx_uses_cpu(config)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; /* never reset a shared cycle counter */
        tx.status.dsp_budget_cycles = (uint32_t)((uint64_t)R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_ICLK)
            * tx.status.timer_period / tx.status.timer_clock_hz);
    }

    /* Unbuffered DAC: external high-impedance I/Q input required. No amplifier
     * startup short-to-ground sequence, and no claim of simultaneous latching. */
    ra_mstpcrd_start(R_MSTP_MSTPCRD_MSTPD20_Msk);
    R_DAC->DACR = TX_DAC_DISABLED;
    R_DAC->DAAMPCR = 0;
    R_DAC->DAASWCR = 0;
    R_DAC->DADPR = 0;
    R_DAC->DAADSCR = 0;
    tx.dac_open = true;
    tx_neutral();
    ra_gpio_config(P014, GPIO_MODE_ANALOG, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
    ra_gpio_config(P015, GPIO_MODE_ANALOG, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
    R_DAC->DACR = TX_DAC_DISABLED | TX_DAC_OUTPUT_MASK;
    if (!ra_tx_core_build_lut(config, lut, lut_bytes)) {
        return tx_error(RA_TX_ERROR_CONFIG, FSP_ERR_INVALID_ARGUMENT);
    }
    tx.bias = ra_tx_core_fm_bias(config);
    if (config->mode == RA_TX_MODE_CW) {
        ra_tx_core_ramp(tx.dsp.cw.shape, config->ramp_samples, 0, UINT16_MAX);
    }
    if (config->mode != RA_TX_MODE_CW && !ra_tx_software_source(config)) {
        /* Pre-existing standalone ADC objects must be reconstructed afterward;
         * their resolution/PGA/pin settings are not a saved hardware context. */
        ra_adc_enable(P001);
        tx.pin_enabled = true;
        tx.adc_cfg = g_adc0_cfg;
        tx.adc_ext = *(adc_extended_cfg_t *)g_adc0_cfg.p_extend;
        memset(&tx.adc_channels, 0, sizeof(tx.adc_channels));
        tx.adc_cfg.unit = 0;
        tx.adc_cfg.mode = ADC_MODE_SINGLE_SCAN;
        tx.adc_cfg.resolution = ADC_RESOLUTION_12_BIT;
        tx.adc_cfg.alignment = ADC_ALIGNMENT_RIGHT;
        tx.adc_cfg.trigger = ADC_TRIGGER_SYNC_ELC;
        tx.adc_cfg.scan_end_irq = VECTOR_NUMBER_ADC0_SCAN_END;
        tx.adc_cfg.scan_end_ipl = 5;
        tx.adc_cfg.scan_end_b_irq = FSP_INVALID_VECTOR;
        tx.adc_cfg.scan_end_b_ipl = BSP_IRQ_DISABLED;
        tx.adc_cfg.p_callback = tx_adc_callback;
        tx.adc_cfg.p_context = NULL;
        tx.adc_cfg.p_extend = &tx.adc_ext;
        tx.adc_ext.clearing = ADC_CLEAR_AFTER_READ_OFF;
        tx.adc_ext.add_average_count = ADC_ADD_OFF;
        tx.adc_ext.double_trigger_mode = ADC_DOUBLE_TRIGGER_DISABLED;
        tx.adc_ext.enable_adbuf = 0;
        tx.adc_ext.window_a_irq = FSP_INVALID_VECTOR;
        tx.adc_ext.window_b_irq = FSP_INVALID_VECTOR;
        tx.adc_ext.window_a_ipl = BSP_IRQ_DISABLED;
        tx.adc_ext.window_b_ipl = BSP_IRQ_DISABLED;
        tx.adc_channels.scan_mask = 2;
        tx.adc_channels.sample_hold_states = 24;
        fsp_err_t err = R_ADC_Open((adc_ctrl_t *)&tx.adc, &tx.adc_cfg);
        if (err != FSP_SUCCESS) {
            return tx_error(RA_TX_ERROR_ADC, err);
        }
        tx.adc_open = true;
        R_BSP_IrqDisable(tx.irq);
        err = R_ADC_ScanCfg((adc_ctrl_t *)&tx.adc, &tx.adc_channels);
        if (err != FSP_SUCCESS) {
            return tx_error(RA_TX_ERROR_ADC, err);
        }
        ra_mstpcrc_start(R_MSTP_MSTPCRC_MSTPC14_Msk);
        tx.saved_elc = R_ELC->ELSR[TX_ELC_ADC0].HA;
        R_ELC->ELSR[TX_ELC_ADC0].HA = ch == 0 ? ELC_EVENT_AGT0_INT : ELC_EVENT_AGT1_INT;
        FSP_REGISTER_READ(R_ELC->ELSR[TX_ELC_ADC0].HA);
        R_ELC->ELCR |= R_ELC_ELCR_ELCON_Msk;
        tx.elc_owned = true;
        if (!ra_tx_uses_cpu(config)) {
            R_BSP_MODULE_START(FSP_IP_DOC, 0);
            tx.doc_open = true;
            R_DOC->DOCR = 0x41; /* addition; clear overflow; no DOC IRQ is routed */
            R_DOC->DODSR = 0;
        }
    }
    if (!ra_tx_uses_cpu(config)) {
        tx_build_chain();
        if (!tx_open_chain()) {
            return false;
        }
    }
    tx.ready = true;
    return true; /* no trigger until explicit start() */
}

bool ra_tx_hw_start(void) {
    if (!tx.ready || tx.status.error != RA_TX_ERROR_NONE) {
        return false;
    }
    if (tx.status.running) {
        return true;
    }
    #if MICROPY_HW_ENABLE_IQ_ADC
    if (tx.config.file_source && ra_iq_adc_file_decode_service() == 0) {
        return tx_error(RA_TX_ERROR_CONFIG, FSP_ERR_NOT_INITIALIZED);
    }
    #endif
    if (!tx_quiesce()) {
        return false;
    }
    if (ra_tx_uses_cpu(&tx.config)) {
        if (tx.config.gen_source) { ra_tone_reset(&tx.generator); }
        if (ra_tx_is_voice_fm(&tx.config)) {
            if (!ra_tx_core_fm_reset(&tx.dsp.fm, &tx.config,
                tx.status.timer_clock_hz, tx.status.timer_period)) {
                return tx_error(RA_TX_ERROR_CONFIG, FSP_ERR_INVALID_ARGUMENT);
            }
        } else if (ra_tx_mode_is_ssb(tx.config.mode)) {
            ra_tx_core_ssb_reset(&tx.dsp.ssb);
        }
        tx.status.dsp_samples = 0;
        tx.status.dsp_clips = 0;
        tx.status.adc_rails = 0;
        tx.status.dc_estimate = 0;
        tx.status.audio_peak = 0;
        tx.status.dsp_last_cycles = 0;
        tx.status.dsp_max_cycles = 0;
        tx.status.dsp_deadline_misses = 0;
    } else {
        tx_build_chain();
        if (!tx_open_chain()) {
            return false;
        }
    }
    tx.phase = 0;
    if (tx.doc_open) {
        R_DOC->DODSR = 0;
    }
    tx.status.keyed = false;
    tx_neutral();
    return tx_arm();
}

bool ra_tx_hw_stop(void) {
    if (!tx.status.owned) {
        return true;
    }
    if (!tx_quiesce()) {
        return false;
    }
    tx.status.keyed = false;
    tx_neutral();
    return true;
}

bool ra_tx_hw_fm_configure(const ra_tx_config_t *config) {
    /* Prepare outside the critical section; publish three controls + phase
     * scale together between samples. Never stop/restart the stream for a knob.
     * Reject invalid control requests without faulting an otherwise healthy TX. */
    if (!tx.ready || tx.status.error != RA_TX_ERROR_NONE ||
        !ra_tx_is_voice_fm(&tx.config) || !ra_tx_core_validate(config) ||
        !ra_tx_is_voice_fm(config) || config->sample_rate_hz != tx.config.sample_rate_hz ||
        config->adc_mid != tx.config.adc_mid || config->i_zero != tx.config.i_zero ||
        config->q_zero != tx.config.q_zero) {
        return false;
    }
    uint32_t step = ra_tx_core_fm_step(tx.status.timer_clock_hz,
        tx.status.timer_period, config->deviation_hz);
    if (step == 0) {
        return false;
    }
    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;
    tx.config.deviation_hz = config->deviation_hz;
    tx.config.mic_gain = config->mic_gain;
    tx.config.amplitude = config->amplitude;
    tx.dsp.fm.max_step = step;
    FSP_CRITICAL_SECTION_EXIT;
    return true;
}

bool ra_tx_hw_audio_configure(const ra_tx_config_t *config) {
    if (!tx.ready || tx.status.error != RA_TX_ERROR_NONE || !config->audio_controls ||
        !tx.config.audio_controls || !ra_tx_core_validate(config) ||
        config->mode != tx.config.mode || config->file_source != tx.config.file_source ||
        config->gen_source != tx.config.gen_source ||
        config->sample_rate_hz != tx.config.sample_rate_hz || config->adc_mid != tx.config.adc_mid ||
        config->i_zero != tx.config.i_zero || config->q_zero != tx.config.q_zero) {
        return false;
    }
    /* Opt-in MIC/FILE AM and SSB all use the C sample callback, not a LUT.
     * Preserve ADC ownership, decoder position, scope and filter history.
     * Three bounded scalars are published atomically between samples. */
    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;
    tx.config.audio_gain = config->audio_gain;
    tx.config.am_depth = config->am_depth;
    tx.config.amplitude = config->amplitude;
    FSP_CRITICAL_SECTION_EXIT;
    return true;
}

bool ra_tx_hw_gen_configure(const ra_tx_config_t *config) {
    if (!tx.ready || tx.status.error || !tx.config.gen_source || !config->gen_source ||
        !ra_tx_core_validate(config) || config->mode != tx.config.mode ||
        config->sample_rate_hz != tx.config.sample_rate_hz) {
        return false;
    }
    ra_tone_gen_t prepared = {0};
    if (!ra_tone_configure(&prepared, tx.status.timer_clock_hz, tx.status.timer_period,
        config->gen_frequency_dhz, (2047U * config->gen_level + 50U) / 100U, config->gen_wave)) {
        return false;
    }
    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;
    tx.generator.step = prepared.step;
    tx.generator.peak = prepared.peak;
    tx.generator.wave = prepared.wave;
    tx.config.gen_frequency_dhz = config->gen_frequency_dhz;
    tx.config.gen_level = config->gen_level;
    tx.config.gen_wave = config->gen_wave;
    FSP_CRITICAL_SECTION_EXIT; /* keep current phase, filter state and scope */
    return true;
}

bool ra_tx_hw_key(bool down) {
    if (!tx.ready || tx.config.mode != RA_TX_MODE_CW || !tx.status.running ||
        tx.status.error != RA_TX_ERROR_NONE) {
        return false;
    }
    if (tx.status.keyed == down) {
        return true;
    }
    if (!tx_quiesce()) {
        return false;
    }
    uint16_t from = R_DAC->DADR[0];
    tx.hold = tx.config.i_zero + (down ? tx.config.amplitude : 0);
    ra_tx_core_scale_ramp(tx.dsp.cw.ramp, tx.dsp.cw.shape, tx.config.ramp_samples, from, tx.hold);
    /* The last ramp event writes back D0, THEN the three descriptors change D0
     * into a fixed repeat/hold. The last descriptor is itself endless repeat,
     * so the normal ramp completion never delivers a CPU interrupt (18.4.1). */
    tx_transfer(0, &tx.hold, (void *)&R_DAC->DADR[0], TRANSFER_SIZE_2_BYTE, true);
    tx.hold_settings = tx.info[0].transfer_settings_word;
    tx.hold_source = (uint32_t)(uintptr_t)&tx.hold;
    tx.hold_counts = 0x01010000U; /* CRB @+12 = 0, CRA @+14 = 0x0101 */
    tx.info[0].transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    tx.info[0].transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    tx.info[0].transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_END;
    tx.info[0].p_src = tx.dsp.cw.ramp;
    tx.info[0].length = tx.config.ramp_samples;
    tx_transfer(1, &tx.hold_settings, &tx.info[0].transfer_settings_word, TRANSFER_SIZE_4_BYTE, false);
    tx_transfer(2, &tx.hold_source, (void *)&tx.info[0].p_src, TRANSFER_SIZE_4_BYTE, false);
    tx_transfer(3, &tx.hold_counts, (void *)&tx.info[0].num_blocks, TRANSFER_SIZE_4_BYTE, true);
    tx.status.transfer_count = 4;
    if (!tx_open_chain()) {
        tx_neutral();
        return false;
    }
    tx.status.keyed = down;
    return tx_arm();
}

bool ra_tx_hw_deinit_checked(void) {
    if (!tx.status.owned) {
        return true;
    }
    if (!ra_tx_hw_stop()) {
        return false; /* keep ownership, descriptors and GC roots */
    }
    if (tx.dtc_open) {
        fsp_err_t err = R_DTC_Close((transfer_ctrl_t *)&tx.dtc);
        if (err != FSP_SUCCESS) {
            return tx_error(RA_TX_ERROR_DTC, err);
        }
        tx.dtc_open = false;
    }
    if (tx.adc_open) {
        fsp_err_t err = R_ADC_Close((adc_ctrl_t *)&tx.adc);
        if (err != FSP_SUCCESS) {
            return tx_error(RA_TX_ERROR_ADC, err);
        }
        tx.adc_open = false;
    }
    if (tx.elc_owned) {
        R_ELC->ELSR[TX_ELC_ADC0].HA = tx.saved_elc;
        FSP_REGISTER_READ(R_ELC->ELSR[TX_ELC_ADC0].HA);
        tx.elc_owned = false;
    }
    if (tx.timer_reserved) {
        if (!ra_agt_timer_deinit_checked((uint32_t)tx.status.timer_channel)) {
            return tx_error(RA_TX_ERROR_STOP_TIMEOUT, FSP_ERR_TIMEOUT);
        }
        tx.timer_reserved = false;
    }
    if (tx.pin_enabled) {
        ra_adc_disable(P001);
        tx.pin_enabled = false;
    }
    if (tx.doc_open) {
        R_DOC->DOCR = 0x40;
        R_BSP_MODULE_STOP(FSP_IP_DOC, 0);
        tx.doc_open = false;
    }
    if (tx.dac_open) {
        R_DAC->DACR = TX_DAC_DISABLED;
        ra_gpio_config(P014, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
        ra_gpio_config(P015, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
        ra_mstpcrd_stop(R_MSTP_MSTPCRD_MSTPD20_Msk);
        tx.dac_open = false;
    }
    #if MICROPY_HW_ENABLE_IQ_ADC
    if (tx.config.file_source) {
        ra_iq_adc_file_detach();
    }
    #endif
    memset(&tx, 0, sizeof(tx));
    tx.status.timer_channel = -1;
    return true;
}

bool ra_tx_hw_owns_resources(void) { return tx.status.owned; }
bool ra_tx_hw_owns_adc(void) { return tx.status.owned; }
bool ra_tx_hw_owns_dac(void) { return tx.status.owned; }
uint32_t ra_tx_hw_adc_epoch(void) { return tx_adc_epoch; }

void ra_tx_hw_get_status(ra_tx_status_t *status) {
    *status = tx.status;
    status->last_adc = tx.raw;
    status->phase = tx.phase;
    if (tx.dac_open) {
        uint8_t enables = R_DAC->DACR & TX_DAC_OUTPUT_MASK;
        status->i_enabled = (enables & 0x40U) != 0;
        status->q_enabled = (enables & 0x80U) != 0;
        status->outputs_enabled = enables == TX_DAC_OUTPUT_MASK;
        status->i_code = R_DAC->DADR[0];
        status->q_code = R_DAC->DADR[1];
    }
    /* These debug reads are deliberately not an atomic per-sample snapshot. */
}
#endif
