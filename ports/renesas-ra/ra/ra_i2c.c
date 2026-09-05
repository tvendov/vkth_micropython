/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021,2022 Renesas Electronics Corporation
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hal_data.h"
#include "ra_config.h"
#include "ra_gpio.h"
#include "ra_icu.h"
#include "ra_int.h"
#include "ra_timer.h"
#include "ra_utils.h"
#include "ra_i2c.h"
#include "ra_i2c_recovery.h"
#include "ra_i2c_slave.h"

#if !defined(RA_PRI_I2C)
#define RA_PRI_I2C (8)
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

// Per-channel mode flag: 0=master, 1=slave.  Checked inside the
// iic_master_*_isr() dispatch functions so that the same vector-table
// entries work for both master and slave modes.
volatile uint8_t iic_slave_mode[3] = {0, 0, 0};

void ra_i2c_set_slave_mode(uint8_t ch, bool slave) {
    if (ch < 3) {
        iic_slave_mode[ch] = slave ? 1 : 0;
    }
}

extern volatile uint32_t uwTick;

static const ra_af_pin_t scl_pins[] = {
    #if defined(RA4M1)

    { AF_I2C, 0, P204 },
    { AF_I2C, 0, P400 },
    { AF_I2C, 0, P408 },
    { AF_I2C, 1, P100 },
    { AF_I2C, 1, P205 },

    #elif defined(RA4W1)

    { AF_I2C, 0, P204 },
    { AF_I2C, 1, P100 },
    { AF_I2C, 1, P205 },

    #elif defined(RA6M1)

    { AF_I2C, 0, P400 },
    { AF_I2C, 0, P408 },
    { AF_I2C, 1, P100 },
    { AF_I2C, 1, P205 },

    #elif defined(RA4M2)

    { AF_I2C, 0, P204 },
    { AF_I2C, 0, P400 },
    { AF_I2C, 1, P100 },
    { AF_I2C, 1, P205 },

    #elif defined(RA6M2) || defined(RA6M3)

    { AF_I2C, 0, P204 },
    { AF_I2C, 0, P400 },
    { AF_I2C, 0, P408 },
    { AF_I2C, 1, P100 },
    { AF_I2C, 1, P205 },
    { AF_I2C, 2, P512 },

    #elif defined(RA6M5)

    { AF_I2C, 0, P400 },
    { AF_I2C, 0, P408 },
    { AF_I2C, 1, P205 },
    { AF_I2C, 1, P512 },
    { AF_I2C, 2, P410 },
    { AF_I2C, 2, P415 },

    #else
    #error "CMSIS MCU Series is not specified."
    #endif
};
#define SCL_PINS_SIZE sizeof(scl_pins) / sizeof(ra_af_pin_t)

static const ra_af_pin_t sda_pins[] = {
    #if defined(RA4M1)

    { AF_I2C, 0, P401 },
    { AF_I2C, 0, P407 },
    { AF_I2C, 1, P101 },
    { AF_I2C, 1, P206 },

    #elif defined(RA4W1)

    { AF_I2C, 0, P407 },
    { AF_I2C, 1, P101 },
    { AF_I2C, 1, P206 },

    #elif defined(RA6M1)

    { AF_I2C, 0, P401 },
    { AF_I2C, 0, P407 },
    { AF_I2C, 1, P101 },
    { AF_I2C, 1, P206 },

    #elif defined(RA4M2)

    { AF_I2C, 0, P401 },
    { AF_I2C, 1, P101 },
    { AF_I2C, 1, P206 },

    #elif defined(RA6M2) || defined(RA6M3)

    { AF_I2C, 0, P401 },
    { AF_I2C, 0, P407 },
    { AF_I2C, 1, P101 },
    { AF_I2C, 1, P206 },
    { AF_I2C, 2, P511 },

    #elif defined(RA6M5)

    { AF_I2C, 0, P401 },
    { AF_I2C, 0, P407 },
    { AF_I2C, 1, P206 },
    { AF_I2C, 1, P511 },
    { AF_I2C, 2, P409 },
    { AF_I2C, 2, P414 },

    #else
    #error "CMSIS MCU Series is not specified."
    #endif
};
#define SDA_PINS_SIZE sizeof(sda_pins) / sizeof(ra_af_pin_t)

const uint8_t ra_i2c_ch_to_rxirq[] = {
    #if defined(VECTOR_NUMBER_IIC0_RXI)
    VECTOR_NUMBER_IIC0_RXI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
    #if defined(VECTOR_NUMBER_IIC1_RXI)
    VECTOR_NUMBER_IIC1_RXI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
    #if defined(VECTOR_NUMBER_IIC2_RXI)
    VECTOR_NUMBER_IIC2_RXI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
};
const uint8_t ra_i2c_ch_to_txirq[] = {
    #if defined(VECTOR_NUMBER_IIC0_TXI)
    VECTOR_NUMBER_IIC0_TXI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
    #if defined(VECTOR_NUMBER_IIC1_TXI)
    VECTOR_NUMBER_IIC1_TXI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
    #if defined(VECTOR_NUMBER_IIC2_TXI)
    VECTOR_NUMBER_IIC2_TXI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
};
const uint8_t ra_i2c_ch_to_teirq[] = {
    #if defined(VECTOR_NUMBER_IIC0_TEI)
    VECTOR_NUMBER_IIC0_TEI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
    #if defined(VECTOR_NUMBER_IIC1_TEI)
    VECTOR_NUMBER_IIC1_TEI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
    #if defined(VECTOR_NUMBER_IIC2_TEI)
    VECTOR_NUMBER_IIC2_TEI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
};
const uint8_t ra_i2c_ch_to_erirq[] = {
    #if defined(VECTOR_NUMBER_IIC0_ERI)
    VECTOR_NUMBER_IIC0_ERI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
    #if defined(VECTOR_NUMBER_IIC1_ERI)
    VECTOR_NUMBER_IIC1_ERI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
    #if defined(VECTOR_NUMBER_IIC2_ERI)
    VECTOR_NUMBER_IIC2_ERI,
    #else
    VECTOR_NUMBER_NONE,
    #endif
};

static xaction_t *volatile current_xaction;
static xaction_unit_t *volatile current_xaction_unit;
static bool last_stop;
static uint8_t current_channel;
static ra_i2c_recovery_t recovery;
static bool bus_fault[3];

typedef struct {
    transfer_info_t info;
    dtc_instance_ctrl_t ctrl;
    dtc_extended_cfg_t extend;
    transfer_cfg_t cfg;
    bool open;
    bool active;
    bool completion_irq_pending;
    uint8_t channel;
} ra_i2c_master_dtc_t;

// RIIC master transactions are serialized by current_xaction, so one static
// RX descriptor and one TX descriptor are sufficient for all channels.
static ra_i2c_master_dtc_t master_rx_dtc;
static ra_i2c_master_dtc_t master_tx_dtc;
static bool ra_i2c_dtc_quiesce_one(ra_i2c_master_dtc_t *dtc);
static uint8_t pclk_div[8] = {
    1, 2, 4, 8, 16, 32, 64, 128
};

static uint32_t R_IIC0_Type_to_ch(R_IIC0_Type *i2c_inst) {
    if (i2c_inst == R_IIC2) {
        return 2; /* channel 2 */
    } else if (i2c_inst == R_IIC1) {
        return 1; /* channel 1 */
    } else {
        return 0; /* channel 0 */
    }
}

static R_IIC0_Type *ch_to_R_IIC0_Type(uint32_t ch) {
    if (ch == 2) {
        return R_IIC2;
    } else if (ch == 1) {
        return R_IIC1;
    } else {
        return R_IIC0;
    }
}

static void ra_i2c_master_rx_dtc_close(void) {
    if (!ra_i2c_dtc_quiesce_one(&master_rx_dtc)) {
        return; // Keep descriptor alive until a subsequent single check.
    }
    if (master_rx_dtc.open) {
        (void)R_DTC_Close((transfer_ctrl_t *)&master_rx_dtc.ctrl);
    }
    master_rx_dtc.open = false;
    master_rx_dtc.active = false;
    master_rx_dtc.completion_irq_pending = false;
}

static bool ra_i2c_master_rx_dtc_prepare(R_IIC0_Type *i2c_inst, xaction_t *action) {
    ra_i2c_master_rx_dtc_close();

    if (i2c_inst == NULL || action == NULL || action->m_num_of_units == 0) {
        return false;
    }

    xaction_unit_t *unit = action->units;
    if (!unit->m_fread || unit->m_bytes_total <= 3 || unit->m_bytes_total - 3 > UINT16_MAX) {
        return false;
    }

    uint32_t channel = R_IIC0_Type_to_ch(i2c_inst);
    if (ra_i2c_ch_to_rxirq[channel] == VECTOR_NUMBER_NONE) {
        action->m_dtc_fallback_count++;
        return false;
    }

    memset(&master_rx_dtc, 0, sizeof(master_rx_dtc));
    master_rx_dtc.channel = (uint8_t)channel;
    master_rx_dtc.info.transfer_settings_word = 0;
    master_rx_dtc.info.transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    master_rx_dtc.info.transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION;
    master_rx_dtc.info.transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    master_rx_dtc.info.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    master_rx_dtc.info.transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    master_rx_dtc.info.transfer_settings_word_b.size = TRANSFER_SIZE_1_BYTE;
    master_rx_dtc.info.transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    master_rx_dtc.info.p_src = (void const *)&i2c_inst->ICDRR;
    master_rx_dtc.info.p_dest = unit->buf;
    master_rx_dtc.info.num_blocks = 0;
    master_rx_dtc.info.length = (uint16_t)(unit->m_bytes_total - 3);
    master_rx_dtc.extend.activation_source = (IRQn_Type)ra_i2c_ch_to_rxirq[channel];
    master_rx_dtc.cfg.p_info = &master_rx_dtc.info;
    master_rx_dtc.cfg.p_extend = &master_rx_dtc.extend;

    fsp_err_t error = R_DTC_Open(
        (transfer_ctrl_t *)&master_rx_dtc.ctrl, &master_rx_dtc.cfg);
    if (error != FSP_SUCCESS) {
        action->m_dtc_fallback_count++;
        memset(&master_rx_dtc, 0, sizeof(master_rx_dtc));
        return false;
    }

    master_rx_dtc.open = true;
    return true;
}

static bool ra_i2c_master_rx_dtc_arm(
    R_IIC0_Type *i2c_inst, xaction_t *action, xaction_unit_t *unit) {
    if (!master_rx_dtc.open || master_rx_dtc.active ||
        master_rx_dtc.channel != R_IIC0_Type_to_ch(i2c_inst)) {
        return false;
    }

    uint16_t transfer_count = (uint16_t)(unit->m_bytes_total - 3);
    // Open installed the complete descriptor with activation disabled. Enable
    // it once; R_DTC_Reset contains a hardware busy-wait and is not needed.
    fsp_err_t error = R_DTC_Enable((transfer_ctrl_t *)&master_rx_dtc.ctrl);
    if (error != FSP_SUCCESS) {
        action->m_dtc_fallback_count++;
        ra_i2c_master_rx_dtc_close();
        return false;
    }

    // IRQ_END lets the CPU see only the last DTC activation. The following
    // three RXIs perform the mandatory WAIT, NACK and STOP sequence.
    unit->m_bytes_transferred = transfer_count;
    unit->m_bytes_transfer = 3;
    action->m_dtc_transfer_count++;
    action->m_dtc_bytes += transfer_count;
    master_rx_dtc.active = true;
    master_rx_dtc.completion_irq_pending = true;
    return true;
}

static bool ra_i2c_master_rx_dtc_finish_irq(void) {
    if (!master_rx_dtc.active || !master_rx_dtc.completion_irq_pending) {
        return false;
    }

    ra_i2c_master_rx_dtc_close();
    return true;
}

static void ra_i2c_master_tx_dtc_close(void) {
    if (!ra_i2c_dtc_quiesce_one(&master_tx_dtc)) {
        return;
    }
    if (master_tx_dtc.open) {
        (void)R_DTC_Close((transfer_ctrl_t *)&master_tx_dtc.ctrl);
    }
    master_tx_dtc.open = false;
    master_tx_dtc.active = false;
    master_tx_dtc.completion_irq_pending = false;
}

static bool ra_i2c_master_tx_dtc_prepare(R_IIC0_Type *i2c_inst, xaction_t *action) {
    ra_i2c_master_tx_dtc_close();

    if (i2c_inst == NULL || action == NULL || action->m_num_of_units == 0) {
        return false;
    }

    xaction_unit_t *unit = action->units;
    if (unit->m_fread || unit->m_bytes_total == 0 || unit->m_bytes_total > UINT16_MAX) {
        return false;
    }

    uint32_t channel = R_IIC0_Type_to_ch(i2c_inst);
    if (ra_i2c_ch_to_txirq[channel] == VECTOR_NUMBER_NONE) {
        action->m_dtc_tx_fallback_count++;
        return false;
    }

    memset(&master_tx_dtc, 0, sizeof(master_tx_dtc));
    master_tx_dtc.channel = (uint8_t)channel;
    master_tx_dtc.info.transfer_settings_word = 0;
    master_tx_dtc.info.transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    master_tx_dtc.info.transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_SOURCE;
    master_tx_dtc.info.transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    master_tx_dtc.info.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    master_tx_dtc.info.transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    master_tx_dtc.info.transfer_settings_word_b.size = TRANSFER_SIZE_1_BYTE;
    master_tx_dtc.info.transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    master_tx_dtc.info.p_src = unit->buf;
    master_tx_dtc.info.p_dest = (void *)&i2c_inst->ICDRT;
    master_tx_dtc.info.num_blocks = 0;
    master_tx_dtc.info.length = (uint16_t)unit->m_bytes_total;
    master_tx_dtc.extend.activation_source = (IRQn_Type)ra_i2c_ch_to_txirq[channel];
    master_tx_dtc.cfg.p_info = &master_tx_dtc.info;
    master_tx_dtc.cfg.p_extend = &master_tx_dtc.extend;

    fsp_err_t error = R_DTC_Open(
        (transfer_ctrl_t *)&master_tx_dtc.ctrl, &master_tx_dtc.cfg);
    if (error != FSP_SUCCESS) {
        action->m_dtc_tx_fallback_count++;
        memset(&master_tx_dtc, 0, sizeof(master_tx_dtc));
        return false;
    }

    master_tx_dtc.open = true;
    return true;
}

static bool ra_i2c_master_tx_dtc_arm(
    R_IIC0_Type *i2c_inst, xaction_t *action, xaction_unit_t *unit) {
    if (!master_tx_dtc.open || master_tx_dtc.active ||
        master_tx_dtc.channel != R_IIC0_Type_to_ch(i2c_inst)) {
        return false;
    }

    uint16_t transfer_count = (uint16_t)unit->m_bytes_total;
    fsp_err_t error = R_DTC_Enable((transfer_ctrl_t *)&master_tx_dtc.ctrl);
    if (error != FSP_SUCCESS) {
        action->m_dtc_tx_fallback_count++;
        ra_i2c_master_tx_dtc_close();
        return false;
    }

    // The address byte is written by the CPU. Every payload byte is then
    // loaded into ICDRT by DTC. The final TXI only advances the state to TEI.
    unit->m_bytes_transferred = transfer_count;
    unit->m_bytes_transfer = 0;
    action->m_dtc_tx_transfer_count++;
    action->m_dtc_tx_bytes += transfer_count;
    master_tx_dtc.active = true;
    master_tx_dtc.completion_irq_pending = true;
    return true;
}

static bool ra_i2c_master_tx_dtc_finish_irq(void) {
    if (!master_tx_dtc.active || !master_tx_dtc.completion_irq_pending) {
        return false;
    }

    ra_i2c_master_tx_dtc_close();
    return true;
}

static void ra_i2c_xaction_notify_terminal(xaction_t *action) {
    if (action == NULL || action->m_status != RA_I2C_STATUS_Stopped ||
        action->m_completion_notified) {
        return;
    }

    action->m_completion_notified = true;
    if (action->m_complete_callback != NULL) {
        action->m_complete_callback(action->m_complete_context);
    }
}

bool ra_i2c_find_af_ch(uint32_t scl, uint32_t sda, uint8_t *ch) {
    bool find = false;
    uint8_t scl_ch;
    uint8_t sda_ch;
    find = ra_af_find_ch((ra_af_pin_t *)&scl_pins, SCL_PINS_SIZE, scl, &scl_ch);
    if (find) {
        find = ra_af_find_ch((ra_af_pin_t *)&sda_pins, SDA_PINS_SIZE, sda, &sda_ch);
        if (find) {
            find = (scl_ch == sda_ch);
            if (find) {
                *ch = scl_ch;
            } else {
                *ch = 0;
            }
        }
    }
    return find;
}

static void ra_i2c_module_start(R_IIC0_Type *i2c_inst) {
    if (i2c_inst == R_IIC0) {
        ra_mstpcrb_start(R_MSTP_MSTPCRB_MSTPB9_Msk);
    } else if (i2c_inst == R_IIC1) {
        ra_mstpcrb_start(R_MSTP_MSTPCRB_MSTPB8_Msk);
    } else if (i2c_inst == R_IIC2) {
        ra_mstpcrb_start(R_MSTP_MSTPCRB_MSTPB7_Msk);
    }
}

static void ra_i2c_module_stop(R_IIC0_Type *i2c_inst) {
    if (i2c_inst == R_IIC0) {
        ra_mstpcrb_stop(R_MSTP_MSTPCRB_MSTPB9_Msk);
    } else if (i2c_inst == R_IIC1) {
        ra_mstpcrb_stop(R_MSTP_MSTPCRB_MSTPB8_Msk);
    } else if (i2c_inst == R_IIC2) {
        ra_mstpcrb_stop(R_MSTP_MSTPCRB_MSTPB7_Msk);
    }
}

void ra_i2c_irq_enable(R_IIC0_Type *i2c_inst) {
    uint32_t ch = R_IIC0_Type_to_ch(i2c_inst);
    R_BSP_IrqEnable((IRQn_Type const)ra_i2c_ch_to_rxirq[ch]);
    R_BSP_IrqEnable((IRQn_Type const)ra_i2c_ch_to_txirq[ch]);
    R_BSP_IrqEnable((IRQn_Type const)ra_i2c_ch_to_teirq[ch]);
    R_BSP_IrqEnable((IRQn_Type const)ra_i2c_ch_to_erirq[ch]);
}

void ra_i2c_irq_disable(R_IIC0_Type *i2c_inst) {
    uint32_t ch = R_IIC0_Type_to_ch(i2c_inst);
    R_BSP_IrqDisable((IRQn_Type const)ra_i2c_ch_to_rxirq[ch]);
    R_BSP_IrqDisable((IRQn_Type const)ra_i2c_ch_to_txirq[ch]);
    R_BSP_IrqDisable((IRQn_Type const)ra_i2c_ch_to_teirq[ch]);
    R_BSP_IrqDisable((IRQn_Type const)ra_i2c_ch_to_erirq[ch]);
}

void ra_i2c_priority(R_IIC0_Type *i2c_inst, uint32_t ipl) {
    uint32_t ch = R_IIC0_Type_to_ch(i2c_inst);
    R_BSP_IrqCfg((IRQn_Type const)ra_i2c_ch_to_rxirq[ch], ipl, (void *)NULL);
    R_BSP_IrqCfg((IRQn_Type const)ra_i2c_ch_to_txirq[ch], ipl, (void *)NULL);
    R_BSP_IrqCfg((IRQn_Type const)ra_i2c_ch_to_teirq[ch], ipl, (void *)NULL);
    R_BSP_IrqCfg((IRQn_Type const)ra_i2c_ch_to_erirq[ch], ipl, (void *)NULL);
}

void ra_i2c_clear_IR(R_IIC0_Type *i2c_inst) {
    uint32_t ch = R_IIC0_Type_to_ch(i2c_inst);
    R_BSP_IrqStatusClear((IRQn_Type const)ra_i2c_ch_to_rxirq[ch]);
    R_BSP_IrqStatusClear((IRQn_Type const)ra_i2c_ch_to_txirq[ch]);
    R_BSP_IrqStatusClear((IRQn_Type const)ra_i2c_ch_to_teirq[ch]);
    R_BSP_IrqStatusClear((IRQn_Type const)ra_i2c_ch_to_erirq[ch]);
}

// ToDo: need to properly implement
static void ra_i2c_clock_calc(uint32_t baudrate, uint8_t *cks, uint8_t *brh, uint8_t *brl);
static void ra_i2c_clock_calc(uint32_t baudrate, uint8_t *cks, uint8_t *brh, uint8_t *brl) {
    #if defined(RA4M1)
    if (baudrate >= 400000) {
        // assume clock is 400000Hz (PCLKB 32MHz)
        *cks = 1;
        *brh = 9;
        *brl = 20;
    } else {
        // assume clock is 100000Hz (PCLKB 32MHz)
        *cks = 3;
        *brh = 15;
        *brl = 18;
    }
    #elif defined(RA4W1)
    if (baudrate >= 400000) {
        // assume clock is 400000Hz (PCLKB 32MHz)
        *cks = 1;
        *brh = 9;
        *brl = 20;
    } else if (baudrate >= 100000) {
        // assume clock is 100000Hz (PCLKB 32MHz)
        *cks = 3;
        *brh = 15;
        *brl = 18;
    } else {
        // assume clock is 50000Hz (PCLKB 32MHz)
        *cks = 4;
        *brh = 15;
        *brl = 18;
    }
    #elif defined(RA6M1)
    // PCLKB 60MHz SCLE=0
    if (baudrate >= 1000000) {
        *cks = 0;
        *brh = 15;
        *brl = 29;
    } else if (baudrate >= 400000) {
        // assume clock is 400000Hz (PCLKB 32MHz)
        *cks = 2;
        *brh = 8;
        *brl = 19;
    } else {
        // assume clock is 100000Hz (PCLKB 32MHz)
        *cks = 4;
        *brh = 14;
        *brl = 17;
    }
    #elif defined(RA4M2)
    // PCLKB 50MHz SCLE=0 (same as RA6M5)
    if (baudrate >= RA_I2C_CLOCK_MAX) {
        *cks = 0;
        *brh = 12;
        *brl = 24;
    } else if (baudrate >= 400000) {
        // assume clock is 400000Hz
        *cks = 2;
        *brh = 7;
        *brl = 15;
    } else if (baudrate >= 100000) {
        // assume clock is 100000Hz
        *cks = 3;
        *brh = 24;
        *brl = 30;
    } else {
        // assume clock is 50000Hz
        *cks = 4;
        *brh = 24;
        *brl = 30;
    }
    #elif defined(RA6M2) || defined(RA6M3)
    // PCLKB 60MHz SCLE=0
    if (baudrate >= RA_I2C_CLOCK_MAX) {
        *cks = 0;
        *brh = 15;
        *brl = 29;
    } else if (baudrate >= 400000) {
        // assume clock is 400000Hz
        *cks = 2;
        *brh = 8;
        *brl = 19;
    } else if (baudrate >= 100000) {
        // assume clock is 100000Hz
        *cks = 4;
        *brh = 14;
        *brl = 17;
    } else {
        // assume clock is 50000Hz
        *cks = 5;
        *brh = 14;
        *brl = 17;
    }
    #elif defined(RA6M5)
    // PCLKB 50MHz SCLE=0
    if (baudrate >= RA_I2C_CLOCK_MAX) {
        *cks = 0;
        *brh = 12;
        *brl = 24;
    } else if (baudrate >= 400000) {
        // assume clock is 400000Hz
        *cks = 2;
        *brh = 7;
        *brl = 15;
    } else if (baudrate >= 100000) {
        // assume clock is 100000Hz
        *cks = 3;
        *brh = 24;
        *brl = 30;
    } else {
        // assume clock is 50000Hz
        *cks = 4;
        *brh = 24;
        *brl = 30;
    }
    #else
    #error "CMSIS MCU Series is not specified."
    #endif
}

void ra_i2c_set_baudrate(R_IIC0_Type *i2c_inst, uint32_t baudrate) {
    uint8_t cks;
    uint8_t brh;
    uint8_t brl;
    ra_i2c_clock_calc(baudrate, &cks, &brh, &brl);
    i2c_inst->ICMR1_b.CKS = cks;
    i2c_inst->ICBRH_b.BRH = brh;
    i2c_inst->ICBRL_b.BRL = brl;
}

static bool ra_i2c_init_idle(R_IIC0_Type *i2c_inst, uint32_t scl, uint32_t sda, uint32_t baudrate) {
    ra_i2c_module_start(i2c_inst);
    ra_gpio_config(scl, GPIO_MODE_AF_OD, GPIO_NOPULL, GPIO_LOW_POWER, AF_I2C);
    ra_gpio_config(sda, GPIO_MODE_AF_OD, GPIO_NOPULL, GPIO_LOW_POWER, AF_I2C);
    ra_i2c_priority(i2c_inst, RA_PRI_I2C);
    i2c_inst->ICCR1_b.ICE = 0;     // I2C disable
    i2c_inst->ICCR1_b.IICRST = 1;  // I2C internal reset
    i2c_inst->ICIER = 0x00;        // I2C disable all interrupts
    if (i2c_inst->ICIER != 0) {
        return false; // Failed readback is a fault, never a CPU wait.
    }
    ra_i2c_clear_IR(i2c_inst);     // clear IR
    i2c_inst->ICCR1_b.ICE = 1;     // I2C enable
    ra_i2c_set_baudrate(i2c_inst, baudrate);
    i2c_inst->ICSER = 0x00;         // I2C reset bus status enable register
    i2c_inst->ICMR3_b.ACKWP = 0x00; // I2C not allow to write ACKBT (transfer acknowledge bit)
    i2c_inst->ICIER = 0xFF;         // Enable all interrupts
    i2c_inst->ICCR1_b.IICRST = 0;   // I2C internal reset
    ra_i2c_irq_enable(i2c_inst);
    last_stop = true;
    return true;
}

bool ra_i2c_bus_faulted(R_IIC0_Type *inst) {
    return bus_fault[R_IIC0_Type_to_ch(inst)];
}

bool ra_i2c_init(R_IIC0_Type *inst, uint32_t scl, uint32_t sda, uint32_t freq) {
    if (current_xaction != NULL) {
        return false;
    }
    // Explicit re-init clears a latched electrical fault only with idle lines.
    if (ra_i2c_bus_faulted(inst) && (!ra_gpio_read(scl) || !ra_gpio_read(sda))) {
        return false;
    }
    bool ok = ra_i2c_init_idle(inst, scl, sda, freq);
    bus_fault[R_IIC0_Type_to_ch(inst)] = !ok;
    return ok;
}

void ra_i2c_deinit(R_IIC0_Type *i2c_inst) {
    if (master_rx_dtc.open &&
        master_rx_dtc.channel == R_IIC0_Type_to_ch(i2c_inst)) {
        ra_i2c_master_rx_dtc_close();
    }
    if (master_tx_dtc.open &&
        master_tx_dtc.channel == R_IIC0_Type_to_ch(i2c_inst)) {
        ra_i2c_master_tx_dtc_close();
    }
    i2c_inst->ICIER = 0;        // I2C interrupt disable
    i2c_inst->ICCR1_b.ICE = 0;  // I2C disable
    ra_i2c_module_stop(i2c_inst);
    return;
}

static bool ra_i2c_dtc_quiesce_one(ra_i2c_master_dtc_t *dtc) {
    if (!dtc->open) {
        return true;
    }
    R_ICU->IELSR_b[dtc->ctrl.irq].DTCE = 0;
    __DSB();
    // FSP uses the activation event (IELS), NOT the NVIC IRQ index.
    uint32_t in_progress = 0x8000U | R_ICU->IELSR_b[dtc->ctrl.irq].IELS;
    return R_DTC->DTCSTS != in_progress;
}

static bool ra_i2c_dtc_quiesce(void) {
    bool rx = ra_i2c_dtc_quiesce_one(&master_rx_dtc);
    bool tx = ra_i2c_dtc_quiesce_one(&master_tx_dtc);
    return rx && tx;
}

bool ra_i2c_recovery_begin(R_IIC0_Type *inst, xaction_t *action,
    uint32_t scl, uint32_t sda, uint32_t freq, uint32_t now) {
    if (current_xaction != action || action == NULL ||
        (recovery.state != REC_IDLE && recovery.state != REC_DONE)) {
        return false;
    }
    ra_i2c_irq_disable(inst);
    inst->ICIER = 0;
    (void)ra_i2c_dtc_quiesce();
    recovery = (ra_i2c_recovery_t) {
        .inst = inst, .scl = scl, .sda = sda, .freq = freq,
        .started = now, .edge = now, .state = REC_QUIESCE,
    };
    return true; // current_xaction remains the C-level bus reservation.
}

static bool ra_i2c_recovery_finish(int result) {
    recovery.result = result;
    recovery.state = REC_DONE;
    if (result < 0) {
        bus_fault[R_IIC0_Type_to_ch(recovery.inst)] = true;
        if (recovery.buffer_safe) {
            ra_gpio_write(recovery.scl, 1);
            ra_gpio_write(recovery.sda, 1);
            ra_i2c_deinit(recovery.inst);
        }
    }
    current_xaction->m_status = RA_I2C_STATUS_Stopped;
    current_xaction->m_error = RA_I2C_ERROR_TMOF;
    last_stop = true;
    return true;
}

bool ra_i2c_recovery_buffer_safe(void) {
    return recovery.state == REC_IDLE || recovery.buffer_safe;
}

int ra_i2c_recovery_result(void) {
    return recovery.result;
}

bool ra_i2c_recovery_step(uint32_t now) {
    if (recovery.state == REC_IDLE || recovery.state == REC_DONE) {
        return true;
    }
    if (now - recovery.started >= RA_I2C_RECOVERY_DEADLINE_US) {
        return ra_i2c_recovery_finish(recovery.buffer_safe ? -3 : -5);
    }
    uint32_t elapsed = now - recovery.edge;
    switch (recovery.state) {
        case REC_QUIESCE:
            if (!ra_i2c_dtc_quiesce()) {
                break;
            }
            recovery.buffer_safe = true;
            ra_i2c_deinit(recovery.inst);
            ra_gpio_write(recovery.scl, 1);
            ra_gpio_write(recovery.sda, 1);
            ra_gpio_config(recovery.scl, GPIO_MODE_OUTPUT_OD, GPIO_NOPULL, GPIO_LOW_POWER, 0);
            ra_gpio_config(recovery.sda, GPIO_MODE_OUTPUT_OD, GPIO_NOPULL, GPIO_LOW_POWER, 0);
            recovery.edge = mp_hal_ticks_us();
            recovery.state = REC_BUS_CHECK;
            break;
        case REC_BUS_CHECK:
            if (!ra_gpio_read(recovery.scl)) {
                if (elapsed >= RA_I2C_RECOVERY_STRETCH_US) {
                    return ra_i2c_recovery_finish(-1);
                }
            } else {
                // An aborted read can leave a HIGH data bit, not an idle
                // slave. Complete nine clocks + STOP even if SDA is high now.
                ra_gpio_write(recovery.scl, 0);
                recovery.edge = mp_hal_ticks_us();
                recovery.state = REC_LOW;
            }
            break;
        case REC_LOW:
        case REC_STOP_LOW:
            if (elapsed >= RA_I2C_RECOVERY_HOLD_US) {
                ra_gpio_write(recovery.scl, 1);
                recovery.edge = mp_hal_ticks_us();
                recovery.state = recovery.state == REC_LOW ? REC_WAIT_HIGH : REC_STOP_WAIT_HIGH;
            }
            break;
        case REC_WAIT_HIGH:
        case REC_STOP_WAIT_HIGH:
            if (ra_gpio_read(recovery.scl)) {
                recovery.edge = mp_hal_ticks_us(); // AFTER observing actual high.
                recovery.state = recovery.state == REC_WAIT_HIGH ? REC_HIGH : REC_STOP_HIGH;
            } else if (elapsed >= RA_I2C_RECOVERY_STRETCH_US) {
                return ra_i2c_recovery_finish(-1);
            }
            break;
        case REC_HIGH:
            if (elapsed >= RA_I2C_RECOVERY_HOLD_US) {
                ra_gpio_write(recovery.scl, 0);
                recovery.edge = mp_hal_ticks_us();
                if (++recovery.pulses == 9) {
                    ra_gpio_write(recovery.sda, 0);
                    recovery.state = REC_STOP_LOW;
                } else {
                    recovery.state = REC_LOW;
                }
            }
            break;
        case REC_STOP_HIGH:
            if (elapsed >= RA_I2C_RECOVERY_HOLD_US) {
                ra_gpio_write(recovery.sda, 1);
                recovery.edge = mp_hal_ticks_us();
                recovery.state = REC_STOP_SETTLE;
            }
            break;
        case REC_STOP_SETTLE:
            if (elapsed >= RA_I2C_RECOVERY_HOLD_US) {
                if (!ra_gpio_read(recovery.scl)) {
                    return ra_i2c_recovery_finish(-1);
                }
                if (!ra_gpio_read(recovery.sda)) {
                    return ra_i2c_recovery_finish(-2);
                }
                recovery.state = REC_RESTORE;
            }
            break;
        case REC_RESTORE:
            if (!ra_gpio_read(recovery.scl) || !ra_gpio_read(recovery.sda)) {
                return ra_i2c_recovery_finish(-2);
            }
            return ra_i2c_recovery_finish(ra_i2c_init_idle(recovery.inst,
                recovery.scl, recovery.sda, recovery.freq) ? 1 : -4);
        default:
            break;
    }
    return false;
}

void ra_i2c_xaction_start(R_IIC0_Type *i2c_inst, xaction_t *action, bool repeated_start) {
    (void)repeated_start;
    if (last_stop == false) {
        i2c_inst->ICSR2_b.START = 0;
        i2c_inst->ICCR2_b.RS = 1;
        return; /* We still keep I2C bus */
    }
    if (i2c_inst->ICCR2_b.BBSY) {
        action->m_status = RA_I2C_STATUS_Stopped;
        action->m_error = RA_I2C_ERROR_BUSY;
        return;
    }
    i2c_inst->ICCR2_b.ST = 1;  // I2C start condition
}

void ra_i2c_xaction_stop() {
    // An error ends bus ownership even when the request omitted STOP.
    last_stop = current_xaction->m_stop || current_xaction->m_error != RA_I2C_ERROR_OK;
}

void ra_i2c_xunit_write_byte(R_IIC0_Type *i2c_inst, xaction_unit_t *unit) {
    i2c_inst->ICDRT = unit->buf[unit->m_bytes_transferred];
    ++unit->m_bytes_transferred;
    --unit->m_bytes_transfer;
}

void ra_i2c_xunit_read_byte(R_IIC0_Type *i2c_inst, xaction_unit_t *unit) {
    uint8_t data = i2c_inst->ICDRR;
    unit->buf[unit->m_bytes_transferred] = data;
    ++unit->m_bytes_transferred;
    --unit->m_bytes_transfer;
}

void ra_i2c_xunit_init(xaction_unit_t *unit, uint8_t *buf, uint32_t size, bool fread, void *next) {
    unit->m_bytes_transferred = 0;
    unit->m_bytes_transfer = size;
    unit->m_bytes_total = size;
    unit->m_fread = fread;
    unit->buf = buf;
    unit->next = (void *)next;
}

void ra_i2c_xaction_init(xaction_t *action, xaction_unit_t *units, uint32_t size, uint32_t address, bool stop) {
    action->units = units;
    action->m_num_of_units = size;
    action->m_current = 0;
    action->m_address = (((address << 1) & 0xFE) | (units->m_fread ? 0x1 : 0x0));
    action->m_status = RA_I2C_STATUS_Idle;
    action->m_error = RA_I2C_ERROR_OK;
    action->m_stop = stop;
    action->m_complete_callback = NULL;
    action->m_complete_context = NULL;
    action->m_completion_notified = false;
    action->m_rxi_irq_count = 0;
    action->m_dtc_transfer_count = 0;
    action->m_dtc_bytes = 0;
    action->m_dtc_fallback_count = 0;
    action->m_txi_irq_count = 0;
    action->m_dtc_tx_transfer_count = 0;
    action->m_dtc_tx_bytes = 0;
    action->m_dtc_tx_fallback_count = 0;
}

void ra_i2c_xaction_set_callback(xaction_t *action, ra_i2c_async_callback_t callback, void *context) {
    action->m_complete_callback = callback;
    action->m_complete_context = context;
}

static void ra_i2c_iceri_isr(R_IIC0_Type *i2c_inst) {
    xaction_t *action = current_xaction;
    if (i2c_inst->ICSR2_b.TMOF != 0) {
        ra_i2c_master_rx_dtc_close();
        ra_i2c_master_tx_dtc_close();
        action->m_error = RA_I2C_ERROR_TMOF;
        i2c_inst->ICSR2_b.TMOF = 0;
        i2c_inst->ICCR2_b.SP = 1; // request stop condition
    }
    if (i2c_inst->ICSR2_b.AL != 0) {
        ra_i2c_master_rx_dtc_close();
        ra_i2c_master_tx_dtc_close();
        action->m_error = RA_I2C_ERROR_AL;
        i2c_inst->ICSR2_b.AL = 0;
        i2c_inst->ICCR2_b.SP = 1; // request stop condition
    }
    // Check Start
    if (i2c_inst->ICSR2_b.START != 0) {
        if (action->m_status == RA_I2C_STATUS_Idle) {
            action->m_status = RA_I2C_STATUS_Started;
        }
        i2c_inst->ICSR2_b.START = 0;
    }
    // Check Stop
    if (i2c_inst->ICSR2_b.STOP != 0) {
        ra_i2c_master_rx_dtc_close();
        ra_i2c_master_tx_dtc_close();
        action->m_status = RA_I2C_STATUS_Stopped;
        i2c_inst->ICSR2_b.STOP = 0;
        i2c_inst->ICSR2_b.NACKF = 0; // clear for next transaction
    }
    // Check NACK reception
    if (i2c_inst->ICSR2_b.NACKF != 0) {
        ra_i2c_master_rx_dtc_close();
        ra_i2c_master_tx_dtc_close();
        action->m_error = RA_I2C_ERROR_NACK;
        i2c_inst->ICSR2_b.NACKF = 0;
        i2c_inst->ICCR2_b.SP = 1; // request stop condition
    }
}

static void ra_i2c_icrxi_isr(R_IIC0_Type *i2c_inst) {
    xaction_unit_t *unit = current_xaction_unit;
    xaction_t *action = current_xaction;
    action->m_rxi_irq_count++;

    if (ra_i2c_master_rx_dtc_finish_irq()) {
        return;
    }

    // 1 byte or 2 bytes
    if (unit->m_bytes_total <= 2) {
        if (action->m_status == RA_I2C_STATUS_AddrWriteCompleted) {
            action->m_status = RA_I2C_STATUS_FirstReceiveCompleted;
            i2c_inst->ICMR3_b.WAIT = 1;
            // need dummy read processes for 1 byte and 2 bytes receive
            if (unit->m_bytes_total == 2) {
                (void)i2c_inst->ICDRR; // dummy read for 2 bytes receive
            } else {  // m_bytes_total == 1
                i2c_inst->ICMR3_b.ACKWP = 0x01; // enable write ACKBT (transfer acknowledge bit)
                i2c_inst->ICMR3_b.ACKBT = 1;
                i2c_inst->ICMR3_b.ACKWP = 0x00; // disable write ACKBT (transfer acknowledge bit)
                (void)i2c_inst->ICDRR; // dummy read for 1 byte receive
            }
            return;
        }
        if (unit->m_bytes_transfer == 2) {      // last two data
            i2c_inst->ICMR3_b.ACKWP = 0x01; // enable write ACKBT (transfer acknowledge bit)
            i2c_inst->ICMR3_b.ACKBT = 1;
            i2c_inst->ICMR3_b.ACKWP = 0x00; // disable write ACKBT (transfer acknowledge bit)
            ra_i2c_xunit_read_byte(i2c_inst, unit);
        } else { // last data
            action->m_status = RA_I2C_STATUS_LastReceiveCompleted;
            if (action->m_stop == true) {
                i2c_inst->ICCR2_b.SP = 1; // request top condition
            }
            ra_i2c_xunit_read_byte(i2c_inst, unit);
        }
        return;
    }
    // 3 bytes or more
    if (action->m_status == RA_I2C_STATUS_AddrWriteCompleted) {
        if (unit->m_bytes_total > 3) {
            (void)ra_i2c_master_rx_dtc_arm(i2c_inst, action, unit);
        }
        (void)i2c_inst->ICDRR; // dummy read
        action->m_status = RA_I2C_STATUS_FirstReceiveCompleted;
        return;
    }
    if (unit->m_bytes_transfer > 2) {
        if (unit->m_bytes_transfer == 3) {
            i2c_inst->ICMR3_b.WAIT = 1;
        }
        ra_i2c_xunit_read_byte(i2c_inst, unit);
    } else if (unit->m_bytes_transfer == 2) {
        i2c_inst->ICMR3_b.ACKWP = 0x01; // enable write ACKBT (transfer acknowledge bit)
        i2c_inst->ICMR3_b.ACKBT = 1;
        i2c_inst->ICMR3_b.ACKWP = 0x00; // disable write ACKBT (transfer acknowledge bit)
        ra_i2c_xunit_read_byte(i2c_inst, unit);
    } else {
        // last data
        action->m_status = RA_I2C_STATUS_LastReceiveCompleted;
        if (action->m_stop == true) {
            i2c_inst->ICCR2_b.SP = 1; // request top condition
        }
        ra_i2c_xunit_read_byte(i2c_inst, unit);
    }
}

static void ra_i2c_ictxi_isr(R_IIC0_Type *i2c_inst) {
    xaction_t *action = current_xaction;
    xaction_unit_t *unit = current_xaction_unit;
    action->m_txi_irq_count++;
    bool dtc_completion = ra_i2c_master_tx_dtc_finish_irq();
    // When STIE is already checked.        When TIE occurs before STIE
    if (action->m_status == RA_I2C_STATUS_Started || action->m_status == RA_I2C_STATUS_Idle) {
        if (!unit->m_fread && unit->m_bytes_transfer != 0) {
            (void)ra_i2c_master_tx_dtc_arm(i2c_inst, action, unit);
        }
        i2c_inst->ICDRT = action->m_address;  // I2C send slave address
        action->m_status = RA_I2C_STATUS_AddrWriteCompleted;
        return;
    }
    if (action->m_status == RA_I2C_STATUS_AddrWriteCompleted &&
        unit->m_fread == false) {
        if (!dtc_completion && unit->m_bytes_transfer != 0) {
            ra_i2c_xunit_write_byte(i2c_inst, unit);
        } else {
            if (unit->next == (void *)NULL) {
                action->m_status = RA_I2C_STATUS_DataWriteCompleted;
            } else {
                current_xaction_unit = (xaction_unit_t *)unit->next;
                if (current_xaction_unit->m_bytes_transfer == 0) {
                    action->m_status = RA_I2C_STATUS_DataWriteCompleted;
                }
            }
        }
        return;
    }
}

static void ra_i2c_ictei_isr(R_IIC0_Type *i2c_inst) {
    xaction_t *action = current_xaction;
    i2c_inst->ICSR2_b.TEND = 0;
    action->m_current++;
    if (action->m_current == action->m_num_of_units) {
        // We wrote all data and received transfer end, so request stop condition
        if (action->m_stop == true) {
            action->m_status = RA_I2C_STATUS_DataSendCompleted;
            i2c_inst->ICCR2_b.SP = 1;
        } else {
            action->m_status = RA_I2C_STATUS_Stopped; // set Stopped status instead STOP condition
        }
    } else {
        ra_i2c_xaction_start(i2c_inst, action, true);
    }
}

void iic_master_rxi_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint8_t ch = irq_to_ch[(uint32_t)irq];
    if (iic_slave_mode[ch]) {
        iic_slave_rxi_isr();
        return;
    }
    if (current_xaction == NULL || ch != current_channel || recovery.state != REC_IDLE) {
        R_BSP_IrqStatusClear(irq);
        return;
    }
    xaction_t *action = current_xaction;
    ra_i2c_icrxi_isr(ch_to_R_IIC0_Type(ch));
    ra_i2c_xaction_notify_terminal(action);
    R_BSP_IrqStatusClear(irq);
}

void iic_master_txi_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint8_t ch = irq_to_ch[(uint32_t)irq];
    if (iic_slave_mode[ch]) {
        iic_slave_txi_isr();
        return;
    }
    if (current_xaction == NULL || ch != current_channel || recovery.state != REC_IDLE) {
        R_BSP_IrqStatusClear(irq);
        return;
    }
    xaction_t *action = current_xaction;
    ra_i2c_ictxi_isr(ch_to_R_IIC0_Type(ch));
    ra_i2c_xaction_notify_terminal(action);
    R_BSP_IrqStatusClear(irq);
}

void iic_master_tei_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint8_t ch = irq_to_ch[(uint32_t)irq];
    if (iic_slave_mode[ch]) {
        iic_slave_tei_isr();
        return;
    }
    if (current_xaction == NULL || ch != current_channel || recovery.state != REC_IDLE) {
        R_BSP_IrqStatusClear(irq);
        return;
    }
    xaction_t *action = current_xaction;
    ra_i2c_ictei_isr(ch_to_R_IIC0_Type(ch));
    ra_i2c_xaction_notify_terminal(action);
    R_BSP_IrqStatusClear(irq);
}

void iic_master_eri_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint8_t ch = irq_to_ch[(uint32_t)irq];
    if (iic_slave_mode[ch]) {
        iic_slave_eri_isr();
        return;
    }
    if (current_xaction == NULL || ch != current_channel || recovery.state != REC_IDLE) {
        R_BSP_IrqStatusClear(irq);
        return;
    }
    xaction_t *action = current_xaction;
    ra_i2c_iceri_isr(ch_to_R_IIC0_Type(ch));
    ra_i2c_xaction_notify_terminal(action);
    R_BSP_IrqStatusClear(irq);
}

bool ra_i2c_action_is_busy(void) {
    return current_xaction != NULL;
}

bool ra_i2c_action_start_async(R_IIC0_Type *i2c_inst, xaction_t *action, bool repeated_start) {
    if (i2c_inst == NULL || action == NULL || current_xaction != NULL || ra_i2c_bus_faulted(i2c_inst)) {
        if (action != NULL) {
            action->m_status = RA_I2C_STATUS_Stopped;
            action->m_error = RA_I2C_ERROR_BUSY;
        }
        return false;
    }

    recovery.state = REC_IDLE;
    current_channel = (uint8_t)R_IIC0_Type_to_ch(i2c_inst);
    current_xaction = action;
    current_xaction_unit = action->units;
    (void)ra_i2c_master_rx_dtc_prepare(i2c_inst, action);
    (void)ra_i2c_master_tx_dtc_prepare(i2c_inst, action);
    ra_i2c_xaction_start(i2c_inst, action, repeated_start);
    if (action->m_status == RA_I2C_STATUS_Stopped) {
        ra_i2c_master_rx_dtc_close();
        ra_i2c_master_tx_dtc_close();
    }
    ra_i2c_xaction_notify_terminal(action);
    return true;
}

ra_i2c_async_status_t ra_i2c_action_poll_async(xaction_t *action) {
    if (action == NULL) {
        return RA_I2C_ASYNC_ERROR;
    }
    if (current_xaction != action) {
        return action->m_status == RA_I2C_STATUS_Stopped &&
               action->m_error == RA_I2C_ERROR_OK
            ? RA_I2C_ASYNC_COMPLETE
            : RA_I2C_ASYNC_ERROR;
    }
    if (recovery.state != REC_IDLE && recovery.state != REC_DONE) {
        return RA_I2C_ASYNC_PENDING;
    }
    if (recovery.state == REC_DONE && !recovery.buffer_safe) {
        return RA_I2C_ASYNC_ERROR; // Quarantine descriptor, owner and buffer.
    }
    if (action->m_status != RA_I2C_STATUS_Stopped) {
        return RA_I2C_ASYNC_PENDING;
    }
    if (!ra_i2c_dtc_quiesce()) {
        return RA_I2C_ASYNC_PENDING;
    }

    ra_i2c_master_rx_dtc_close();
    ra_i2c_master_tx_dtc_close();
    ra_i2c_xaction_notify_terminal(action);
    // NACK/timeout/arbitration errors force STOP or lose bus ownership. Do
    // not request a repeated START on the next transaction after that STOP.
    last_stop = action->m_stop || action->m_error != RA_I2C_ERROR_OK;
    current_xaction = NULL;
    current_xaction_unit = NULL;
    return action->m_error == RA_I2C_ERROR_OK
        ? RA_I2C_ASYNC_COMPLETE
        : RA_I2C_ASYNC_ERROR;
}

void ra_i2c_action_cancel_async(R_IIC0_Type *i2c_inst, xaction_t *action) {
    if (i2c_inst == NULL || action == NULL || current_xaction != action) {
        return;
    }

    ra_i2c_irq_disable(i2c_inst);
    i2c_inst->ICIER = 0;
    if (!ra_i2c_dtc_quiesce()) {
        // Only the explicit synchronous facade / VM shutdown uses this path.
        // Never sweep a live DTC buffer. An unquiescent engine requires reset.
        NVIC_SystemReset();
    }
    recovery.state = REC_IDLE;
    ra_i2c_master_rx_dtc_close();
    ra_i2c_master_tx_dtc_close();
    i2c_inst->ICIER = 0;
    i2c_inst->ICCR1_b.IICRST = 1;
    ra_i2c_clear_IR(i2c_inst);
    i2c_inst->ICMR3_b.WAIT = 0;
    i2c_inst->ICMR3_b.ACKWP = 1;
    i2c_inst->ICMR3_b.ACKBT = 0;
    i2c_inst->ICMR3_b.ACKWP = 0;
    i2c_inst->ICIER = 0xFF;
    i2c_inst->ICCR1_b.IICRST = 0;

    action->m_status = RA_I2C_STATUS_Stopped;
    action->m_error = RA_I2C_ERROR_TMOF;
    last_stop = true;
    current_xaction = NULL;
    current_xaction_unit = NULL;
    ra_i2c_irq_enable(i2c_inst);
    ra_i2c_xaction_notify_terminal(action);
}

bool ra_i2c_action_execute(R_IIC0_Type *i2c_inst, xaction_t *action, bool repeated_start, uint32_t timeout_ms) {
    uint32_t start = uwTick;
    if (!ra_i2c_action_start_async(i2c_inst, action, repeated_start)) {
        return false;
    }

    ra_i2c_async_status_t status;
    do {
        status = ra_i2c_action_poll_async(action);
        if (status != RA_I2C_ASYNC_PENDING) {
            break;
        }
        if (uwTick - start > timeout_ms) {
            ra_i2c_action_cancel_async(i2c_inst, action);
            status = RA_I2C_ASYNC_ERROR;
            break;
        }
    } while (true);

    if (last_stop == true) {
        mp_hal_delay_ms(3);  // Preserve legacy synchronous transfer timing.
    }
    return status == RA_I2C_ASYNC_COMPLETE;
}
