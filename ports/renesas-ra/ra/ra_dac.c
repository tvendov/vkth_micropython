/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Vekatech Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* FSP has available API for DAC (r_dac)

R_DAC_Open ( ... )
R_DAC_Stop ( ... )
R_DAC_Start ( ... )
R_DAC_Write ( ... )
R_DAC_Close ( ... )

and this is (The Lazy way)

   ... but looking to other drivers implementation (for example AGT [ra_timer.c/h]), Renesas want to be hard, so ...

   (The Hard way it is)
*/

#include <string.h>

#include "hal_data.h"
#include "r_dmac.h"
#include "r_dtc.h"
#include "ra_config.h"
#include "ra_dac.h"
#include "ra_gpio.h"
#include "ra_sci.h"
#include "ra_timer.h"
#include "ra_utils.h"
#include "vector_data.h"

#define RA_DAC_OUTPUT_AMP_DELAY_US (4U)


#if defined(RA4M2)
#define DAC_CH_SIZE 2
#elif defined(RA4M1) || defined(RA4W1)
#define DAC_CH_SIZE 1
#elif defined(RA6M1) || defined(RA6M2) || defined(RA6M3) || defined(RA6M5)
#define DAC_CH_SIZE 2
#else
#error "CMSIS MCU Series is not specified."
#endif

#define DAC_PINS_SIZE sizeof(ra_dac_pins) / sizeof(ra_af_pin_t)

static const ra_af_pin_t ra_dac_pins[] = {
    #if defined(RA4M1) || defined(RA4W1)
    { AF_GPIO, 0, P014 }, // (A3)
    #elif defined(RA4M2)
    { AF_GPIO, 0, P014 }, // DA0
    { AF_GPIO, 1, P015 }, // DA1
    #elif defined(RA6M1) || defined(RA6M2) || defined(RA6M3) || defined(RA6M5)
    { AF_GPIO, 0, P014 }, // (A4)
    { AF_GPIO, 1, P015 }, // (A5)
    #else
    #error "CMSIS MCU Series is not specified."
    #endif
};

typedef struct _ra_dac_stream_state_t {
    bool active;
    bool loop;
    bool double_buffered;
    bool timer_reserved;
    bool timer_initialized;
    bool transfer_open;
    bool dmac_reserved;
    bool agt_iels_detached;
    uint8_t timer_ch;
    uint8_t dmac_ch;
    uint32_t freq;
    uint32_t agt_iels_saved;
    IRQn_Type agt_irq;
    ra_dac_transfer_t transfer;
    transfer_info_t info;
    transfer_cfg_t cfg;
    dmac_instance_ctrl_t dmac_ctrl;
    dmac_extended_cfg_t dmac_ext;
    dtc_instance_ctrl_t dtc_ctrl;
    dtc_extended_cfg_t dtc_ext;
    uint16_t *buffers[2];
    bool buffer_ready[2];
    size_t buffer_sample_count;
    uint8_t active_buffer;
    ra_dac_stream_double_buffer_fill_t buffer_fill;
    ra_dac_stream_double_buffer_stop_t buffer_stop;
    void *buffer_context;
} ra_dac_stream_state_t;

static ra_dac_stream_state_t ra_dac_stream_state[DAC_CH_SIZE];
static ra_dac_hw_stage_t ra_dac_last_stage[DAC_CH_SIZE];
static int32_t ra_dac_last_error[DAC_CH_SIZE];
static void ra_dac_stream_cleanup(uint8_t ch);

static void ra_dac_output_amp_init(uint8_t ch) {
#if BSP_FEATURE_DAC_HAS_OUTPUT_AMPLIFIER
    if (ch >= DAC_CH_SIZE) {
        return;
    }

    uint16_t value = R_DAC->DADR[ch];
    R_DAC->DADR[ch] = 0U;

    if (ch == 0U) {
        R_DAC->DACR_b.DAOE0 = 0U;
        R_DAC->DAASWCR_b.DAASW0 = 1U;
        R_DAC->DAAMPCR_b.DAAMP0 = 1U;
        R_DAC->DACR_b.DAOE0 = 1U;
    } else {
        R_DAC->DACR_b.DAOE1 = 0U;
        R_DAC->DAASWCR_b.DAASW1 = 1U;
        R_DAC->DAAMPCR_b.DAAMP1 = 1U;
        R_DAC->DACR_b.DAOE1 = 1U;
    }

    R_BSP_SoftwareDelay(RA_DAC_OUTPUT_AMP_DELAY_US, BSP_DELAY_UNITS_MICROSECONDS);

    if (ch == 0U) {
        R_DAC->DAASWCR_b.DAASW0 = 0U;
    } else {
        R_DAC->DAASWCR_b.DAASW1 = 0U;
    }

    R_DAC->DADR[ch] = value;
#else
    (void)ch;
#endif
}

static void ra_dac_set_last_error(uint8_t ch, ra_dac_hw_stage_t stage, fsp_err_t err) {
    if (ch < DAC_CH_SIZE) {
        ra_dac_last_stage[ch] = stage;
        ra_dac_last_error[ch] = (int32_t)err;
    }
}

static void ra_dac_set_pin(uint32_t pin) {
    bool find = false;
    uint32_t ch;
    uint32_t af;
    find = ra_af_find_ch_af((ra_af_pin_t *)&ra_dac_pins, DAC_PINS_SIZE, pin, &ch, &af);
    if (find) {
        ra_gpio_config(pin, GPIO_MODE_ANALOG, GPIO_NOPULL, GPIO_LOW_POWER, af);
    }
}

static void ra_dac_release_pin(uint32_t pin) {
    bool find = false;
    uint32_t ch;
    uint32_t af;
    find = ra_af_find_ch_af((ra_af_pin_t *)&ra_dac_pins, DAC_PINS_SIZE, pin, &ch, &af);
    if (find) {
        ra_gpio_config(pin, GPIO_MODE_ANALOG, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
    }
}

bool ra_dac_is_dac_pin(uint32_t pin) {
    uint32_t ch;
    uint32_t af;
    return ra_af_find_ch_af((ra_af_pin_t *)&ra_dac_pins, DAC_PINS_SIZE, pin, &ch, &af);
}

static IRQn_Type ra_dac_agt_irq(uint32_t ch) {
    switch (ch) {
        #if defined(VECTOR_NUMBER_AGT0_INT)
        case 0:
            return VECTOR_NUMBER_AGT0_INT;
        #endif
        #if defined(VECTOR_NUMBER_AGT1_INT)
        case 1:
            return VECTOR_NUMBER_AGT1_INT;
        #endif
        #if defined(VECTOR_NUMBER_AGT2_INT)
        case 2:
            return VECTOR_NUMBER_AGT2_INT;
        #endif
        #if defined(VECTOR_NUMBER_AGT3_INT)
        case 3:
            return VECTOR_NUMBER_AGT3_INT;
        #endif
        #if defined(VECTOR_NUMBER_AGT4_INT)
        case 4:
            return VECTOR_NUMBER_AGT4_INT;
        #endif
        #if defined(VECTOR_NUMBER_AGT5_INT)
        case 5:
            return VECTOR_NUMBER_AGT5_INT;
        #endif
        default:
            return FSP_INVALID_VECTOR;
    }
}

static elc_event_t ra_dac_agt_event(uint32_t ch) {
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

static IRQn_Type ra_dac_dmac_irq(uint32_t ch) {
    switch (ch) {
        #if defined(VECTOR_NUMBER_DMAC0_INT)
        case 0:
            return VECTOR_NUMBER_DMAC0_INT;
        #endif
        #if defined(VECTOR_NUMBER_DMAC1_INT)
        case 1:
            return VECTOR_NUMBER_DMAC1_INT;
        #endif
        #if defined(VECTOR_NUMBER_DMAC2_INT)
        case 2:
            return VECTOR_NUMBER_DMAC2_INT;
        #endif
        #if defined(VECTOR_NUMBER_DMAC3_INT)
        case 3:
            return VECTOR_NUMBER_DMAC3_INT;
        #endif
        #if defined(VECTOR_NUMBER_DMAC4_INT)
        case 4:
            return VECTOR_NUMBER_DMAC4_INT;
        #endif
        #if defined(VECTOR_NUMBER_DMAC5_INT)
        case 5:
            return VECTOR_NUMBER_DMAC5_INT;
        #endif
        #if defined(VECTOR_NUMBER_DMAC6_INT)
        case 6:
            return VECTOR_NUMBER_DMAC6_INT;
        #endif
        #if defined(VECTOR_NUMBER_DMAC7_INT)
        case 7:
            return VECTOR_NUMBER_DMAC7_INT;
        #endif
        default:
            return FSP_INVALID_VECTOR;
    }
}

static bool ra_dac_dmac_channel_acquire(uint8_t *channel) {
    for (uint8_t ch = 0; ch < BSP_FEATURE_DMAC_MAX_CHANNEL; ++ch) {
        if (ra_dmac_reserve(ch)) {
            *channel = ch;
            return true;
        }
    }
    return false;
}

static void ra_dac_dmac_channel_release(uint8_t channel) {
    if (channel < BSP_FEATURE_DMAC_MAX_CHANNEL) {
        ra_dmac_release(channel);
    }
}

static void ra_dac_dmac_error_clear(void) {
    if (R_DMA->DMECHR_b.DMESTA != 0U) {
        R_DMA->DMECHR = R_DMA_DMECHR_DMESTA_Msk;
    }
    if (R_BUS->BUS3ERRSTAT != 0U) {
        R_BUS->BUS3ERRCLR = (uint8_t)(R_BUS->BUS3ERRSTAT &
            (R_BUS_B_BUS3ERRCLR_SLERRCLR_Msk |
             R_BUS_B_BUS3ERRCLR_STERRCLR_Msk |
             R_BUS_B_BUS3ERRCLR_MMERRCLR_Msk |
             R_BUS_B_BUS3ERRCLR_ILERRCLR_Msk));
    }
    if (R_BUS->DMACDTCERRSTAT_b.MTERRSTAT != 0U) {
        R_BUS->DMACDTCERRCLR = R_BUS_B_DMACDTCERRCLR_MTERRCLR_Msk;
    }
}

static void ra_dac_dmac_make_channel_secure(uint8_t channel) {
    uint32_t mask = 1UL << channel;

    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_SAR);
    R_CPSCU->ICUSARC = (R_CPSCU->ICUSARC | ~R_CPSCU_ICUSARC_SADMACn_Msk) & ~mask;
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_SAR);
}

static int32_t ra_dac_dmac_runtime_diag(uint8_t dmac_ch) {
    uint32_t diag = 0U;

    diag |= (R_DMA->DMECHR & 0x0001FFFFUL);
    diag |= ((uint32_t)R_BUS->BUS3ERRSTAT & 0xFFUL) << 17;
    diag |= ((uint32_t)R_BUS->DMACDTCERRSTAT & 0xFFUL) << 25;
    diag |= ((uint32_t)dmac_ch & 0x7UL) << 29;
    return (int32_t)diag;
}

static void ra_dac_stream_cleanup(uint8_t ch) {
    if (ch >= DAC_CH_SIZE) {
        return;
    }

    ra_dac_stream_state_t *state = &ra_dac_stream_state[ch];
    bool was_active = state->active;
    bool notify_double_buffer_stop = state->double_buffered && state->buffer_stop != NULL;
    ra_dac_stream_double_buffer_stop_t stop_cb = state->buffer_stop;
    void *stop_context = state->buffer_context;
    if (state->timer_initialized) {
        ra_agt_timer_set_callback(state->timer_ch, NULL, NULL);
    }
    if (state->active && state->timer_initialized) {
        ra_agt_timer_stop(state->timer_ch);
    }

    if (state->transfer_open) {
        if (state->transfer == RA_DAC_TRANSFER_DMAC) {
            R_DMAC_Disable((transfer_ctrl_t *)&state->dmac_ctrl);
            R_DMAC_Close((transfer_ctrl_t *)&state->dmac_ctrl);
        } else if (state->transfer == RA_DAC_TRANSFER_DTC) {
            R_DTC_Disable((transfer_ctrl_t *)&state->dtc_ctrl);
            R_DTC_Close((transfer_ctrl_t *)&state->dtc_ctrl);
        }
    }

    if (state->timer_initialized) {
        ra_agt_timer_deinit(state->timer_ch);
    } else if (state->timer_reserved) {
        ra_agt_timer_release_reservation(state->timer_ch);
    }

    if (state->agt_iels_detached && state->agt_irq >= (IRQn_Type)0) {
        R_ICU->IELSR[state->agt_irq] = state->agt_iels_saved;
        FSP_REGISTER_READ(R_ICU->IELSR[state->agt_irq]);
    }

    if (state->dmac_reserved) {
        ra_dac_dmac_channel_release(state->dmac_ch);
    }

    memset(state, 0, sizeof(*state));

    if (was_active && notify_double_buffer_stop) {
        stop_cb(stop_context);
    }
}

static void ra_dac_elc_enable(void) {
    // DMAC peripheral/IRQ activation on RA4M2 uses the event-link fabric even
    // though the route is programmed through ICU.DELSR.
    ra_mstpcrc_start(R_MSTP_MSTPCRC_MSTPC14_Msk);
    R_ELC->ELCR = R_ELC_ELCR_ELCON_Msk;
}

static void ra_dac_dmac_detach_agt_iels(ra_dac_stream_state_t *state) {
    IRQn_Type agt_irq = ra_dac_agt_irq(state->timer_ch);
    if (agt_irq < (IRQn_Type)0) {
        return;
    }

    R_BSP_IrqDisable(agt_irq);
    R_BSP_IrqStatusClear(agt_irq);
    state->agt_irq = agt_irq;
    state->agt_iels_saved = R_ICU->IELSR[agt_irq];
    R_ICU->IELSR[agt_irq] = 0U;
    FSP_REGISTER_READ(R_ICU->IELSR[agt_irq]);
    state->agt_iels_detached = true;
}

static void ra_dac_dtc_complete_callback(void *param) {
    uint8_t ch = (uint8_t)(uintptr_t)param;
    if (ch >= DAC_CH_SIZE) {
        return;
    }

    ra_dac_stream_state_t *state = &ra_dac_stream_state[ch];
    if (state->active && !state->loop && state->transfer == RA_DAC_TRANSFER_DTC) {
        ra_dac_stream_cleanup(ch);
    }
}

static void ra_dac_dmac_complete_callback(dmac_callback_args_t *args) {
    if (args == NULL) {
        return;
    }

    uint8_t ch = (uint8_t)(uintptr_t)args->p_context;
    if (ch >= DAC_CH_SIZE) {
        return;
    }

    ra_dac_stream_state_t *state = &ra_dac_stream_state[ch];
    if (!state->active || state->transfer != RA_DAC_TRANSFER_DMAC) {
        return;
    }

    if (state->double_buffered) {
        uint8_t completed = state->active_buffer;
        uint8_t next = completed ^ 1U;

        if (!state->buffer_ready[next] || state->buffers[next] == NULL) {
            ra_dac_stream_cleanup(ch);
            return;
        }

        fsp_err_t err = R_DMAC_Reset((transfer_ctrl_t *)&state->dmac_ctrl, state->buffers[next],
            (void *)&R_DAC->DADR[ch], (uint16_t)state->buffer_sample_count);
        if (err != FSP_SUCCESS) {
            ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_DMAC_RUNTIME, err);
            ra_dac_stream_cleanup(ch);
            return;
        }

        state->active_buffer = next;
        state->buffer_ready[next] = false;

        if (state->buffer_fill != NULL && state->buffers[completed] != NULL) {
            state->buffer_ready[completed] = state->buffer_fill(state->buffer_context,
                state->buffers[completed], state->buffer_sample_count);
        } else {
            state->buffer_ready[completed] = false;
        }
        return;
    }

    if (state->loop) {
        /* Re-arm DMCRB so FSP ISR re-enables the channel (DMCNT=1).
         * This callback runs inside the DMAC ISR, *before* the FSP
         * code checks DMCRB, so writing it here keeps the transfer
         * running seamlessly. */
        state->dmac_ctrl.p_reg->DMCRB = UINT16_MAX;
    } else {
        ra_dac_stream_cleanup(ch);
    }
}

static bool ra_dac_timer_reserve(int8_t requested, uint8_t *timer_ch) {
    if (requested >= 0) {
        if (!ra_agt_timer_is_valid((uint32_t)requested)) {
            return false;
        }
        if (!ra_agt_timer_reserve((uint32_t)requested)) {
            return false;
        }
        *timer_ch = (uint8_t)requested;
        return true;
    }

    for (uint32_t ch = 0; ch <= BSP_FEATURE_AGT_MAX_CHANNEL_NUM; ++ch) {
        if (ra_agt_timer_reserve(ch)) {
            *timer_ch = (uint8_t)ch;
            return true;
        }
    }

    return false;
}

static bool ra_dac_stream_start_transfer(uint8_t ch, const uint16_t *buf, size_t sample_count, bool loop) {
    ra_dac_stream_state_t *state = &ra_dac_stream_state[ch];
    IRQn_Type agt_irq = ra_dac_agt_irq(state->timer_ch);

    memset(&state->info, 0, sizeof(state->info));
    memset(&state->cfg, 0, sizeof(state->cfg));
    state->info.transfer_settings_word = 0;
    state->info.transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    state->info.transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_SOURCE;
    state->info.transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    state->info.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    state->info.transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    state->info.transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
    state->info.transfer_settings_word_b.mode = loop ? TRANSFER_MODE_REPEAT : TRANSFER_MODE_NORMAL;
    state->info.p_src = buf;
    state->info.p_dest = (void *)&R_DAC->DADR[ch];
    /* For DMAC repeat mode, num_blocks sets the number of repeats.
     * Use UINT16_MAX (65535) so the hardware runs ~65k cycles before
     * the completion ISR re-arms it — effectively infinite looping
     * with zero software gap.  DTC repeat ignores num_blocks. */
    state->info.num_blocks = (loop && state->transfer == RA_DAC_TRANSFER_DMAC)
        ? UINT16_MAX : 0;
    state->info.length = (uint16_t)sample_count;

    if (agt_irq >= (IRQn_Type)0) {
        R_BSP_IrqDisable(agt_irq);
        R_BSP_IrqStatusClear(agt_irq);
    }

    state->cfg.p_info = &state->info;

    if (state->transfer == RA_DAC_TRANSFER_DMAC) {
        memset(&state->dmac_ctrl, 0, sizeof(state->dmac_ctrl));
        memset(&state->dmac_ext, 0, sizeof(state->dmac_ext));
        if (!ra_dac_dmac_channel_acquire(&state->dmac_ch)) {
            return false;
        }

        state->dmac_reserved = true;
        state->dmac_ext.channel = state->dmac_ch;
        state->dmac_ext.irq = FSP_INVALID_VECTOR;
        state->dmac_ext.ipl = 0;
        state->dmac_ext.activation_source = ra_dac_agt_event(state->timer_ch);
        {
            IRQn_Type dmac_irq = ra_dac_dmac_irq(state->dmac_ch);
            if (state->double_buffered && dmac_irq < (IRQn_Type)0) {
                ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_EVENT_MAP, FSP_ERR_UNSUPPORTED);
                return false;
            }
            if (dmac_irq >= (IRQn_Type)0) {
                state->dmac_ext.irq = dmac_irq;
                state->dmac_ext.ipl = 1;
                state->dmac_ext.p_callback = ra_dac_dmac_complete_callback;
                state->dmac_ext.p_context = (void *)(uintptr_t)ch;
                if (loop) {
                    /* For DMAC repeat loop we need TRANSFER_IRQ_EACH so the
                     * ISR fires every num_blocks repeats, giving our callback
                     * a chance to re-arm DMCRB for infinite playback. */
                    state->info.transfer_settings_word_b.irq = TRANSFER_IRQ_EACH;
                }
            }
        }
        if (state->dmac_ext.activation_source == ELC_EVENT_NONE) {
            ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_EVENT_MAP, FSP_ERR_UNSUPPORTED);
            return false;
        }

        ra_dac_dmac_detach_agt_iels(state);
        ra_dac_dmac_make_channel_secure(state->dmac_ch);
        ra_dac_elc_enable();
        ra_dac_dmac_error_clear();
        state->cfg.p_extend = &state->dmac_ext;
        fsp_err_t err = R_DMAC_Open((transfer_ctrl_t *)&state->dmac_ctrl, &state->cfg);
        if (err != FSP_SUCCESS) {
            ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_DMAC_OPEN, err);
            return false;
        }
        state->transfer_open = true;
        err = R_DMAC_Enable((transfer_ctrl_t *)&state->dmac_ctrl);
        if (err != FSP_SUCCESS) {
            ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_DMAC_ENABLE, err);
            return false;
        }
    } else {
        memset(&state->dtc_ctrl, 0, sizeof(state->dtc_ctrl));
        memset(&state->dtc_ext, 0, sizeof(state->dtc_ext));
        if (agt_irq < (IRQn_Type)0) {
            ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_EVENT_MAP, FSP_ERR_UNSUPPORTED);
            return false;
        }

        state->dtc_ext.activation_source = agt_irq;
        state->cfg.p_extend = &state->dtc_ext;
        fsp_err_t err = R_DTC_Open((transfer_ctrl_t *)&state->dtc_ctrl, &state->cfg);
        if (err != FSP_SUCCESS) {
            ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_DTC_OPEN, err);
            return false;
        }
        state->transfer_open = true;
        err = R_DTC_Enable((transfer_ctrl_t *)&state->dtc_ctrl);
        if (err != FSP_SUCCESS) {
            ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_DTC_ENABLE, err);
            return false;
        }
    }

    return true;
}

uint8_t ra_dac_is_running(uint8_t ch) {
    if (ch < DAC_CH_SIZE) {
        return ch? R_DAC->DACR_b.DAOE1 : R_DAC->DACR_b.DAOE0;
    } else {
        return 0;
    }
}

void ra_dac_start(uint8_t ch) {
    if (ch < DAC_CH_SIZE) {
#if BSP_FEATURE_DAC_HAS_OUTPUT_AMPLIFIER
        ra_dac_output_amp_init(ch);
#else
        if (ch) {
            R_DAC->DACR_b.DAOE1 = 1U;
        } else {
            R_DAC->DACR_b.DAOE0 = 1U;
        }
#endif
    }
}

void ra_dac_stop(uint8_t ch) {
    if (ch < DAC_CH_SIZE) {
        if (ch) {
            R_DAC->DACR_b.DAOE1 = 0U;
        } else {
            R_DAC->DACR_b.DAOE0 = 0U;
        }
    }
}

void ra_dac_write(uint8_t ch, uint16_t val) {
    if ((ch < DAC_CH_SIZE) && (val < 4096)) {
        R_DAC->DADR[ch] = val;
    }
}

uint16_t ra_dac_read(uint8_t ch) {
    if (ch < DAC_CH_SIZE) {
        return R_DAC->DADR[ch];
    } else {
        return 0;
    }
}

void ra_dac_init(uint32_t dac_pin, uint8_t ch) {
    if (ch < DAC_CH_SIZE) {
        ra_mstpcrd_start(R_MSTP_MSTPCRD_MSTPD20_Msk);

        R_DAC->DADPR_b.DPSEL = 0;    // Right-justified format
        R_DAC->DAADSCR_b.DAADST = 0;  // Do not synchronize with ADC14
        R_DAC->DADR[ch] = 0;         // Output 0 Volts

#if BSP_FEATURE_DAC_HAS_DAVREFCR
        R_DAC->DAVREFCR_b.REF = 1;   // AVCC0/AVSS0 selected
#endif

        ra_dac_set_pin(dac_pin);
        if (!ra_dac_is_running(ch)) {
            ra_dac_start(ch);
        }
    }
}

void ra_dac_deinit(uint32_t dac_pin, uint8_t ch) {
    if (ch < DAC_CH_SIZE) {
        ra_dac_stream_cleanup(ch);
        ra_dac_stop(ch);
        ra_dac_release_pin(dac_pin);

#if BSP_FEATURE_DAC_HAS_OUTPUT_AMPLIFIER
        if (ch == 0U) {
            R_DAC->DAAMPCR_b.DAAMP0 = 0U;
        } else {
            R_DAC->DAAMPCR_b.DAAMP1 = 0U;
        }
#endif

        // Only fully power down the DAC block when no channel is running.
        bool any_running = false;
        for (uint8_t i = 0; i < DAC_CH_SIZE; i++) {
            if (ra_dac_is_running(i)) {
                any_running = true;
                break;
            }
        }
        if (!any_running) {
#if BSP_FEATURE_DAC_HAS_DAVREFCR
            R_DAC->DAVREFCR_b.REF = 0;   // No reference voltage selected
#endif
            ra_mstpcrd_stop(R_MSTP_MSTPCRD_MSTPD20_Msk);
        }
    }
}

ra_dac_stream_status_t ra_dac_write_timed(uint8_t ch, const uint16_t *buf, size_t sample_count, uint32_t freq,
    bool loop, ra_dac_transfer_t transfer, int8_t timer_ch) {
    if (ch >= DAC_CH_SIZE) {
        return RA_DAC_STREAM_STATUS_INVALID_CHANNEL;
    }
    if (buf == NULL || sample_count == 0U) {
        return RA_DAC_STREAM_STATUS_INVALID_LENGTH;
    }
    if (freq == 0U) {
        return RA_DAC_STREAM_STATUS_INVALID_FREQ;
    }

    if (transfer == RA_DAC_TRANSFER_AUTO) {
        if (!loop) {
            transfer = RA_DAC_TRANSFER_DMAC;
        } else if (sample_count <= DTC_MAX_REPEAT_TRANSFER_LENGTH) {
            transfer = RA_DAC_TRANSFER_DTC;
        } else {
            transfer = RA_DAC_TRANSFER_DMAC;
        }
    }

    if (loop) {
        if (transfer == RA_DAC_TRANSFER_DTC && sample_count > DTC_MAX_REPEAT_TRANSFER_LENGTH) {
            return RA_DAC_STREAM_STATUS_LOOP_UNSUPPORTED;
        }
        if (transfer == RA_DAC_TRANSFER_DMAC && sample_count > DMAC_MAX_REPEAT_TRANSFER_LENGTH) {
            return RA_DAC_STREAM_STATUS_INVALID_LENGTH;
        }
    } else {
        if (transfer == RA_DAC_TRANSFER_DMAC && sample_count > DMAC_MAX_NORMAL_TRANSFER_LENGTH) {
            return RA_DAC_STREAM_STATUS_INVALID_LENGTH;
        }
        if (transfer == RA_DAC_TRANSFER_DTC && sample_count > UINT16_MAX) {
            return RA_DAC_STREAM_STATUS_INVALID_LENGTH;
        }
    }

    if (timer_ch >= 0 && !ra_agt_timer_is_valid((uint32_t)timer_ch)) {
        return RA_DAC_STREAM_STATUS_INVALID_TIMER;
    }

    ra_dac_stream_cleanup(ch);
    ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_NONE, FSP_SUCCESS);

    ra_dac_stream_state_t *state = &ra_dac_stream_state[ch];
    state->transfer = transfer;
    state->loop = loop;
    state->freq = freq;

    if (!ra_dac_timer_reserve(timer_ch, &state->timer_ch)) {
        return RA_DAC_STREAM_STATUS_TIMER_BUSY;
    }
    state->timer_reserved = true;

    ra_agt_timer_init(state->timer_ch, (float)freq);
    state->timer_initialized = true;
    if (!loop && transfer == RA_DAC_TRANSFER_DTC) {
        ra_agt_timer_set_callback(state->timer_ch, ra_dac_dtc_complete_callback, (void *)(uintptr_t)ch);
    }
    if (ra_agt_timer_get_freq(state->timer_ch) <= 0.0f) {
        ra_dac_stream_cleanup(ch);
        return RA_DAC_STREAM_STATUS_INVALID_FREQ;
    }

    if (!ra_dac_stream_start_transfer(ch, buf, sample_count, loop)) {
        ra_dac_stream_status_t err =
            (transfer == RA_DAC_TRANSFER_DMAC && !state->dmac_reserved) ? RA_DAC_STREAM_STATUS_TRANSFER_BUSY : RA_DAC_STREAM_STATUS_HW_ERROR;
        ra_dac_stream_cleanup(ch);
        return err;
    }

    if (!ra_dac_is_running(ch)) {
        ra_dac_start(ch);
    }

    IRQn_Type agt_irq = ra_dac_agt_irq(state->timer_ch);
    if (agt_irq >= (IRQn_Type)0) {
        R_BSP_IrqStatusClear(agt_irq);
    }
    state->active = true;
    ra_agt_timer_start(state->timer_ch);
    if (!loop && transfer == RA_DAC_TRANSFER_DTC && agt_irq >= (IRQn_Type)0) {
        R_BSP_IrqEnable(agt_irq);
    }

    return RA_DAC_STREAM_STATUS_OK;
}

ra_dac_stream_status_t ra_dac_write_timed_double_buffered(uint8_t ch, uint16_t *buf_a, uint16_t *buf_b,
    bool buf_b_ready, size_t sample_count, uint32_t freq, ra_dac_stream_double_buffer_fill_t fill_cb,
    ra_dac_stream_double_buffer_stop_t stop_cb, void *context, int8_t timer_ch) {
    if (ch >= DAC_CH_SIZE) {
        return RA_DAC_STREAM_STATUS_INVALID_CHANNEL;
    }
    if (buf_a == NULL || buf_b == NULL || sample_count == 0U) {
        return RA_DAC_STREAM_STATUS_INVALID_LENGTH;
    }
    if (freq == 0U) {
        return RA_DAC_STREAM_STATUS_INVALID_FREQ;
    }
    if (sample_count > DMAC_MAX_NORMAL_TRANSFER_LENGTH) {
        return RA_DAC_STREAM_STATUS_INVALID_LENGTH;
    }
    if (timer_ch >= 0 && !ra_agt_timer_is_valid((uint32_t)timer_ch)) {
        return RA_DAC_STREAM_STATUS_INVALID_TIMER;
    }

    ra_dac_stream_cleanup(ch);
    ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_NONE, FSP_SUCCESS);

    ra_dac_stream_state_t *state = &ra_dac_stream_state[ch];
    state->transfer = RA_DAC_TRANSFER_DMAC;
    state->loop = false;
    state->double_buffered = true;
    state->freq = freq;
    state->buffers[0] = buf_a;
    state->buffers[1] = buf_b;
    state->buffer_ready[0] = false;
    state->buffer_ready[1] = buf_b_ready;
    state->buffer_sample_count = sample_count;
    state->active_buffer = 0;
    state->buffer_fill = fill_cb;
    state->buffer_stop = stop_cb;
    state->buffer_context = context;

    if (!ra_dac_timer_reserve(timer_ch, &state->timer_ch)) {
        return RA_DAC_STREAM_STATUS_TIMER_BUSY;
    }
    state->timer_reserved = true;

    ra_agt_timer_init(state->timer_ch, (float)freq);
    state->timer_initialized = true;
    if (ra_agt_timer_get_freq(state->timer_ch) <= 0.0f) {
        ra_dac_stream_cleanup(ch);
        return RA_DAC_STREAM_STATUS_INVALID_FREQ;
    }

    if (!ra_dac_stream_start_transfer(ch, buf_a, sample_count, false)) {
        ra_dac_stream_status_t err = !state->dmac_reserved ? RA_DAC_STREAM_STATUS_TRANSFER_BUSY : RA_DAC_STREAM_STATUS_HW_ERROR;
        ra_dac_stream_cleanup(ch);
        return err;
    }

    if (!ra_dac_is_running(ch)) {
        ra_dac_start(ch);
    }

    IRQn_Type agt_irq = ra_dac_agt_irq(state->timer_ch);
    if (agt_irq >= (IRQn_Type)0) {
        R_BSP_IrqStatusClear(agt_irq);
    }
    state->active = true;
    ra_agt_timer_start(state->timer_ch);

    return RA_DAC_STREAM_STATUS_OK;
}

void ra_dac_stream_stop(uint8_t ch) {
    ra_dac_stream_cleanup(ch);
}

bool ra_dac_stream_is_active(uint8_t ch) {
    if (ch >= DAC_CH_SIZE) {
        return false;
    }

    ra_dac_stream_state_t *state = &ra_dac_stream_state[ch];
    if (!state->active) {
        return false;
    }
    if (state->loop || !state->transfer_open) {
        return true;
    }

    if (state->transfer == RA_DAC_TRANSFER_DMAC) {
        R_DMAC0_Type *dmac_reg = state->dmac_ctrl.p_reg;
        uint16_t remaining = dmac_reg->DMCRA_b.DMCRAL;
        bool dte = dmac_reg->DMCNT_b.DTE != 0U;
        bool done = dmac_reg->DMSTS_b.DTIF != 0U || remaining == 0U;
        bool runtime_error =
            (!dte && !done) ||
            (R_DMA->DMECHR_b.DMESTA != 0U && R_DMA->DMECHR_b.DMECH == state->dmac_ch) ||
            (R_BUS->BUS3ERRSTAT != 0U) ||
            (R_BUS->DMACDTCERRSTAT_b.MTERRSTAT != 0U) ||
            ((R_ICU->DELSR[state->dmac_ch] & 0x1FFU) == 0U && remaining != 0U);

        if (runtime_error) {
            ra_dac_set_last_error(ch, RA_DAC_HW_STAGE_DMAC_RUNTIME, ra_dac_dmac_runtime_diag(state->dmac_ch));
            ra_dac_stream_cleanup(ch);
            return false;
        }

        if (state->double_buffered) {
            return true;
        }

        if (done) {
            ra_dac_stream_cleanup(ch);
            return false;
        }
    }

    transfer_properties_t props;
    fsp_err_t err;
    if (state->transfer == RA_DAC_TRANSFER_DMAC) {
        err = R_DMAC_InfoGet((transfer_ctrl_t *)&state->dmac_ctrl, &props);
    } else {
        err = R_DTC_InfoGet((transfer_ctrl_t *)&state->dtc_ctrl, &props);
    }

    if (err == FSP_SUCCESS && props.transfer_length_remaining != 0U) {
        return true;
    }

    ra_dac_stream_cleanup(ch);
    return false;
}

int8_t ra_dac_stream_timer(uint8_t ch) {
    if (ch >= DAC_CH_SIZE) {
        return -1;
    }

    ra_dac_stream_state_t *state = &ra_dac_stream_state[ch];
    if (!state->timer_reserved) {
        return -1;
    }

    return (int8_t)state->timer_ch;
}

ra_dac_hw_stage_t ra_dac_stream_last_stage(uint8_t ch) {
    if (ch >= DAC_CH_SIZE) {
        return RA_DAC_HW_STAGE_NONE;
    }
    return ra_dac_last_stage[ch];
}

int32_t ra_dac_stream_last_error(uint8_t ch) {
    if (ch >= DAC_CH_SIZE) {
        return (int32_t)FSP_SUCCESS;
    }
    return ra_dac_last_error[ch];
}
