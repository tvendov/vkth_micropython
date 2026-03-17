/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
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

#include "py/mphal.h"
#include "hal_data.h"
#include "r_dtc.h"
#include "ra_config.h"
#include "ra_gpio.h"
#include "ra_sci.h"
#include "ra_sci_ws2812.h"
#include "ra_utils.h"
#include "vector_data.h"

#if defined(MICROPY_HW_WS2812_DATA) && defined(MICROPY_HW_WS2812_SCI_CH)

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

// Git baseline from f2f762d46, but keep sparse channel-indexed arrays to avoid
// the old ws2812_ch_to_idx out-of-bounds bug on VK_RA4M2.
static R_SCI0_Type *ws2812_regs[SCI_CH_MAX] = {
    #if defined(VECTOR_NUMBER_SCI0_RXI)
    [0] = R_SCI0,
    #endif
    #if defined(VECTOR_NUMBER_SCI1_RXI)
    [1] = R_SCI1,
    #endif
    #if defined(VECTOR_NUMBER_SCI2_RXI)
    [2] = R_SCI2,
    #endif
    #if defined(VECTOR_NUMBER_SCI3_RXI)
    [3] = R_SCI3,
    #endif
    #if defined(VECTOR_NUMBER_SCI4_RXI)
    [4] = R_SCI4,
    #endif
    #if defined(VECTOR_NUMBER_SCI5_RXI)
    [5] = R_SCI5,
    #endif
    #if defined(VECTOR_NUMBER_SCI6_RXI)
    [6] = R_SCI6,
    #endif
    #if defined(VECTOR_NUMBER_SCI7_RXI)
    [7] = R_SCI7,
    #endif
    #if defined(VECTOR_NUMBER_SCI8_RXI)
    [8] = R_SCI8,
    #endif
    #if defined(VECTOR_NUMBER_SCI9_RXI)
    [9] = R_SCI9,
    #endif
};

static uint32_t ws2812_module_mask[SCI_CH_MAX] = {
    #if defined(VECTOR_NUMBER_SCI0_RXI)
    [0] = R_MSTP_MSTPCRB_MSTPB31_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI1_RXI)
    [1] = R_MSTP_MSTPCRB_MSTPB30_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI2_RXI)
    [2] = R_MSTP_MSTPCRB_MSTPB29_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI3_RXI)
    [3] = R_MSTP_MSTPCRB_MSTPB28_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI4_RXI)
    [4] = R_MSTP_MSTPCRB_MSTPB27_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI5_RXI)
    [5] = R_MSTP_MSTPCRB_MSTPB26_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI6_RXI)
    [6] = R_MSTP_MSTPCRB_MSTPB25_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI7_RXI)
    [7] = R_MSTP_MSTPCRB_MSTPB24_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI8_RXI)
    [8] = R_MSTP_MSTPCRB_MSTPB23_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI9_RXI)
    [9] = R_MSTP_MSTPCRB_MSTPB22_Msk,
    #endif
};

static bool ws2812_active[SCI_CH_MAX];
static uint32_t ws2812_baudrate[SCI_CH_MAX];

typedef struct {
    uint8_t brr;
    uint8_t cks;
    uint8_t mddr;
} ra_sci_ws2812_div_setting_t;

typedef struct {
    dtc_instance_ctrl_t ctrl;
    transfer_info_t info;
    transfer_cfg_t cfg;
    dtc_extended_cfg_t ext;
    IRQn_Type txi_irq;
    bool open;
} ra_sci_ws2812_dtc_state_t;

static ra_sci_ws2812_dtc_state_t ws2812_dtc[SCI_CH_MAX];

static void ra_sci_ws2812_set_data_pin_af(uint32_t data_pin, uint32_t af);
static void ra_sci_ws2812_set_data_pin_gpio_low(uint32_t data_pin);

static bool ra_sci_ws2812_find_pin_af_ch(uint32_t data_pin, uint32_t *ch, uint32_t *af) {
    return ra_sci_find_tx_ch_af(data_pin, ch, af);
}

static bool ra_sci_ws2812_is_valid_channel(uint32_t ch) {
    return ch < SCI_CH_MAX && ws2812_regs[ch] != NULL && ws2812_module_mask[ch] != 0;
}

static IRQn_Type ra_sci_ws2812_ch_to_txi_irq(uint32_t ch) {
    switch (ch) {
        #if defined(VECTOR_NUMBER_SCI0_TXI)
        case 0:
            return VECTOR_NUMBER_SCI0_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI1_TXI)
        case 1:
            return VECTOR_NUMBER_SCI1_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI2_TXI)
        case 2:
            return VECTOR_NUMBER_SCI2_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI3_TXI)
        case 3:
            return VECTOR_NUMBER_SCI3_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI4_TXI)
        case 4:
            return VECTOR_NUMBER_SCI4_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI5_TXI)
        case 5:
            return VECTOR_NUMBER_SCI5_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI6_TXI)
        case 6:
            return VECTOR_NUMBER_SCI6_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI7_TXI)
        case 7:
            return VECTOR_NUMBER_SCI7_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI8_TXI)
        case 8:
            return VECTOR_NUMBER_SCI8_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI9_TXI)
        case 9:
            return VECTOR_NUMBER_SCI9_TXI;
        #endif
        default:
            return (IRQn_Type)-1;
    }
}

static bool ra_sci_ws2812_dtc_open(uint32_t ch, R_SCI0_Type *sci_reg) {
    ra_sci_ws2812_dtc_state_t *dtc = &ws2812_dtc[ch];
    IRQn_Type txi_irq = ra_sci_ws2812_ch_to_txi_irq(ch);
    if (txi_irq < (IRQn_Type)0) {
        return false;
    }

    memset(dtc, 0, sizeof(*dtc));
    dtc->txi_irq = txi_irq;
    dtc->info.transfer_settings_word = 0;
    dtc->info.transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    dtc->info.transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION;
    dtc->info.transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    dtc->info.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    dtc->info.transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    dtc->info.transfer_settings_word_b.size = TRANSFER_SIZE_1_BYTE;
    dtc->info.transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    dtc->info.p_src = NULL;
    dtc->info.p_dest = (void *)&sci_reg->TDR;
    dtc->info.num_blocks = 0;
    dtc->info.length = 1;
    dtc->ext.activation_source = txi_irq;
    dtc->cfg.p_info = &dtc->info;
    dtc->cfg.p_extend = &dtc->ext;

    R_BSP_IrqDisable(txi_irq);
    R_BSP_IrqStatusClear(txi_irq);

    if (R_DTC_Open((transfer_ctrl_t *)&dtc->ctrl, &dtc->cfg) != FSP_SUCCESS) {
        memset(dtc, 0, sizeof(*dtc));
        return false;
    }

    dtc->open = true;
    return true;
}

static void ra_sci_ws2812_dtc_close(uint32_t ch) {
    ra_sci_ws2812_dtc_state_t *dtc = &ws2812_dtc[ch];
    if (!dtc->open) {
        return;
    }
    R_DTC_Disable((transfer_ctrl_t *)&dtc->ctrl);
    R_BSP_IrqStatusClear(dtc->txi_irq);
    R_BSP_IrqDisable(dtc->txi_irq);
    R_DTC_Close((transfer_ctrl_t *)&dtc->ctrl);
    memset(dtc, 0, sizeof(*dtc));
}

static void ra_sci_ws2812_write_polling_payload(R_SCI0_Type *sci_reg, uint32_t data_pin, uint32_t af, const uint8_t *buf, uint32_t len) {
    while ((sci_reg->SSR & R_SCI0_SSR_TDRE_Msk) == 0) {
        ;
    }
    sci_reg->TDR = buf[0];
    sci_reg->SCR = (uint8_t)((sci_reg->SCR & R_SCI0_SCR_CKE_Msk) | R_SCI0_SCR_TE_Msk);
    ra_sci_ws2812_set_data_pin_af(data_pin, af);

    for (uint32_t i = 1; i < len; ++i) {
        while ((sci_reg->SSR & R_SCI0_SSR_TDRE_Msk) == 0) {
            ;
        }
        sci_reg->TDR = buf[i];
    }
}

static bool ra_sci_ws2812_write_dtc_payload(uint32_t ch, R_SCI0_Type *sci_reg, uint32_t data_pin, uint32_t af, const uint8_t *buf, uint32_t len) {
    ra_sci_ws2812_dtc_state_t *dtc = &ws2812_dtc[ch];
    if (!dtc->open || len == 0 || len > UINT16_MAX) {
        return false;
    }

    // Re-arm the channel for this exact transfer: one contiguous TXI->DTC stream,
    // no CPU-fed per-byte handoff inside the visible payload.
    R_DTC_Disable((transfer_ctrl_t *)&dtc->ctrl);
    R_BSP_IrqStatusClear(dtc->txi_irq);

    if (R_DTC_Reset((transfer_ctrl_t *)&dtc->ctrl, (void *)buf, (void *)&sci_reg->TDR, (uint16_t)len) != FSP_SUCCESS) {
        return false;
    }
    if (R_DTC_Enable((transfer_ctrl_t *)&dtc->ctrl) != FSP_SUCCESS) {
        return false;
    }

    // Start TXI->DTC from a fully disabled state while the line is still held low.
    // This keeps the SCI start transition inside the raw-zero prefix instead of
    // on the first encoded WS2812 payload bit.
    sci_reg->SCR = (uint8_t)(sci_reg->SCR & R_SCI0_SCR_CKE_Msk);
    sci_reg->SCR = (uint8_t)((sci_reg->SCR & R_SCI0_SCR_CKE_Msk) | R_SCI0_SCR_TE_Msk | R_SCI0_SCR_TIE_Msk);
    ra_sci_ws2812_set_data_pin_af(data_pin, af);

    uint32_t timeout_ms = (uint32_t)((((uint64_t)len * 8ULL * 1000ULL) + ws2812_baudrate[ch] - 1) / ws2812_baudrate[ch]) + 5;
    mp_uint_t start_ms = mp_hal_ticks_ms();
    transfer_properties_t props;
    do {
        R_DTC_InfoGet((transfer_ctrl_t *)&dtc->ctrl, &props);
        if (props.transfer_length_remaining == 0 && (sci_reg->SSR & R_SCI0_SSR_TEND_Msk) != 0) {
            R_DTC_Disable((transfer_ctrl_t *)&dtc->ctrl);
            R_BSP_IrqStatusClear(dtc->txi_irq);
            sci_reg->SCR &= (uint8_t)~R_SCI0_SCR_TIE_Msk;
            return true;
        }
    } while (mp_hal_ticks_ms() - start_ms < timeout_ms);

    R_DTC_Disable((transfer_ctrl_t *)&dtc->ctrl);
    R_BSP_IrqStatusClear(dtc->txi_irq);
    sci_reg->SCR &= (uint8_t)~R_SCI0_SCR_TIE_Msk;
    return false;
}

static void ra_sci_ws2812_set_data_pin_af(uint32_t data_pin, uint32_t af) {
    ra_gpio_config(data_pin, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_MID_POWER, af);
}

static void ra_sci_ws2812_set_data_pin_gpio_low(uint32_t data_pin) {
    // Drive low before switching mode so the line never floats during latch/reset.
    ra_gpio_write(data_pin, 0);
    ra_gpio_config(data_pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_MID_POWER, AF_GPIO);
    ra_gpio_write(data_pin, 0);
}

static void ra_sci_ws2812_calc_baud(uint32_t baud, ra_sci_ws2812_div_setting_t *div) {
    int32_t divisor = 0;
    int32_t brr = 0;
    int32_t cks = -1;

    for (uint32_t i = 0; i <= 3; ++i) {
        cks++;
        divisor = (1 << (2 * (i + 1))) * (int32_t)baud;
        brr = ((int32_t)PCLK + divisor - 1) / divisor - 1;
        if (brr <= UINT8_MAX) {
            break;
        }
    }

    if (brr < 0) {
        brr = 0;
    } else if (brr > UINT8_MAX) {
        brr = UINT8_MAX;
    }

    int64_t mddr = (int64_t)divisor * (brr + 1) * (UINT8_MAX + 1) / PCLK;
    if (mddr > UINT8_MAX) {
        mddr = 0;
    }

    div->brr = (uint8_t)brr;
    div->cks = (uint8_t)(cks & 3);
    div->mddr = (uint8_t)mddr;
}

bool ra_sci_ws2812_find_ch(uint32_t data_pin, uint8_t *ch) {
    uint32_t found_ch;
    uint32_t af;
    bool ok = ra_sci_ws2812_find_pin_af_ch(data_pin, &found_ch, &af);
    if (ok && ch != NULL) {
        *ch = (uint8_t)found_ch;
    }
    return ok;
}

bool ra_sci_ws2812_init(uint32_t ch, uint32_t data_pin, uint32_t baudrate, uint32_t polarity, uint32_t phase) {
    uint32_t found_ch = 0xff;
    uint32_t af = 0;
    if (!ra_sci_ws2812_find_pin_af_ch(data_pin, &found_ch, &af) || found_ch != ch) {
        return false;
    }
    if (!ra_sci_ws2812_is_valid_channel(ch)) {
        return false;
    }
    if (!ra_sci_owner_acquire(ch, RA_SCI_OWNER_WS2812)) {
        return false;
    }

    // Baseline recovery keeps the original working SCI mode from f2f762d46.
    (void)polarity;
    (void)phase;

    R_SCI0_Type *sci_reg = ws2812_regs[ch];
    ra_sci_ws2812_div_setting_t div;

    ra_mstpcrb_start(ws2812_module_mask[ch]);
    ra_sci_ws2812_set_data_pin_gpio_low(data_pin);

    sci_reg->SCR = 0;
    while (sci_reg->SCR != 0) {
        ;
    }

    ra_sci_ws2812_calc_baud(baudrate, &div);

    uint8_t smr = R_SCI0_SMR_CM_Msk | (uint8_t)(div.cks << R_SCI0_SMR_CKS_Pos);
    uint8_t scmr = (uint8_t)((2U << R_SCI0_SCMR_CHR1_Pos) | R_SCI0_SCMR_BCP2_Msk | R_SCI0_SCMR_SDIR_Msk);
    uint8_t semr = 0;
    uint8_t spmr = 0;

    if (div.mddr > INT8_MAX) {
        semr |= R_SCI0_SEMR_BRME_Msk;
    }

    sci_reg->SSR;
    sci_reg->SSR = (uint8_t)(R_SCI0_SSR_TDRE_Msk | R_SCI0_SSR_TEND_Msk);
    sci_reg->FCR = R_SCI0_FCR_RSTRG_Msk | (8U << R_SCI0_FCR_RTRG_Pos);
    sci_reg->SMR = smr;
    sci_reg->SCR = 0;
    sci_reg->SCMR = scmr;
    sci_reg->BRR = div.brr;
    sci_reg->MDDR = div.mddr;
    sci_reg->SEMR = semr;
    sci_reg->SPMR = spmr;
    sci_reg->SNFR = 0;
    sci_reg->SIMR1 = 0;
    sci_reg->SIMR2 = 0;
    sci_reg->SIMR3 = 0;
    sci_reg->CDR = 0;
    sci_reg->DCCR = R_SCI0_DCCR_IDSEL_Msk;
    // SPTR behavior is only guaranteed in async mode; keep neutral value here.
    sci_reg->SPTR = 0;

    ws2812_baudrate[ch] = baudrate;
    ra_sci_ws2812_dtc_open(ch, sci_reg);
    ws2812_active[ch] = true;
    return true;
}

bool ra_sci_ws2812_write(uint32_t ch, uint32_t data_pin, const uint8_t *buf, uint32_t len, uint32_t latch_us) {
    uint32_t found_ch = 0xff;
    uint32_t af = 0;

    if (!ra_sci_ws2812_is_valid_channel(ch) || !ws2812_active[ch] || buf == NULL || len == 0) {
        ra_sci_ws2812_set_data_pin_gpio_low(data_pin);
        mp_hal_delay_us(latch_us);
        return false;
    }

    R_SCI0_Type *sci_reg = ws2812_regs[ch];

    if (!ra_sci_ws2812_find_pin_af_ch(data_pin, &found_ch, &af) || found_ch != ch) {
        ra_sci_ws2812_set_data_pin_gpio_low(data_pin);
        mp_hal_delay_us(latch_us);
        return false;
    }

    sci_reg->SCR = (uint8_t)(sci_reg->SCR & R_SCI0_SCR_CKE_Msk);
    ra_sci_ws2812_set_data_pin_gpio_low(data_pin);

    // Prefer DTC so IRQ load cannot create byte-to-byte gaps in the visible frame.
    bool use_dtc = ra_sci_ws2812_write_dtc_payload(ch, sci_reg, data_pin, af, buf, len);
    if (!use_dtc) {
        ra_sci_ws2812_write_polling_payload(sci_reg, data_pin, af, buf, len);
    }

    while ((sci_reg->SSR & R_SCI0_SSR_TEND_Msk) == 0) {
        ;
    }

    // After the last stop bit, return the pin to a driven low GPIO and let the
    // raw-zero suffix plus latch delay cover the LED reset window.
    sci_reg->SCR &= (uint8_t)R_SCI0_SCR_CKE_Msk;
    ra_sci_ws2812_set_data_pin_gpio_low(data_pin);
    mp_hal_delay_us(latch_us);
    return true;
}

void ra_sci_ws2812_deinit(uint32_t ch, uint32_t data_pin) {
    R_SCI0_Type *sci_reg = ra_sci_ws2812_is_valid_channel(ch) ? ws2812_regs[ch] : NULL;

    if (sci_reg != NULL && ws2812_active[ch]) {
        sci_reg->SCR &= (uint8_t)R_SCI0_SCR_CKE_Msk;
        ra_mstpcrb_stop(ws2812_module_mask[ch]);
        ws2812_active[ch] = false;
        ws2812_baudrate[ch] = 0;
    }

    ra_sci_ws2812_dtc_close(ch);
    ra_sci_ws2812_set_data_pin_gpio_low(data_pin);
    ra_sci_owner_release(ch, RA_SCI_OWNER_WS2812);
}

#else

bool ra_sci_ws2812_find_ch(uint32_t data_pin, uint8_t *ch) {
    (void)data_pin;
    (void)ch;
    return false;
}

bool ra_sci_ws2812_init(uint32_t ch, uint32_t data_pin, uint32_t baudrate, uint32_t polarity, uint32_t phase) {
    (void)ch;
    (void)data_pin;
    (void)baudrate;
    (void)polarity;
    (void)phase;
    return false;
}

bool ra_sci_ws2812_write(uint32_t ch, uint32_t data_pin, const uint8_t *buf, uint32_t len, uint32_t latch_us) {
    (void)ch;
    (void)data_pin;
    (void)buf;
    (void)len;
    (void)latch_us;
    return false;
}

void ra_sci_ws2812_deinit(uint32_t ch, uint32_t data_pin) {
    (void)ch;
    (void)data_pin;
}

#endif
