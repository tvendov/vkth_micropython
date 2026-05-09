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

#include <string.h>
#include "hal_data.h"
#include "mphalport.h"
#include "r_dtc.h"
#include "ra_config.h"
#include "ra_gpio.h"
#include "ra_sci.h"
#include "ra_sci_spi.h"
#include "ra_utils.h"
#include "vector_data.h"

// Per-board defaults: each MICROPY_HW_SPI<n>_* triple maps spi_id -> SCI channel + AF.
// AF_SCI1 = PSEL=00100b, AF_SCI2 = PSEL=00101b. Override per board via mpconfigboard.h.
#if defined(MICROPY_HW_SPI2_SCK) && defined(MICROPY_HW_SPI2_MOSI) && defined(MICROPY_HW_SPI2_MISO)
#ifndef MICROPY_HW_SPI2_SCI_CH
#define MICROPY_HW_SPI2_SCI_CH      (2)
#endif
#ifndef MICROPY_HW_SPI2_SCI_AF
#define MICROPY_HW_SPI2_SCI_AF      AF_SCI1
#endif
#define RA_SCI_SPI_HAS_BUS2         (1)
#endif

#if defined(MICROPY_HW_SPI3_SCK) && defined(MICROPY_HW_SPI3_MOSI) && defined(MICROPY_HW_SPI3_MISO)
#ifndef MICROPY_HW_SPI3_SCI_CH
#error "Define MICROPY_HW_SPI3_SCI_CH (SCI hardware channel) for SPI3 simple-SPI"
#endif
#ifndef MICROPY_HW_SPI3_SCI_AF
#error "Define MICROPY_HW_SPI3_SCI_AF (AF_SCI1 or AF_SCI2) for SPI3 simple-SPI"
#endif
#define RA_SCI_SPI_HAS_BUS3         (1)
#endif

#if defined(RA_SCI_SPI_HAS_BUS2) || defined(RA_SCI_SPI_HAS_BUS3)
#define RA_SCI_SPI_ENABLED          (1)
#endif

#if defined(RA_SCI_SPI_ENABLED)

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

#define RA_SCI_SPI_SCMR_RESERVED_MASK (0x62U)

typedef struct {
    uint8_t spi_id;     // user-facing SPI bus id
    uint8_t sci_ch;     // hardware SCI channel (0..9)
    uint8_t af;         // GPIO alternate function for the pin trio
    uint32_t mosi;
    uint32_t miso;
    uint32_t sck;
} ra_sci_spi_pinset_t;

static R_SCI0_Type *const sci_spi_regs[SCI_CH_MAX] = {
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

static const uint32_t sci_spi_module_mask[SCI_CH_MAX] = {
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

static bool sci_spi_active[SCI_CH_MAX];
// Map spi_id -> sci_ch for active buses (so deinit/transfer can find the channel).
// Sentinel 0xff = "no channel registered for this spi_id".
static uint8_t sci_spi_id_to_ch[16] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

// Per-channel transfer state. Updated only by the RX DTC's IRQ_END callback
// (1 IRQ per transfer), polled by foreground via WFI.
typedef struct {
    volatile uint8_t done;
    uint8_t tx_idle_byte;   // sent on MOSI when src == NULL
    uint8_t rx_scratch;     // byte sink when dst == NULL
} ra_sci_spi_xfer_t;
static ra_sci_spi_xfer_t sci_spi_xfer[SCI_CH_MAX];

// One DTC instance per direction per SCI channel: TX feeds FTDR, RX drains FRDR.
// Each is wired to its own SCIn_TXI / SCIn_RXI activation source; FSP installs
// the DTC vector table entry on R_DTC_Open(). With IRQ_END mode the activation
// IRQ is suppressed during transfer and propagates to NVIC only on the last byte.
typedef struct {
    dtc_instance_ctrl_t ctrl;
    transfer_info_t info;
    transfer_cfg_t cfg;
    dtc_extended_cfg_t ext;
    IRQn_Type irq;
    bool open;
} ra_sci_spi_dtc_t;

static ra_sci_spi_dtc_t sci_spi_tx_dtc[SCI_CH_MAX];
static ra_sci_spi_dtc_t sci_spi_rx_dtc[SCI_CH_MAX];

#define RA_SCI_SPI_IRQ_PRIORITY     (12)
#define RA_SCI_SPI_FIFO_DEPTH       (16U)
#define RA_SCI_SPI_RX_TRIGGER       (1U)   // RXI on every byte received
#define RA_SCI_SPI_TX_TRIGGER       (15U)  // TXI whenever ≥1 stage free in TX FIFO

// Map an SCI channel to its RXI/TXI IRQ vector. Returns -1 when the channel
// has no vector defined (which means SPI on that channel is not supported).
static IRQn_Type ra_sci_spi_ch_to_rxi_irq(uint32_t ch) {
    switch (ch) {
        #if defined(VECTOR_NUMBER_SCI0_RXI)
        case 0: return VECTOR_NUMBER_SCI0_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI1_RXI)
        case 1: return VECTOR_NUMBER_SCI1_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI2_RXI)
        case 2: return VECTOR_NUMBER_SCI2_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI3_RXI)
        case 3: return VECTOR_NUMBER_SCI3_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI4_RXI)
        case 4: return VECTOR_NUMBER_SCI4_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI5_RXI)
        case 5: return VECTOR_NUMBER_SCI5_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI6_RXI)
        case 6: return VECTOR_NUMBER_SCI6_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI7_RXI)
        case 7: return VECTOR_NUMBER_SCI7_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI8_RXI)
        case 8: return VECTOR_NUMBER_SCI8_RXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI9_RXI)
        case 9: return VECTOR_NUMBER_SCI9_RXI;
        #endif
        default: return (IRQn_Type)-1;
    }
}

static IRQn_Type ra_sci_spi_ch_to_txi_irq(uint32_t ch) {
    switch (ch) {
        #if defined(VECTOR_NUMBER_SCI0_TXI)
        case 0: return VECTOR_NUMBER_SCI0_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI1_TXI)
        case 1: return VECTOR_NUMBER_SCI1_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI2_TXI)
        case 2: return VECTOR_NUMBER_SCI2_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI3_TXI)
        case 3: return VECTOR_NUMBER_SCI3_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI4_TXI)
        case 4: return VECTOR_NUMBER_SCI4_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI5_TXI)
        case 5: return VECTOR_NUMBER_SCI5_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI6_TXI)
        case 6: return VECTOR_NUMBER_SCI6_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI7_TXI)
        case 7: return VECTOR_NUMBER_SCI7_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI8_TXI)
        case 8: return VECTOR_NUMBER_SCI8_TXI;
        #endif
        #if defined(VECTOR_NUMBER_SCI9_TXI)
        case 9: return VECTOR_NUMBER_SCI9_TXI;
        #endif
        default: return (IRQn_Type)-1;
    }
}

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

// Build the pinset table at runtime (machine_pin_obj_t* fields are not constant exprs).
static void ra_sci_spi_get_pinsets(ra_sci_spi_pinset_t *out, uint32_t *count) {
    uint32_t n = 0;
    #if defined(RA_SCI_SPI_HAS_BUS2)
    out[n].spi_id = 2;
    out[n].sci_ch = MICROPY_HW_SPI2_SCI_CH;
    out[n].af     = MICROPY_HW_SPI2_SCI_AF;
    out[n].mosi   = MICROPY_HW_SPI2_MOSI->pin;
    out[n].miso   = MICROPY_HW_SPI2_MISO->pin;
    out[n].sck    = MICROPY_HW_SPI2_SCK->pin;
    n++;
    #endif
    #if defined(RA_SCI_SPI_HAS_BUS3)
    out[n].spi_id = 3;
    out[n].sci_ch = MICROPY_HW_SPI3_SCI_CH;
    out[n].af     = MICROPY_HW_SPI3_SCI_AF;
    out[n].mosi   = MICROPY_HW_SPI3_MOSI->pin;
    out[n].miso   = MICROPY_HW_SPI3_MISO->pin;
    out[n].sck    = MICROPY_HW_SPI3_SCK->pin;
    n++;
    #endif
    *count = n;
}

#define RA_SCI_SPI_MAX_PINSETS  (4)

static bool ra_sci_spi_lookup_pinset(uint32_t mosi, uint32_t miso, uint32_t sck, ra_sci_spi_pinset_t *match) {
    ra_sci_spi_pinset_t pinsets[RA_SCI_SPI_MAX_PINSETS];
    uint32_t count = 0;
    ra_sci_spi_get_pinsets(pinsets, &count);
    for (uint32_t i = 0; i < count; ++i) {
        if (pinsets[i].mosi == mosi && pinsets[i].miso == miso && pinsets[i].sck == sck) {
            *match = pinsets[i];
            return true;
        }
    }
    return false;
}

static bool ra_sci_spi_lookup_by_id(uint8_t spi_id, ra_sci_spi_pinset_t *match) {
    ra_sci_spi_pinset_t pinsets[RA_SCI_SPI_MAX_PINSETS];
    uint32_t count = 0;
    ra_sci_spi_get_pinsets(pinsets, &count);
    for (uint32_t i = 0; i < count; ++i) {
        if (pinsets[i].spi_id == spi_id) {
            *match = pinsets[i];
            return true;
        }
    }
    return false;
}

typedef struct {
    uint8_t brr;
    uint8_t cks;
    uint8_t mddr;
} ra_sci_spi_div_setting_t;

static void ra_sci_spi_calc_baud(uint32_t baud, ra_sci_spi_div_setting_t *div) {
    /* SCI in clock-synchronous (SPI master) mode.  Scope measurements on
     * VK_RA4M2 show that the SPI clock is generated from:
     *
     *   B = PCLK / (2 × 4^N × (BRR + 1))
     *
     * where N = CKS (0..3), BRR = 0..255.
     *
     * BRME/MDDR are not used here; on this SCI clock-synchronous path they
     * did not affect the measured SCK period, and enabling BRME caused the
     * register dump to look precise while the real SCK stayed BRR-based.
     *
     * Strategy: pick the smallest CKS that fits and round the divider to the
     * nearest integer.  This gives exact 1 MHz for PCLK=40 MHz (BRR=19) and
     * PCLK=50 MHz (BRR=24).
     */
    int32_t cks = 0;
    int32_t brr = 0;

    for (int32_t i = 0; i <= 3; ++i) {
        int32_t factor = 2 * (1 << (2 * i));  /* 2, 8, 32, 128 */
        int64_t denom = (int64_t)factor * baud;
        if (denom == 0) {
            continue;
        }
        /* Divider = round(PCLK / denom), then BRR = divider - 1. */
        int32_t divider = (int32_t)(((int64_t)PCLK + (denom / 2)) / denom);
        int32_t computed = divider - 1;
        if (computed < 0) {
            computed = 0;
        }
        if (computed <= UINT8_MAX) {
            brr = computed;
            cks = i;
            break;
        }
    }
    if (brr > UINT8_MAX) {
        brr = UINT8_MAX;
    }

    div->brr = (uint8_t)brr;
    div->cks = (uint8_t)(cks & 3);
    div->mddr = 0;
}

// --- ISR callbacks (owner-matched dispatch from ra_sci.c) ---
//
// Both DTC channels run with TRANSFER_IRQ_END: only the LAST activation
// propagates to NVIC. So each callback fires exactly once per transfer.
// The RX-end callback is the canonical completion signal — by the time it
// runs, the last byte has already been moved into the user's dst buffer.

static void ra_sci_spi_rxi_cb(uint32_t ch) {
    // RX DTC has drained FRDR for the last byte. Foreground may now wake.
    sci_spi_xfer[ch].done = 1;
}

static void ra_sci_spi_txi_cb(uint32_t ch) {
    // TX DTC has fed the last byte into FTDR. Nothing else to do here —
    // shifting and the RX side completes via the RX callback above.
    (void)ch;
}

// --- DTC helpers ---
//
// Open one DTC channel for the given activation source. Source/dest are
// placeholders here; they get re-armed per transfer via R_DTC_Reset.
static bool ra_sci_spi_dtc_open(ra_sci_spi_dtc_t *dtc, IRQn_Type irq,
    transfer_addr_mode_t src_mode, transfer_addr_mode_t dst_mode) {
    if (irq < (IRQn_Type)0) {
        return false;
    }
    memset(dtc, 0, sizeof(*dtc));
    dtc->irq = irq;
    dtc->info.transfer_settings_word = 0;
    dtc->info.transfer_settings_word_b.dest_addr_mode = dst_mode;
    dtc->info.transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION;
    dtc->info.transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    dtc->info.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    dtc->info.transfer_settings_word_b.src_addr_mode = src_mode;
    dtc->info.transfer_settings_word_b.size = TRANSFER_SIZE_1_BYTE;
    dtc->info.transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    dtc->info.p_src = NULL;
    dtc->info.p_dest = NULL;
    dtc->info.num_blocks = 0;
    dtc->info.length = 1;
    dtc->ext.activation_source = irq;
    dtc->cfg.p_info = &dtc->info;
    dtc->cfg.p_extend = &dtc->ext;

    R_BSP_IrqDisable(irq);
    R_BSP_IrqStatusClear(irq);

    if (R_DTC_Open((transfer_ctrl_t *)&dtc->ctrl, &dtc->cfg) != FSP_SUCCESS) {
        memset(dtc, 0, sizeof(*dtc));
        return false;
    }
    dtc->open = true;
    return true;
}

static void ra_sci_spi_dtc_close(ra_sci_spi_dtc_t *dtc) {
    if (!dtc->open) {
        return;
    }
    R_DTC_Disable((transfer_ctrl_t *)&dtc->ctrl);
    R_BSP_IrqStatusClear(dtc->irq);
    R_BSP_IrqDisable(dtc->irq);
    R_DTC_Close((transfer_ctrl_t *)&dtc->ctrl);
    memset(dtc, 0, sizeof(*dtc));
}

// Arm a DTC channel for a single transfer. src_mode/dst_mode were fixed in
// open(); only the address pointer values can change here.
static bool ra_sci_spi_dtc_arm(ra_sci_spi_dtc_t *dtc, void *src, void *dst,
    transfer_addr_mode_t src_mode, transfer_addr_mode_t dst_mode, uint16_t len) {
    R_DTC_Disable((transfer_ctrl_t *)&dtc->ctrl);
    R_BSP_IrqStatusClear(dtc->irq);
    // Mode may change between transfers (FIXED <-> INCREMENTED for src/dst).
    dtc->info.transfer_settings_word_b.src_addr_mode = src_mode;
    dtc->info.transfer_settings_word_b.dest_addr_mode = dst_mode;
    if (R_DTC_Reset((transfer_ctrl_t *)&dtc->ctrl, src, dst, len) != FSP_SUCCESS) {
        return false;
    }
    if (R_DTC_Enable((transfer_ctrl_t *)&dtc->ctrl) != FSP_SUCCESS) {
        return false;
    }
    return true;
}

bool ra_sci_spi_find_af_ch(uint32_t mosi, uint32_t miso, uint32_t sck, uint8_t *ch) {
    ra_sci_spi_pinset_t match;
    if (!ra_sci_spi_lookup_pinset(mosi, miso, sck, &match)) {
        return false;
    }
    if (ch != NULL) {
        *ch = match.spi_id;
    }
    return true;
}

bool ra_sci_spi_init(uint32_t ch, uint32_t mosi, uint32_t miso, uint32_t sck, uint32_t baud, uint32_t polarity, uint32_t phase, uint32_t firstbit) {
    ra_sci_spi_pinset_t match;
    if (!ra_sci_spi_lookup_pinset(mosi, miso, sck, &match) || match.spi_id != ch) {
        return false;
    }
    if (firstbit != SPI_FIRSTBIT_MSB) {
        return false;
    }
    uint32_t sci_ch = match.sci_ch;
    if (sci_ch >= SCI_CH_MAX || sci_spi_regs[sci_ch] == NULL) {
        return false;
    }
    if (!ra_sci_owner_acquire(sci_ch, RA_SCI_OWNER_SPI)) {
        return false;
    }

    R_SCI0_Type *sci_reg = sci_spi_regs[sci_ch];
    ra_sci_spi_div_setting_t div;

    ra_mstpcrb_start(sci_spi_module_mask[sci_ch]);
    ra_gpio_config(sck,  GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_HIGH_POWER, match.af);
    ra_gpio_config(mosi, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_HIGH_POWER, match.af);
    ra_gpio_config(miso, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_HIGH_POWER, match.af);

    sci_reg->SCR = 0;
    while (sci_reg->SCR != 0) {
        ;
    }

    ra_sci_spi_calc_baud(baud, &div);

    uint8_t smr = R_SCI0_SMR_CM_Msk | (uint8_t)(div.cks << R_SCI0_SMR_CKS_Pos);
    uint8_t scmr = (uint8_t)((2U << R_SCI0_SCMR_CHR1_Pos) | R_SCI0_SCMR_BCP2_Msk | RA_SCI_SPI_SCMR_RESERVED_MASK);
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
    // Step 1 of FIFO setup: keep FCR.FM=0 while we configure other registers,
    // then flip to FIFO mode last (datasheet 27.3.6 Note: FM must be set after
    // BRR/SCMR/SMR for stable triggers).
    sci_reg->FCR = 0;
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

    // Step 2: enter FIFO mode. RTRG=1 (RXI on each byte → DTC drains it),
    // TTRG=15 (TXI whenever TX FIFO has ≥1 free stage → DTC keeps it full).
    // Reset both FIFOs initially (RFRST/TFRST=1).
    uint16_t fcr = (uint16_t)(R_SCI0_FCR_FM_Msk
                              | R_SCI0_FCR_RFRST_Msk
                              | R_SCI0_FCR_TFRST_Msk
                              | (RA_SCI_SPI_TX_TRIGGER << R_SCI0_FCR_TTRG_Pos)
                              | (RA_SCI_SPI_RX_TRIGGER << R_SCI0_FCR_RTRG_Pos));
    sci_reg->FCR = fcr;
    // Take FIFOs out of reset.
    sci_reg->FCR = (uint16_t)(fcr & ~(R_SCI0_FCR_RFRST_Msk | R_SCI0_FCR_TFRST_Msk));

    ra_sci_spi_delay_bit_time(baud);

    // Register owner-matched ISR callbacks (only fired by DTC IRQ_END at end).
    ra_sci_set_rxi_callback(sci_ch, RA_SCI_OWNER_SPI, ra_sci_spi_rxi_cb);
    ra_sci_set_txi_callback(sci_ch, RA_SCI_OWNER_SPI, ra_sci_spi_txi_cb);

    // Open both DTC channels. Address modes are placeholders; per-transfer
    // arming overrides them depending on whether src/dst are user buffers
    // or fixed scratch bytes.
    IRQn_Type rxi = ra_sci_spi_ch_to_rxi_irq(sci_ch);
    IRQn_Type txi = ra_sci_spi_ch_to_txi_irq(sci_ch);
    if (!ra_sci_spi_dtc_open(&sci_spi_tx_dtc[sci_ch], txi,
            TRANSFER_ADDR_MODE_INCREMENTED, TRANSFER_ADDR_MODE_FIXED)) {
        ra_sci_owner_release(sci_ch, RA_SCI_OWNER_SPI);
        return false;
    }
    if (!ra_sci_spi_dtc_open(&sci_spi_rx_dtc[sci_ch], rxi,
            TRANSFER_ADDR_MODE_FIXED, TRANSFER_ADDR_MODE_INCREMENTED)) {
        ra_sci_spi_dtc_close(&sci_spi_tx_dtc[sci_ch]);
        ra_sci_owner_release(sci_ch, RA_SCI_OWNER_SPI);
        return false;
    }

    // Enable NVIC for both irqs. DTC consumes the body activations; only the
    // final byte's activation propagates here (TRANSFER_IRQ_END). The RX one
    // is the real completion signal; the TX one is a benign no-op.
    if (rxi >= 0) {
        R_BSP_IrqCfg(rxi, RA_SCI_SPI_IRQ_PRIORITY, NULL);
        R_BSP_IrqStatusClear(rxi);
        R_BSP_IrqEnable(rxi);
    }
    if (txi >= 0) {
        R_BSP_IrqCfg(txi, RA_SCI_SPI_IRQ_PRIORITY, NULL);
        R_BSP_IrqStatusClear(txi);
        R_BSP_IrqEnable(txi);
    }

    sci_spi_active[sci_ch] = true;
    sci_spi_id_to_ch[match.spi_id & 0xf] = (uint8_t)sci_ch;
    return true;
}

// FIFO + dual-DTC full-duplex transfer.
// One IRQ_END per direction: RX completion is the foreground wake signal.
// While running, DTC pumps FTDR / drains FRDR autonomously — CPU sleeps in WFI.
//
// Length is bounded by DTC: max 65535 bytes per transfer. Larger transfers
// are split into chunks. Smaller chunks would fit in one FIFO depth (16) but
// DTC handles the full length transparently.
void ra_sci_spi_transfer(uint32_t ch, const uint8_t *src, uint8_t *dst, uint32_t count) {
    uint8_t sci_ch = sci_spi_id_to_ch[ch & 0xf];
    if (sci_ch >= SCI_CH_MAX || !sci_spi_active[sci_ch] || count == 0) {
        return;
    }
    R_SCI0_Type *sci_reg = sci_spi_regs[sci_ch];
    ra_sci_spi_xfer_t *xfer = &sci_spi_xfer[sci_ch];

    // Stop any prior SCI activity. Keep CKE so the master clock pin holds level.
    sci_reg->SCR = (uint8_t)(sci_reg->SCR & R_SCI0_SCR_CKE_Msk);

    // Reset both FIFOs and clear status. RFRST/TFRST drain leftovers in case
    // the previous transfer aborted mid-stream.
    uint16_t fcr_run = (uint16_t)(R_SCI0_FCR_FM_Msk
                                  | (RA_SCI_SPI_TX_TRIGGER << R_SCI0_FCR_TTRG_Pos)
                                  | (RA_SCI_SPI_RX_TRIGGER << R_SCI0_FCR_RTRG_Pos));
    sci_reg->FCR = (uint16_t)(fcr_run | R_SCI0_FCR_RFRST_Msk | R_SCI0_FCR_TFRST_Msk);
    sci_reg->FCR = fcr_run;
    // Clear status flags via SSR_FIFO (same offset as SSR, FIFO bit layout).
    sci_reg->SSR_FIFO = (uint8_t)~(R_SCI0_SSR_FIFO_TDFE_Msk
                                   | R_SCI0_SSR_FIFO_RDF_Msk
                                   | R_SCI0_SSR_FIFO_TEND_Msk
                                   | R_SCI0_SSR_FIFO_DR_Msk);

    // For chunks larger than 65535 we'd need to loop; clamp to one DTC transfer
    // here — the caller is expected to split if needed.
    uint16_t len = (count > UINT16_MAX) ? UINT16_MAX : (uint16_t)count;

    // Choose DTC address modes based on whether user provided buffers.
    void *tx_src;
    transfer_addr_mode_t tx_src_mode;
    if (src != NULL) {
        tx_src = (void *)src;
        tx_src_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    } else {
        xfer->tx_idle_byte = 0xff;
        tx_src = (void *)&xfer->tx_idle_byte;
        tx_src_mode = TRANSFER_ADDR_MODE_FIXED;
    }
    void *rx_dst;
    transfer_addr_mode_t rx_dst_mode;
    if (dst != NULL) {
        rx_dst = (void *)dst;
        rx_dst_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    } else {
        rx_dst = (void *)&xfer->rx_scratch;
        rx_dst_mode = TRANSFER_ADDR_MODE_FIXED;
    }

    xfer->done = 0;

    // Arm RX DTC first so the very first received byte is captured.
    if (!ra_sci_spi_dtc_arm(&sci_spi_rx_dtc[sci_ch],
            (void *)&sci_reg->FRDRL, rx_dst,
            TRANSFER_ADDR_MODE_FIXED, rx_dst_mode, len)) {
        return;
    }
    if (!ra_sci_spi_dtc_arm(&sci_spi_tx_dtc[sci_ch],
            tx_src, (void *)&sci_reg->FTDRL,
            tx_src_mode, TRANSFER_ADDR_MODE_FIXED, len)) {
        R_DTC_Disable((transfer_ctrl_t *)&sci_spi_rx_dtc[sci_ch].ctrl);
        return;
    }

    // Kick off: TE+RE with TIE+RIE (preserve CKE). TXI pends immediately because
    // TX FIFO has 16 free stages (TTRG=15 → ≥1 stage free fires TXI). DTC then
    // floods FTDR with `len` bytes; SCI shifts; RX DTC drains FRDR; RX IRQ_END
    // fires our rxi_cb which sets done=1.
    sci_reg->SCR = (uint8_t)((sci_reg->SCR & R_SCI0_SCR_CKE_Msk)
                             | R_SCI0_SCR_TE_Msk | R_SCI0_SCR_RE_Msk
                             | R_SCI0_SCR_TIE_Msk | R_SCI0_SCR_RIE_Msk);

    // Wait for RX DTC completion. WFI puts CPU to sleep between events.
    while (xfer->done == 0) {
        __asm__ __volatile__ ("wfi");
    }

    // Stop SCI; ensure DTCs are disabled (they're already at count=0 but clear).
    sci_reg->SCR &= (uint8_t)R_SCI0_SCR_CKE_Msk;
    R_DTC_Disable((transfer_ctrl_t *)&sci_spi_tx_dtc[sci_ch].ctrl);
    R_DTC_Disable((transfer_ctrl_t *)&sci_spi_rx_dtc[sci_ch].ctrl);
}

void ra_sci_spi_deinit(uint32_t ch) {
    uint8_t sci_ch = sci_spi_id_to_ch[ch & 0xf];
    if (sci_ch >= SCI_CH_MAX) {
        // Bus was never initialised; still try owner release using id->ch lookup table.
        ra_sci_spi_pinset_t match;
        if (ra_sci_spi_lookup_by_id((uint8_t)ch, &match)) {
            ra_sci_owner_release(match.sci_ch, RA_SCI_OWNER_SPI);
        }
        return;
    }
    if (!sci_spi_active[sci_ch]) {
        ra_sci_owner_release(sci_ch, RA_SCI_OWNER_SPI);
        sci_spi_id_to_ch[ch & 0xf] = 0xff;
        return;
    }
    R_SCI0_Type *sci_reg = sci_spi_regs[sci_ch];
    sci_reg->SCR = 0;
    // Drop FIFO mode so the next owner (e.g. UART) starts in 1-stage mode.
    sci_reg->FCR = 0;

    // Tear down DTCs first — DTC close also disables their NVIC activation
    // bits and clears any pending status in ICU.
    ra_sci_spi_dtc_close(&sci_spi_tx_dtc[sci_ch]);
    ra_sci_spi_dtc_close(&sci_spi_rx_dtc[sci_ch]);

    // Belt-and-braces: also disable NVIC at the SCI vector, in case some flag
    // is still latched. The shared sci_uart_*_isr will then fire only when a
    // future owner explicitly re-enables.
    IRQn_Type rxi = ra_sci_spi_ch_to_rxi_irq(sci_ch);
    IRQn_Type txi = ra_sci_spi_ch_to_txi_irq(sci_ch);
    if (rxi >= 0) {
        R_BSP_IrqDisable(rxi);
        R_BSP_IrqStatusClear(rxi);
    }
    if (txi >= 0) {
        R_BSP_IrqDisable(txi);
        R_BSP_IrqStatusClear(txi);
    }

    ra_sci_clear_rxi_callback(sci_ch);
    ra_sci_clear_txi_callback(sci_ch);

    ra_mstpcrb_stop(sci_spi_module_mask[sci_ch]);
    sci_spi_active[sci_ch] = false;
    sci_spi_id_to_ch[ch & 0xf] = 0xff;
    ra_sci_owner_release(sci_ch, RA_SCI_OWNER_SPI);
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
