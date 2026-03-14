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

#include "hal_data.h"
#include "mphalport.h"
#include "ra_config.h"
#include "ra_gpio.h"
#include "ra_sci.h"
#include "ra_sci_spi.h"
#include "ra_utils.h"

#if defined(MICROPY_HW_SPI2_SCK) && defined(MICROPY_HW_SPI2_MOSI) && defined(MICROPY_HW_SPI2_MISO)

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

static R_SCI0_Type *sci_spi_regs[] = {
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

static uint32_t sci_spi_ch_to_idx[SCI_CH_MAX] = {
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

static uint32_t sci_spi_module_mask[] = {
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

static bool sci_spi_active[SCI_CH_MAX];

static void ra_sci_spi_delay_cycles(volatile uint32_t cycles) {
    while (cycles-- > 0) {
        __asm__ __volatile__ ("nop");
    }
}

static void ra_sci_spi_delay_bit_time(uint32_t baud) {
    if (baud == 0) {
        return;
    }
    uint32_t cycles = (PCLK + baud - 1) / baud;
    if (cycles == 0) {
        cycles = 1;
    }
    ra_sci_spi_delay_cycles(cycles);
}

static bool ra_sci_spi_is_default_pinset(uint32_t mosi, uint32_t miso, uint32_t sck) {
    return mosi == MICROPY_HW_SPI2_MOSI->pin
        && miso == MICROPY_HW_SPI2_MISO->pin
        && sck == MICROPY_HW_SPI2_SCK->pin;
}

static void ra_sci_spi_set_sck_pin(uint32_t pin) {
    if (pin == MICROPY_HW_SPI2_SCK->pin) {
        ra_gpio_config(pin, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_LOW_POWER, AF_SCI1);
    }
}

static void ra_sci_spi_set_mosi_pin(uint32_t pin) {
    if (pin == MICROPY_HW_SPI2_MOSI->pin) {
        ra_gpio_config(pin, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_LOW_POWER, AF_SCI1);
    }
}

static void ra_sci_spi_set_miso_pin(uint32_t pin) {
    if (pin == MICROPY_HW_SPI2_MISO->pin) {
        ra_gpio_config(pin, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_LOW_POWER, AF_SCI1);
    }
}

typedef struct {
    uint8_t brr;
    uint8_t cks;
    uint8_t mddr;
} ra_sci_spi_div_setting_t;

static void ra_sci_spi_calc_baud(uint32_t baud, ra_sci_spi_div_setting_t *div) {
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

bool ra_sci_spi_find_af_ch(uint32_t mosi, uint32_t miso, uint32_t sck, uint8_t *ch) {
    bool ok = ra_sci_spi_is_default_pinset(mosi, miso, sck);
    if (ok && ch != NULL) {
        *ch = 2;
    }
    return ok;
}

bool ra_sci_spi_init(uint32_t ch, uint32_t mosi, uint32_t miso, uint32_t sck, uint32_t baud, uint32_t polarity, uint32_t phase, uint32_t firstbit) {
    uint8_t found_ch = 0xff;
    if (!ra_sci_spi_find_af_ch(mosi, miso, sck, &found_ch) || found_ch != ch) {
        return false;
    }
    if (firstbit != SPI_FIRSTBIT_MSB) {
        return false;
    }
    if (!ra_sci_owner_acquire(ch, RA_SCI_OWNER_SPI)) {
        return false;
    }

    uint32_t idx = sci_spi_ch_to_idx[ch];
    R_SCI0_Type *sci_reg = sci_spi_regs[idx];
    ra_sci_spi_div_setting_t div;

    ra_mstpcrb_start(sci_spi_module_mask[idx]);
    ra_sci_spi_set_sck_pin(sck);
    ra_sci_spi_set_mosi_pin(mosi);
    ra_sci_spi_set_miso_pin(miso);

    sci_reg->SCR = 0;
    while (sci_reg->SCR != 0) {
        ;
    }

    ra_sci_spi_calc_baud(baud, &div);

    uint8_t smr = R_SCI0_SMR_CM_Msk | (uint8_t)(div.cks << R_SCI0_SMR_CKS_Pos);
    uint8_t scmr = (uint8_t)((2U << R_SCI0_SCMR_CHR1_Pos) | R_SCI0_SCMR_BCP2_Msk);
    uint8_t semr = 0;
    uint8_t spmr = 0;

    if (div.mddr > INT8_MAX) {
        semr |= R_SCI0_SEMR_BRME_Msk;
    }
    if (phase == 0) {
        spmr |= R_SCI0_SPMR_CKPH_Msk;
        if (polarity == 1) {
            spmr |= R_SCI0_SPMR_CKPOL_Msk;
        }
    } else if (polarity == 0) {
        spmr |= R_SCI0_SPMR_CKPOL_Msk;
    }

    scmr |= R_SCI0_SCMR_SDIR_Msk;

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

    ra_sci_spi_delay_bit_time(baud);
    sci_spi_active[ch] = true;
    return true;
}

void ra_sci_spi_transfer(uint32_t ch, const uint8_t *src, uint8_t *dst, uint32_t count) {
    uint32_t idx = sci_spi_ch_to_idx[ch];
    R_SCI0_Type *sci_reg = sci_spi_regs[idx];

    sci_reg->SCR = (uint8_t)((sci_reg->SCR & R_SCI0_SCR_CKE_Msk) | R_SCI0_SCR_TE_Msk | R_SCI0_SCR_RE_Msk);

    for (uint32_t i = 0; i < count; ++i) {
        uint8_t data = src != NULL ? src[i] : 0xff;
        while ((sci_reg->SSR & R_SCI0_SSR_TDRE_Msk) == 0) {
            ;
        }
        sci_reg->TDR = data;
        while ((sci_reg->SSR & R_SCI0_SSR_RDRF_Msk) == 0) {
            ;
        }
        uint8_t in = sci_reg->RDR;
        if (dst != NULL) {
            dst[i] = in;
        }
    }

    while ((sci_reg->SSR & R_SCI0_SSR_TEND_Msk) == 0) {
        ;
    }
    sci_reg->SCR &= (uint8_t)R_SCI0_SCR_CKE_Msk;
}

void ra_sci_spi_deinit(uint32_t ch) {
    uint32_t idx = sci_spi_ch_to_idx[ch];
    if (!sci_spi_active[ch]) {
        ra_sci_owner_release(ch, RA_SCI_OWNER_SPI);
        return;
    }
    R_SCI0_Type *sci_reg = sci_spi_regs[idx];
    sci_reg->SCR &= (uint8_t)R_SCI0_SCR_CKE_Msk;
    ra_mstpcrb_stop(sci_spi_module_mask[idx]);
    sci_spi_active[ch] = false;
    ra_sci_owner_release(ch, RA_SCI_OWNER_SPI);
}

#else

bool ra_sci_spi_find_af_ch(uint32_t mosi, uint32_t miso, uint32_t sck, uint8_t *ch) {
    (void)mosi;
    (void)miso;
    (void)sck;
    (void)ch;
    return false;
}

bool ra_sci_spi_init(uint32_t ch, uint32_t mosi, uint32_t miso, uint32_t sck, uint32_t baud, uint32_t polarity, uint32_t phase, uint32_t firstbit) {
    (void)ch;
    (void)mosi;
    (void)miso;
    (void)sck;
    (void)baud;
    (void)polarity;
    (void)phase;
    (void)firstbit;
    return false;
}

void ra_sci_spi_deinit(uint32_t ch) {
    (void)ch;
}

void ra_sci_spi_transfer(uint32_t ch, const uint8_t *src, uint8_t *dst, uint32_t count) {
    (void)ch;
    (void)src;
    (void)dst;
    (void)count;
}

#endif
