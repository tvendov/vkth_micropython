/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Renesas Electronics Corporation
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
#include "ra_config.h"
#include "ra_utils.h"

static R_SYSTEM_Type *system_reg = (R_SYSTEM_Type *)0x4001E000;
static R_MSTP_Type *mstp_reg = R_MSTP;
static bool ra_dmac_reserved[BSP_FEATURE_DMAC_MAX_CHANNEL];

static uint32_t ra_utils_irq_save(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void ra_utils_irq_restore(uint32_t primask) {
    __set_PRIMASK(primask);
}

void ra_mstpcra_stop(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    system_reg->MSTPCRA |= mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcra_start(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    system_reg->MSTPCRA &= ~mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcrb_stop(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    mstp_reg->MSTPCRB |= mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcrb_start(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    mstp_reg->MSTPCRB &= ~mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcrc_stop(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    mstp_reg->MSTPCRC |= mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcrc_start(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    mstp_reg->MSTPCRC &= ~mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcrd_stop(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    mstp_reg->MSTPCRD |= mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcrd_start(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    mstp_reg->MSTPCRD &= ~mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcre_stop(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    mstp_reg->MSTPCRE |= mod_mask;
    system_reg->PRCR = 0xa500;
}

void ra_mstpcre_start(uint32_t mod_mask) {
    system_reg->PRCR = 0xa502;
    mstp_reg->MSTPCRE &= ~mod_mask;
    system_reg->PRCR = 0xa500;
}

bool ra_dmac_reserve(uint32_t ch) {
    if (ch >= BSP_FEATURE_DMAC_MAX_CHANNEL) {
        return false;
    }

    uint32_t primask = ra_utils_irq_save();
    bool acquired = !ra_dmac_reserved[ch];
    if (acquired) {
        ra_dmac_reserved[ch] = true;
    }
    ra_utils_irq_restore(primask);
    return acquired;
}

void ra_dmac_release(uint32_t ch) {
    if (ch >= BSP_FEATURE_DMAC_MAX_CHANNEL) {
        return;
    }

    uint32_t primask = ra_utils_irq_save();
    ra_dmac_reserved[ch] = false;
    ra_utils_irq_restore(primask);
}

void ra_dmac_clear_all_reservations(void) {
    uint32_t primask = ra_utils_irq_save();
    memset(ra_dmac_reserved, 0, sizeof(ra_dmac_reserved));
    ra_utils_irq_restore(primask);
}

bool ra_dmac_is_reserved(uint32_t ch) {
    if (ch >= BSP_FEATURE_DMAC_MAX_CHANNEL) {
        return false;
    }

    uint32_t primask = ra_utils_irq_save();
    bool reserved = ra_dmac_reserved[ch];
    ra_utils_irq_restore(primask);
    return reserved;
}

__WEAK void ctsu_write_isr(void) {
    // dummy
}

__WEAK void ctsu_read_isr(void) {
    // dummy
}

__WEAK void ctsu_end_isr(void) {
    // dummy
}
