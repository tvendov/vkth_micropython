/*
 * RA6M3 baseband TX: AGT -> ADC0 -> DTC -> DOC -> LUT -> DAC0/1.
 * No sample callback, buffer refill or CPU NCO. See the board TX README.
 * Manual R01UH0886EJ0120: sections 18 (DTC), 47 (ADC), 48 (DAC), 52 (DOC).
 */
#include <stddef.h>
#include <string.h>
#include "hal_data.h"
#include "r_adc.h"
#include "r_dtc.h"
#include "ra_adc.h"
#include "ra_dac.h"
#include "ra_gpio.h"
#include "ra_timer.h"
#include "ra_utils.h"
#include "ra_tx_hw.h"
#include "ra_tx_core.h"
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
#define TX_MAX_TRANSFERS (9U)
#define TX_DAC_DISABLED (0x1fU) /* DACR reserved bits 4:0 must be written 1 */
#define TX_DAC_OUTPUT_MASK (0xc0U) /* RA6M3 DACR.DAOE1:DAOE0, manual 48.2.2 */

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
    uint16_t bias, hold;
    uint16_t ramp[RA_TX_MAX_RAMP_SAMPLES];
    uint16_t shape[RA_TX_MAX_RAMP_SAMPLES];
    uint32_t hold_settings, hold_source, hold_counts;
} tx_state_t;

static tx_state_t tx;
static uint32_t tx_adc_epoch;

/* A cutoff is not proof of quiescence and cannot replace an external RF mute.
 * Keep ownership and running/quiesced diagnostics until checked stop succeeds. */
static void tx_cutoff(void) {
    if (tx.timer_reserved) {
        ra_agt_timer_stop((uint32_t)tx.status.timer_channel);
    }
    if (tx.dtc_open) {
        R_BSP_IrqDisable(tx.irq);
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

static void tx_adc_fault(adc_callback_args_t *args) {
    (void)args;
    tx_unexpected_irq(NULL);
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
        /* DOC forms 2*ADC (byte offset), not a phase accumulator in AM. */
        tx_transfer(1, (void *)&tx.raw, (void *)&R_DOC->DODSR, TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(2, (void *)&tx.raw, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(3, (void *)&R_DOC->DODSR, (void *)&tx.info[4].p_src, TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(4, tx.lut, (void *)&R_DAC->DADR[0], TRANSFER_SIZE_2_BYTE, false);
        tx_transfer(5, &tx.config.q_zero, (void *)&R_DAC->DADR[1], TRANSFER_SIZE_2_BYTE, true);
        tx.status.transfer_count = 6;
        return;
    }
    unsigned n = 1;
    for (unsigned i = 0; i < tx.config.fm_gain; ++i) {
        tx_transfer(n++, (void *)&tx.raw, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
    }
    tx_transfer(n++, &tx.bias, (void *)&R_DOC->DODIR, TRANSFER_SIZE_2_BYTE, false);
    tx_transfer(n++, (void *)&R_DOC->DODSR, (void *)&tx.phase, TRANSFER_SIZE_2_BYTE, false);
    unsigned i_out = n + 2;
    unsigned q_out = n + 3;
    tx_transfer(n++, (uint8_t *)&tx.phase + 1, (uint8_t *)&tx.info[i_out].p_src + 1, TRANSFER_SIZE_1_BYTE, false);
    tx_transfer(n++, (uint8_t *)&tx.phase + 1, (uint8_t *)&tx.info[q_out].p_src + 1, TRANSFER_SIZE_1_BYTE, false);
    tx_transfer(n++, tx.lut, (void *)&R_DAC->DADR[0], TRANSFER_SIZE_2_BYTE, false);
    tx_transfer(n++, tx.lut + 2, (void *)&R_DAC->DADR[1], TRANSFER_SIZE_2_BYTE, true);
    tx.status.transfer_count = n;
}

/* Source is already stopped before this check. VECN is the IRQ index, NOT
 * IELSR.IELS (event number). No unbounded FSP Reconfigure wait is used. */
static bool tx_quiesce(void) {
    if (tx.timer_reserved && !ra_agt_timer_stop_wait((uint32_t)tx.status.timer_channel)) {
        return tx_error(RA_TX_ERROR_STOP_TIMEOUT, FSP_ERR_TIMEOUT);
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
    fsp_err_t err = R_DTC_Enable((transfer_ctrl_t *)&tx.dtc);
    if (err != FSP_SUCCESS) {
        return tx_error(RA_TX_ERROR_DTC, err);
    }
    if (tx.adc_open) {
        err = R_ADC_ScanStart((adc_ctrl_t *)&tx.adc);
        if (err != FSP_SUCCESS) {
            (void)R_DTC_Disable((transfer_ctrl_t *)&tx.dtc);
            return tx_error(RA_TX_ERROR_ADC, err);
        }
    }
    R_BSP_IrqStatusClear(tx.irq);
    NVIC_ClearPendingIRQ(tx.irq);
    __DMB();
    R_BSP_IrqEnable(tx.irq);
    tx.status.running = true;
    tx.status.quiesced = false;
    ra_agt_timer_start((uint32_t)tx.status.timer_channel);
    return true;
}

bool ra_tx_hw_init(const ra_tx_config_t *config, uint8_t *lut, size_t lut_bytes) {
    if (tx.status.owned) {
        return tx_error(RA_TX_ERROR_BUSY, FSP_ERR_IN_USE);
    }
    if (!ra_tx_core_validate(config) || (config->mode != RA_TX_MODE_CW &&
        (lut == NULL || ((uintptr_t)lut & 0xffffU) || lut_bytes < RA_TX_LUT_BYTES))) {
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
    if (ra_dac_stream_is_active(0) || ra_dac_stream_is_active(1)) {
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
    tx.status.mode = config->mode;
    tx.status.requested_rate_hz = config->sample_rate_hz;
    tx.status.timer_channel = -1;
    tx.status.owned = true; /* includes partial initialization and stopped state */
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
    tx.irq = config->mode == RA_TX_MODE_CW ? (IRQn_Type)(VECTOR_NUMBER_AGT0_INT + ch) : VECTOR_NUMBER_ADC0_SCAN_END;
    ra_agt_timer_init(ch, (float)config->sample_rate_hz);
    R_BSP_IrqDisable((IRQn_Type)(VECTOR_NUMBER_AGT0_INT + ch));
    if (!ra_agt_timer_stop_wait(ch)) {
        return tx_error(RA_TX_ERROR_STOP_TIMEOUT, FSP_ERR_TIMEOUT);
    }
    ra_agt_timer_set_callback(ch, tx_unexpected_irq, NULL);
    tx.status.timer_clock_hz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / 2U;
    uint32_t period = (tx.status.timer_clock_hz + config->sample_rate_hz / 2U) / config->sample_rate_hz;
    if (!ra_agt_timer_set_period(ch, period)) {
        return tx_error(RA_TX_ERROR_TIMER, FSP_ERR_INVALID_ARGUMENT);
    }
    tx.status.timer_period = ra_agt_timer_get_period(ch);
    tx.status.quiesced = true;

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
        ra_tx_core_ramp(tx.shape, config->ramp_samples, 0, UINT16_MAX);
    }
    if (config->mode != RA_TX_MODE_CW) {
        /* Pre-existing standalone ADC objects must be reconstructed afterward;
         * their resolution/PGA/pin settings are not a saved hardware context. */
        ++tx_adc_epoch;
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
        tx.adc_cfg.p_callback = tx_adc_fault;
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
        R_BSP_MODULE_START(FSP_IP_DOC, 0);
        tx.doc_open = true;
        R_DOC->DOCR = 0x41; /* addition; clear overflow; no DOC IRQ is routed */
        R_DOC->DODSR = 0;
    }
    tx_build_chain();
    if (!tx_open_chain()) {
        return false;
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
    if (!tx_quiesce()) {
        return false;
    }
    tx_build_chain();
    if (!tx_open_chain()) {
        return false;
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
    ra_tx_core_scale_ramp(tx.ramp, tx.shape, tx.config.ramp_samples, from, tx.hold);
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
    tx.info[0].p_src = tx.ramp;
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
        status->outputs_enabled = (R_DAC->DACR & TX_DAC_OUTPUT_MASK) != 0;
        status->i_code = R_DAC->DADR[0];
        status->q_code = R_DAC->DADR[1];
    }
    /* These debug reads are deliberately not an atomic per-sample snapshot. */
}
#endif
