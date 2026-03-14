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
#include "ra_config.h"
#include "ra_gpio.h"
#include "ra_sci.h"
#include "ra_sci_ws2812.h"
#include "ra_utils.h"

#if defined(MICROPY_HW_WS2812_DATA) && defined(MICROPY_HW_WS2812_SCI_CH)

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

static R_SCI0_Type *ws2812_regs[] = {
    #if defined(VECTOR_NUMBER_SCI0_RXI)
    R_SCI0,
    #endif
    #if defined(VECTOR_NUMBER_SCI1_RXI)
    R_SCI1,
    #endif
    #if defined(VECTOR_NUMBER_SCI2_RXI)
    R_SCI2,
    #endif
    #if defined(VECTOR_NUMBER_SCI3_RXI)
    R_SCI3,
    #endif
    #if defined(VECTOR_NUMBER_SCI4_RXI)
    R_SCI4,
    #endif
    #if defined(VECTOR_NUMBER_SCI5_RXI)
    R_SCI5,
    #endif
    #if defined(VECTOR_NUMBER_SCI6_RXI)
    R_SCI6,
    #endif
    #if defined(VECTOR_NUMBER_SCI7_RXI)
    R_SCI7,
    #endif
    #if defined(VECTOR_NUMBER_SCI8_RXI)
    R_SCI8,
    #endif
    #if defined(VECTOR_NUMBER_SCI9_RXI)
    R_SCI9,
    #endif
};

static uint32_t ws2812_ch_to_idx[SCI_CH_MAX] = {
    #if defined(VECTOR_NUMBER_SCI0_RXI)
    0,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI1_RXI)
    1,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI2_RXI)
    2,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI3_RXI)
    3,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI4_RXI)
    4,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI5_RXI)
    5,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI6_RXI)
    6,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI7_RXI)
    7,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI8_RXI)
    8,
    #else
    0,
    #endif
    #if defined(VECTOR_NUMBER_SCI9_RXI)
    9,
    #else
    0,
    #endif
};

static uint32_t ws2812_module_mask[] = {
    #if defined(VECTOR_NUMBER_SCI0_RXI)
    R_MSTP_MSTPCRB_MSTPB31_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI1_RXI)
    R_MSTP_MSTPCRB_MSTPB30_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI2_RXI)
    R_MSTP_MSTPCRB_MSTPB29_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI3_RXI)
    R_MSTP_MSTPCRB_MSTPB28_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI4_RXI)
    R_MSTP_MSTPCRB_MSTPB27_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI5_RXI)
    R_MSTP_MSTPCRB_MSTPB26_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI6_RXI)
    R_MSTP_MSTPCRB_MSTPB25_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI7_RXI)
    R_MSTP_MSTPCRB_MSTPB24_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI8_RXI)
    R_MSTP_MSTPCRB_MSTPB23_Msk,
    #endif
    #if defined(VECTOR_NUMBER_SCI9_RXI)
    R_MSTP_MSTPCRB_MSTPB22_Msk,
    #endif
};

static bool ws2812_active[SCI_CH_MAX];

typedef struct {
    uint8_t brr;
    uint8_t cks;
    uint8_t mddr;
} ra_sci_ws2812_div_setting_t;

static bool ra_sci_ws2812_find_pin_af_ch(uint32_t data_pin, uint32_t *ch, uint32_t *af) {
    return ra_sci_find_tx_ch_af(data_pin, ch, af);
}

static void ra_sci_ws2812_set_data_pin_af(uint32_t data_pin, uint32_t af) {
    ra_gpio_config(data_pin, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_LOW_POWER, af);
}

static void ra_sci_ws2812_set_data_pin_gpio_low(uint32_t data_pin) {
    ra_gpio_write(data_pin, 0);
    ra_gpio_config(data_pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
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

bool ra_sci_ws2812_init(uint32_t ch, uint32_t data_pin, uint32_t baudrate) {
    uint32_t found_ch = 0xff;
    uint32_t af = 0;
    if (!ra_sci_ws2812_find_pin_af_ch(data_pin, &found_ch, &af) || found_ch != ch) {
        return false;
    }
    (void)af;
    if (!ra_sci_owner_acquire(ch, RA_SCI_OWNER_WS2812)) {
        return false;
    }

    uint32_t idx = ws2812_ch_to_idx[ch];
    R_SCI0_Type *sci_reg = ws2812_regs[idx];
    ra_sci_ws2812_div_setting_t div;

    ra_mstpcrb_start(ws2812_module_mask[idx]);
    ra_sci_ws2812_set_data_pin_gpio_low(data_pin);

    sci_reg->SCR = 0;
    while (sci_reg->SCR != 0) {
        ;
    }

    ra_sci_ws2812_calc_baud(baudrate, &div);

    uint8_t smr = R_SCI0_SMR_CM_Msk | (uint8_t)(div.cks << R_SCI0_SMR_CKS_Pos);
    uint8_t scmr = (uint8_t)((2U << R_SCI0_SCMR_CHR1_Pos) | R_SCI0_SCMR_BCP2_Msk | R_SCI0_SCMR_SDIR_Msk);
    uint8_t semr = 0;
    uint8_t spmr = R_SCI0_SPMR_CKPH_Msk;

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
    sci_reg->SPTR = R_SCI0_SPTR_SPB2DT_Msk;

    ws2812_active[ch] = true;
    return true;
}

void ra_sci_ws2812_write(uint32_t ch, uint32_t data_pin, const uint8_t *buf, uint32_t len, uint32_t latch_us) {
    uint32_t found_ch = 0xff;
    uint32_t af = 0;
    uint32_t idx = ws2812_ch_to_idx[ch];
    R_SCI0_Type *sci_reg = ws2812_regs[idx];

    if (!ws2812_active[ch] || buf == NULL || len == 0) {
        ra_sci_ws2812_set_data_pin_gpio_low(data_pin);
        mp_hal_delay_us(latch_us);
        return;
    }

    if (!ra_sci_ws2812_find_pin_af_ch(data_pin, &found_ch, &af) || found_ch != ch) {
        ra_sci_ws2812_set_data_pin_gpio_low(data_pin);
        mp_hal_delay_us(latch_us);
        return;
    }

    ra_sci_ws2812_set_data_pin_af(data_pin, af);
    sci_reg->SCR = (uint8_t)((sci_reg->SCR & R_SCI0_SCR_CKE_Msk) | R_SCI0_SCR_TE_Msk);

    for (uint32_t i = 0; i < len; ++i) {
        while ((sci_reg->SSR & R_SCI0_SSR_TDRE_Msk) == 0) {
            ;
        }
        sci_reg->TDR = buf[i];
    }

    while ((sci_reg->SSR & R_SCI0_SSR_TEND_Msk) == 0) {
        ;
    }

    sci_reg->SCR &= (uint8_t)R_SCI0_SCR_CKE_Msk;
    ra_sci_ws2812_set_data_pin_gpio_low(data_pin);
    mp_hal_delay_us(latch_us);
}

void ra_sci_ws2812_deinit(uint32_t ch, uint32_t data_pin) {
    uint32_t idx = ws2812_ch_to_idx[ch];
    R_SCI0_Type *sci_reg = ws2812_regs[idx];

    if (ws2812_active[ch]) {
        sci_reg->SCR &= (uint8_t)R_SCI0_SCR_CKE_Msk;
        ra_mstpcrb_stop(ws2812_module_mask[idx]);
        ws2812_active[ch] = false;
    }

    ra_sci_ws2812_set_data_pin_gpio_low(data_pin);
    ra_sci_owner_release(ch, RA_SCI_OWNER_WS2812);
}

#else

bool ra_sci_ws2812_find_ch(uint32_t data_pin, uint8_t *ch) {
    (void)data_pin;
    (void)ch;
    return false;
}

bool ra_sci_ws2812_init(uint32_t ch, uint32_t data_pin, uint32_t baudrate) {
    (void)ch;
    (void)data_pin;
    (void)baudrate;
    return false;
}

void ra_sci_ws2812_write(uint32_t ch, uint32_t data_pin, const uint8_t *buf, uint32_t len, uint32_t latch_us) {
    (void)ch;
    (void)data_pin;
    (void)buf;
    (void)len;
    (void)latch_us;
}

void ra_sci_ws2812_deinit(uint32_t ch, uint32_t data_pin) {
    (void)ch;
    (void)data_pin;
}

#endif
