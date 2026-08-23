#include <string.h>

#include "hal_data.h"
#include "r_adc.h"
#include "r_dtc.h"
#include "ra_adc.h"
#include "ra_storm_adc.h"
#include "ra_timer.h"
#include "ra_utils.h"
#include "vector_data.h"

#define RA_STORM_ELC_ADC0_INDEX (8U)

typedef struct {
    bool opened;
    bool dtc_open;
    bool timer_reserved;
    bool pin_enabled;
    bool elc_enabled;
    uint8_t adc_ch;
    uint8_t timer_ch;
    uint32_t half_sequence[2];
    adc_instance_ctrl_t adc_ctrl;
    adc_cfg_t adc_cfg;
    adc_extended_cfg_t adc_cfg_extend;
    adc_channel_cfg_t adc_channel_cfg;
    dtc_instance_ctrl_t dtc_ctrl;
    dtc_extended_cfg_t dtc_ext;
    transfer_cfg_t dtc_cfg;
    transfer_info_t dtc_info;
} ra_storm_adc_private_t;

static ra_storm_adc_private_t s_storm_adc;
static ra_storm_adc_status_t s_storm_status;
static uint16_t s_raw_ring[RA_STORM_ADC_MAX_FRAME_SAMPLES * 2];
static int16_t s_centered[2][RA_STORM_ADC_MAX_FRAME_SAMPLES];

static elc_event_t ra_storm_agt_event(uint32_t ch) {
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
        #if BSP_FEATURE_AGT_MAX_CHANNEL_NUM >= 4
        case 4:
            return ELC_EVENT_AGT4_INT;
        #endif
        #if BSP_FEATURE_AGT_MAX_CHANNEL_NUM >= 5
        case 5:
            return ELC_EVENT_AGT5_INT;
        #endif
        default:
            return ELC_EVENT_NONE;
    }
}

static bool ra_storm_reserve_timer(uint8_t *timer_ch) {
    for (uint32_t ch = 0; ch <= BSP_FEATURE_AGT_MAX_CHANNEL_NUM; ++ch) {
        if (ra_agt_timer_reserve(ch)) {
            *timer_ch = (uint8_t)ch;
            return true;
        }
    }
    return false;
}

static void ra_storm_elc_enable(elc_event_t event) {
    ra_mstpcrc_start(R_MSTP_MSTPCRC_MSTPC14_Msk);
    R_ELC->ELSR[RA_STORM_ELC_ADC0_INDEX].HA = (uint16_t)event;
    FSP_REGISTER_READ(R_ELC->ELSR[RA_STORM_ELC_ADC0_INDEX].HA);
    R_ELC->ELCR = R_ELC_ELCR_ELCON_Msk;
    FSP_REGISTER_READ(R_ELC->ELCR);
}

static void ra_storm_elc_disable(void) {
    R_ELC->ELSR[RA_STORM_ELC_ADC0_INDEX].HA = 0U;
    FSP_REGISTER_READ(R_ELC->ELSR[RA_STORM_ELC_ADC0_INDEX].HA);
}

static void ra_storm_update_ready_state(void) {
    if (!s_storm_status.running) {
        return;
    }

    transfer_properties_t props;
    if (R_DTC_InfoGet((transfer_ctrl_t *)&s_storm_adc.dtc_ctrl, &props) != FSP_SUCCESS) {
        return;
    }

    uint32_t remaining = props.transfer_length_remaining;
    uint32_t ring_samples = s_storm_status.ring_samples;
    if (remaining > ring_samples) {
        remaining = 0U;
    }

    uint32_t write_index = ring_samples - remaining;
    if (write_index >= ring_samples) {
        write_index = 0U;
    }

    s_storm_status.write_index = (uint16_t)write_index;
    uint8_t current_half = (write_index < s_storm_status.frame_samples) ? 0U : 1U;

    if (current_half != s_storm_status.active_half) {
        uint8_t completed_half = s_storm_status.active_half;
        uint8_t bit = (uint8_t)(1U << completed_half);

        if ((s_storm_status.ready_mask & bit) != 0U) {
            s_storm_status.overflow = 1U;
            s_storm_status.dropped_frames++;
        }

        s_storm_status.frame_sequence++;
        s_storm_adc.half_sequence[completed_half] = s_storm_status.frame_sequence;
        s_storm_status.ready_mask |= bit;
        s_storm_status.active_half = current_half;
    }
}

bool ra_storm_adc_init(uint32_t pin, uint32_t sample_rate_hz, size_t frame_samples) {
    uint8_t adc_ch;
    uint8_t timer_ch;
    elc_event_t agt_event;

    if (frame_samples == 0 || frame_samples > RA_STORM_ADC_MAX_FRAME_SAMPLES || sample_rate_hz == 0) {
        return false;
    }
    if (!ra_adc_pin_to_ch(pin, &adc_ch)) {
        return false;
    }
    if (!ra_storm_reserve_timer(&timer_ch)) {
        return false;
    }

    memset(&s_storm_adc, 0, sizeof(s_storm_adc));
    memset(&s_storm_status, 0, sizeof(s_storm_status));
    memset(s_raw_ring, 0, sizeof(s_raw_ring));
    memset(s_centered, 0, sizeof(s_centered));

    s_storm_adc.timer_reserved = true;
    s_storm_adc.timer_ch = timer_ch;
    s_storm_adc.adc_ch = adc_ch;

    s_storm_status.pin = pin;
    s_storm_status.sample_rate_hz = sample_rate_hz;
    s_storm_status.frame_samples = (uint16_t)frame_samples;
    s_storm_status.ring_samples = (uint16_t)(frame_samples * 2U);
    s_storm_status.active_half = 0U;

    ra_adc_enable(pin);
    s_storm_adc.pin_enabled = true;

    agt_event = ra_storm_agt_event(s_storm_adc.timer_ch);
    if (agt_event == ELC_EVENT_NONE) {
        ra_storm_adc_deinit();
        return false;
    }

    ra_agt_timer_init(s_storm_adc.timer_ch, (float)sample_rate_hz);
    R_BSP_IrqDisable((IRQn_Type)(VECTOR_NUMBER_AGT0_INT + s_storm_adc.timer_ch));
    R_BSP_IrqStatusClear((IRQn_Type)(VECTOR_NUMBER_AGT0_INT + s_storm_adc.timer_ch));

    s_storm_adc.adc_cfg = g_adc0_cfg;
    s_storm_adc.adc_cfg_extend = *(adc_extended_cfg_t *)g_adc0_cfg.p_extend;
    s_storm_adc.adc_channel_cfg = g_adc0_channel_cfg;
    s_storm_adc.adc_cfg.trigger = ADC_TRIGGER_SYNC_ELC;
    s_storm_adc.adc_cfg.p_callback = NULL;
    s_storm_adc.adc_cfg.p_context = NULL;
    s_storm_adc.adc_cfg.scan_end_irq = VECTOR_NUMBER_ADC0_SCAN_END;
    s_storm_adc.adc_cfg.scan_end_ipl = BSP_IRQ_DISABLED;
    s_storm_adc.adc_cfg.scan_end_b_irq = FSP_INVALID_VECTOR;
    s_storm_adc.adc_cfg.scan_end_b_ipl = BSP_IRQ_DISABLED;
    s_storm_adc.adc_channel_cfg.scan_mask = (1UL << adc_ch);
    s_storm_adc.adc_channel_cfg.scan_mask_group_b = 0U;

    if (R_ADC_Open((adc_ctrl_t *)&s_storm_adc.adc_ctrl, &s_storm_adc.adc_cfg) != FSP_SUCCESS) {
        ra_storm_adc_deinit();
        return false;
    }
    s_storm_adc.opened = true;
    if (R_ADC_ScanCfg((adc_ctrl_t *)&s_storm_adc.adc_ctrl, &s_storm_adc.adc_channel_cfg) != FSP_SUCCESS) {
        ra_storm_adc_deinit();
        return false;
    }

    memset(&s_storm_adc.dtc_info, 0, sizeof(s_storm_adc.dtc_info));
    memset(&s_storm_adc.dtc_cfg, 0, sizeof(s_storm_adc.dtc_cfg));
    memset(&s_storm_adc.dtc_ext, 0, sizeof(s_storm_adc.dtc_ext));

    s_storm_adc.dtc_info.transfer_settings_word = 0U;
    s_storm_adc.dtc_info.transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    s_storm_adc.dtc_info.transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION;
    s_storm_adc.dtc_info.transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    s_storm_adc.dtc_info.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    s_storm_adc.dtc_info.transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    s_storm_adc.dtc_info.transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
    s_storm_adc.dtc_info.transfer_settings_word_b.mode = TRANSFER_MODE_REPEAT;
    s_storm_adc.dtc_info.p_src = (void *)&R_ADC0->ADDR[adc_ch];
    s_storm_adc.dtc_info.p_dest = s_raw_ring;
    s_storm_adc.dtc_info.num_blocks = 0U;
    s_storm_adc.dtc_info.length = s_storm_status.ring_samples;

    s_storm_adc.dtc_ext.activation_source = VECTOR_NUMBER_ADC0_SCAN_END;
    s_storm_adc.dtc_cfg.p_info = &s_storm_adc.dtc_info;
    s_storm_adc.dtc_cfg.p_extend = &s_storm_adc.dtc_ext;

    if (R_DTC_Open((transfer_ctrl_t *)&s_storm_adc.dtc_ctrl, &s_storm_adc.dtc_cfg) != FSP_SUCCESS) {
        ra_storm_adc_deinit();
        return false;
    }
    s_storm_adc.dtc_open = true;
    if (R_DTC_Enable((transfer_ctrl_t *)&s_storm_adc.dtc_ctrl) != FSP_SUCCESS) {
        ra_storm_adc_deinit();
        return false;
    }

    R_BSP_IrqDisable(VECTOR_NUMBER_ADC0_SCAN_END);
    R_BSP_IrqStatusClear(VECTOR_NUMBER_ADC0_SCAN_END);
    ra_storm_elc_enable(agt_event);
    s_storm_adc.elc_enabled = true;
    s_storm_status.initialised = 1U;
    return true;
}

bool ra_storm_adc_deinit_checked(void) {
    /* This producer owns both an AGT and the ADC0_SCAN_END DTC activation
     * vector.  Confirm the clock is stopped before releasing either resource. */
    if (s_storm_adc.timer_reserved &&
        !ra_agt_timer_stop_wait(s_storm_adc.timer_ch)) {
        return false;
    }
    if (s_storm_adc.opened && s_storm_status.running) {
        if (R_ADC_ScanStop((adc_ctrl_t *)&s_storm_adc.adc_ctrl) != FSP_SUCCESS) {
            return false;
        }
    }
    s_storm_status.running = 0U;

    if (s_storm_adc.elc_enabled) {
        ra_storm_elc_disable();
        s_storm_adc.elc_enabled = false;
    }

    if (s_storm_adc.dtc_open) {
        if (R_DTC_Disable((transfer_ctrl_t *)&s_storm_adc.dtc_ctrl) != FSP_SUCCESS) {
            return false;
        }
        if (R_DTC_Close((transfer_ctrl_t *)&s_storm_adc.dtc_ctrl) != FSP_SUCCESS) {
            return false;
        }
        s_storm_adc.dtc_open = false;
    }

    if (s_storm_adc.opened) {
        if (R_ADC_Close((adc_ctrl_t *)&s_storm_adc.adc_ctrl) != FSP_SUCCESS) {
            return false;
        }
        s_storm_adc.opened = false;
    }
    if (s_storm_adc.timer_reserved) {
        if (!ra_agt_timer_deinit_checked(s_storm_adc.timer_ch)) {
            return false;
        }
        s_storm_adc.timer_reserved = false;
    }
    if (s_storm_adc.pin_enabled) {
        ra_adc_disable(s_storm_status.pin);
        s_storm_adc.pin_enabled = false;
    }

    memset(&s_storm_adc, 0, sizeof(s_storm_adc));
    memset(&s_storm_status, 0, sizeof(s_storm_status));
    return true;
}

void ra_storm_adc_deinit(void) {
    (void)ra_storm_adc_deinit_checked();
}

bool ra_storm_adc_start(void) {
    if (!s_storm_status.initialised) {
        return false;
    }

    memset(s_raw_ring, 0, sizeof(s_raw_ring));
    memset(s_centered, 0, sizeof(s_centered));
    memset(s_storm_adc.half_sequence, 0, sizeof(s_storm_adc.half_sequence));

    s_storm_status.running = 1U;
    s_storm_status.overflow = 0U;
    s_storm_status.ready_mask = 0U;
    s_storm_status.write_index = 0U;
    s_storm_status.active_half = 0U;
    s_storm_status.frame_sequence = 0U;
    s_storm_status.dropped_frames = 0U;
    s_storm_status.last_raw = 0U;

    R_DTC_Disable((transfer_ctrl_t *)&s_storm_adc.dtc_ctrl);
    R_BSP_IrqStatusClear(VECTOR_NUMBER_ADC0_SCAN_END);
    if (R_DTC_Reset((transfer_ctrl_t *)&s_storm_adc.dtc_ctrl, (void *)&R_ADC0->ADDR[s_storm_adc.adc_ch], s_raw_ring,
        s_storm_status.ring_samples) != FSP_SUCCESS) {
        s_storm_status.running = 0U;
        return false;
    }
    if (R_DTC_Enable((transfer_ctrl_t *)&s_storm_adc.dtc_ctrl) != FSP_SUCCESS) {
        s_storm_status.running = 0U;
        return false;
    }

    ra_agt_timer_start(s_storm_adc.timer_ch);
    return true;
}

void ra_storm_adc_stop(void) {
    if (s_storm_status.running) {
        ra_agt_timer_stop(s_storm_adc.timer_ch);
        s_storm_status.running = 0U;
    }
}

bool ra_storm_adc_ready(void) {
    ra_storm_update_ready_state();
    return s_storm_status.ready_mask != 0U;
}

const int16_t *ra_storm_adc_acquire_ready_buffer(size_t *frame_samples, uint32_t *sequence) {
    ra_storm_update_ready_state();

    if (s_storm_status.ready_mask == 0U) {
        return NULL;
    }

    uint8_t half = 0U;
    if (s_storm_status.ready_mask == 0x03U) {
        half = (s_storm_adc.half_sequence[0] <= s_storm_adc.half_sequence[1]) ? 0U : 1U;
    } else if ((s_storm_status.ready_mask & 0x01U) == 0U) {
        half = 1U;
    }

    uint16_t *src = &s_raw_ring[half * s_storm_status.frame_samples];
    for (size_t i = 0; i < s_storm_status.frame_samples; ++i) {
        s_centered[half][i] = (int16_t)((int32_t)src[i] - 2048);
    }

    s_storm_status.last_raw = src[s_storm_status.frame_samples - 1U];
    s_storm_status.ready_mask &= (uint8_t)~(1U << half);

    if (frame_samples != NULL) {
        *frame_samples = s_storm_status.frame_samples;
    }
    if (sequence != NULL) {
        *sequence = s_storm_adc.half_sequence[half];
    }
    return s_centered[half];
}

void ra_storm_adc_get_status(ra_storm_adc_status_t *status) {
    if (status != NULL) {
        ra_storm_update_ready_state();
        *status = s_storm_status;
    }
}
