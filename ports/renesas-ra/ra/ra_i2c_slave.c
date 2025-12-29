/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021,2022 Renesas Electronics Corporation
 * Copyright (c) 2025 MicroPython I2CTarget implementation
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
#include "hal_data.h"
#include "ra_config.h"
#include "ra_gpio.h"
#include "ra_icu.h"
#include "ra_int.h"
#include "ra_utils.h"
#include "ra_i2c.h"
#include "ra_i2c_slave.h"

#if !defined(RA_PRI_I2C_SLAVE)
#define RA_PRI_I2C_SLAVE (8)
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

// Static slave objects for each channel
static ra_i2c_slave_obj_t ra_i2c_slave_obj[RA_I2C_SLAVE_MAX_INSTANCES];

// IRQ vector tables are declared in ra_i2c.h and defined in ra_i2c.c

// Helper: Get channel from I2C instance
static uint32_t i2c_inst_to_ch(R_IIC0_Type *i2c_inst) {
    if (i2c_inst == R_IIC1) {
        return 1;
    }
    #ifdef R_IIC2
    if (i2c_inst == R_IIC2) {
        return 2;
    }
    #endif
    return 0;
}

// Helper: Get I2C instance from channel
static R_IIC0_Type *ch_to_i2c_inst(uint32_t ch) {
    if (ch == 1) {
        return R_IIC1;
    }
    #ifdef R_IIC2
    if (ch == 2) {
        return R_IIC2;
    }
    #endif
    return R_IIC0;
}

// Get slave object by channel
ra_i2c_slave_obj_t *ra_i2c_slave_get_obj(uint32_t ch) {
    if (ch < RA_I2C_SLAVE_MAX_INSTANCES) {
        return &ra_i2c_slave_obj[ch];
    }
    return NULL;
}

// Module start/stop (same as master)
static void ra_i2c_slave_module_start(R_IIC0_Type *i2c_inst) {
    if (i2c_inst == R_IIC0) {
        ra_mstpcrb_start(R_MSTP_MSTPCRB_MSTPB9_Msk);
    } else if (i2c_inst == R_IIC1) {
        ra_mstpcrb_start(R_MSTP_MSTPCRB_MSTPB8_Msk);
    }
    #ifdef R_IIC2
    else if (i2c_inst == R_IIC2) {
        ra_mstpcrb_start(R_MSTP_MSTPCRB_MSTPB7_Msk);
    }
    #endif
}

static void ra_i2c_slave_module_stop(R_IIC0_Type *i2c_inst) {
    if (i2c_inst == R_IIC0) {
        ra_mstpcrb_stop(R_MSTP_MSTPCRB_MSTPB9_Msk);
    } else if (i2c_inst == R_IIC1) {
        ra_mstpcrb_stop(R_MSTP_MSTPCRB_MSTPB8_Msk);
    }
    #ifdef R_IIC2
    else if (i2c_inst == R_IIC2) {
        ra_mstpcrb_stop(R_MSTP_MSTPCRB_MSTPB7_Msk);
    }
    #endif
}

// IRQ enable/disable for slave
static void ra_i2c_slave_irq_enable(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    R_BSP_IrqEnable((IRQn_Type)ra_i2c_ch_to_rxirq[ch]);
    R_BSP_IrqEnable((IRQn_Type)ra_i2c_ch_to_txirq[ch]);
    R_BSP_IrqEnable((IRQn_Type)ra_i2c_ch_to_teirq[ch]);
    R_BSP_IrqEnable((IRQn_Type)ra_i2c_ch_to_erirq[ch]);
}

static void ra_i2c_slave_irq_disable(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    R_BSP_IrqDisable((IRQn_Type)ra_i2c_ch_to_rxirq[ch]);
    R_BSP_IrqDisable((IRQn_Type)ra_i2c_ch_to_txirq[ch]);
    R_BSP_IrqDisable((IRQn_Type)ra_i2c_ch_to_teirq[ch]);
    R_BSP_IrqDisable((IRQn_Type)ra_i2c_ch_to_erirq[ch]);
}

static void ra_i2c_slave_irq_priority(R_IIC0_Type *i2c_inst, uint32_t ipl) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    R_BSP_IrqCfg((IRQn_Type)ra_i2c_ch_to_rxirq[ch], ipl, NULL);
    R_BSP_IrqCfg((IRQn_Type)ra_i2c_ch_to_txirq[ch], ipl, NULL);
    R_BSP_IrqCfg((IRQn_Type)ra_i2c_ch_to_teirq[ch], ipl, NULL);
    R_BSP_IrqCfg((IRQn_Type)ra_i2c_ch_to_erirq[ch], ipl, NULL);
}

static void ra_i2c_slave_clear_ir(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    R_BSP_IrqStatusClear((IRQn_Type)ra_i2c_ch_to_rxirq[ch]);
    R_BSP_IrqStatusClear((IRQn_Type)ra_i2c_ch_to_txirq[ch]);
    R_BSP_IrqStatusClear((IRQn_Type)ra_i2c_ch_to_teirq[ch]);
    R_BSP_IrqStatusClear((IRQn_Type)ra_i2c_ch_to_erirq[ch]);
}

// Initialize I2C in slave mode
void ra_i2c_slave_init(ra_i2c_slave_obj_t *self, R_IIC0_Type *i2c_inst,
    uint32_t scl, uint32_t sda, uint16_t addr, bool addr_10bit) {

    self->i2c_inst = i2c_inst;
    self->scl_pin = scl;
    self->sda_pin = sda;
    self->addr = addr;
    self->addr_10bit = addr_10bit;
    self->state = RA_I2C_SLAVE_STATE_IDLE;
    self->pending_events = RA_I2C_SLAVE_EVENT_NONE;
    self->callback = NULL;
    self->callback_arg = NULL;
    self->tx_in_progress = false;
    self->rx_in_progress = false;

    // Start module clock
    ra_i2c_slave_module_start(i2c_inst);

    // Configure GPIO pins for I2C
    ra_gpio_config(scl, GPIO_MODE_AF_OD, GPIO_NOPULL, GPIO_LOW_POWER, AF_I2C);
    ra_gpio_config(sda, GPIO_MODE_AF_OD, GPIO_NOPULL, GPIO_LOW_POWER, AF_I2C);

    // Set IRQ priority
    ra_i2c_slave_irq_priority(i2c_inst, RA_PRI_I2C_SLAVE);

    // Disable I2C and reset
    i2c_inst->ICCR1_b.ICE = 0;
    i2c_inst->ICCR1_b.IICRST = 1;

    // Disable all interrupts during setup
    i2c_inst->ICIER = 0x00;
    while (i2c_inst->ICIER != 0) {
        ;
    }

    // Clear interrupt flags
    ra_i2c_slave_clear_ir(i2c_inst);

    // Enable I2C
    i2c_inst->ICCR1_b.ICE = 1;

    // Configure slave address using SAR[0] (Slave Address Register 0)
    // SAR[0].L: Slave address bits [7:1] in bits [7:1], bit 0 is R/W (not used here)
    // SAR[0].U: FS bit for 10-bit mode, SVA8/SVA9 for upper address bits
    if (addr_10bit) {
        // 10-bit addressing mode
        i2c_inst->SAR[0].L = (uint8_t)(addr & 0xFF);
        i2c_inst->SAR[0].U = (uint8_t)(((addr >> 8) & 0x03) << 1) | 0x01; // SVA8/9 + FS=1 (10-bit)
    } else {
        // 7-bit addressing mode
        i2c_inst->SAR[0].L = (uint8_t)(addr << 1);
        i2c_inst->SAR[0].U = 0x00;  // FS=0 (7-bit mode)
    }

    // Enable slave address 0 detection
    i2c_inst->ICSER = 0x01;  // SAR0E = 1, enable slave address 0

    // Configure slave mode settings
    // ICMR3: ACK bit control
    i2c_inst->ICMR3_b.ACKWP = 0x00;  // Disable ACKBT write protection initially
    i2c_inst->ICMR3_b.ACKBT = 0;     // Send ACK

    // Enable interrupts:
    // - RIE: Receive data full
    // - TIE: Transmit data empty
    // - TEIE: Transmit end
    // - NAKIE: NACK detection
    // - STIE: Start condition detection
    // - SPIE: Stop condition detection
    i2c_inst->ICIER = 0xFF;  // Enable all interrupts

    // Release reset
    i2c_inst->ICCR1_b.IICRST = 0;

    // Enable IRQs
    ra_i2c_slave_irq_enable(i2c_inst);
}

// Deinitialize I2C slave
void ra_i2c_slave_deinit(ra_i2c_slave_obj_t *self) {
    R_IIC0_Type *i2c_inst = self->i2c_inst;

    // Disable IRQs
    ra_i2c_slave_irq_disable(i2c_inst);

    // Disable interrupts
    i2c_inst->ICIER = 0x00;

    // Disable slave address detection
    i2c_inst->ICSER = 0x00;

    // Disable I2C
    i2c_inst->ICCR1_b.ICE = 0;

    // Stop module clock
    ra_i2c_slave_module_stop(i2c_inst);

    // Reset state
    self->state = RA_I2C_SLAVE_STATE_IDLE;
    self->callback = NULL;
}

// Set callback for slave events
void ra_i2c_slave_set_callback(ra_i2c_slave_obj_t *self,
    ra_i2c_slave_callback_t callback, void *arg) {
    self->callback = callback;
    self->callback_arg = arg;
}

// Read received byte from RX register
size_t ra_i2c_slave_read(ra_i2c_slave_obj_t *self, uint8_t *buf, size_t len) {
    R_IIC0_Type *i2c_inst = self->i2c_inst;
    size_t count = 0;

    // Check if data is available in receive buffer
    if (i2c_inst->ICSR2_b.RDRF && len > 0) {
        buf[count++] = i2c_inst->ICDRR;
        self->rx_in_progress = false;
    }

    return count;
}

// Write byte to TX register
size_t ra_i2c_slave_write(ra_i2c_slave_obj_t *self, const uint8_t *buf, size_t len) {
    R_IIC0_Type *i2c_inst = self->i2c_inst;
    size_t count = 0;

    // Check if TX buffer is empty
    if (i2c_inst->ICSR2_b.TDRE && len > 0) {
        i2c_inst->ICDRT = buf[count++];
        self->tx_in_progress = true;
    }

    return count;
}

// Check if RX data is available
bool ra_i2c_slave_rx_available(ra_i2c_slave_obj_t *self) {
    return self->i2c_inst->ICSR2_b.RDRF != 0;
}

// Check if TX buffer is ready
bool ra_i2c_slave_tx_ready(ra_i2c_slave_obj_t *self) {
    return self->i2c_inst->ICSR2_b.TDRE != 0;
}

// Get current state
ra_i2c_slave_state_t ra_i2c_slave_get_state(ra_i2c_slave_obj_t *self) {
    return self->state;
}

// Clear pending events
void ra_i2c_slave_clear_events(ra_i2c_slave_obj_t *self, ra_i2c_slave_event_t events) {
    self->pending_events &= ~events;
}

// Get and clear pending events
ra_i2c_slave_event_t ra_i2c_slave_get_events(ra_i2c_slave_obj_t *self) {
    ra_i2c_slave_event_t events = self->pending_events;
    self->pending_events = RA_I2C_SLAVE_EVENT_NONE;
    return events;
}

/******************************************************************************/
// ISR Handlers

// Helper: Invoke callback if set
static void ra_i2c_slave_invoke_callback(ra_i2c_slave_obj_t *self, ra_i2c_slave_event_t event) {
    self->pending_events |= event;
    if (self->callback != NULL) {
        self->callback(self, event);
    }
}

// RXI ISR: Receive data register full - master has written a byte
static void ra_i2c_slave_rxi_handler(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    ra_i2c_slave_obj_t *self = &ra_i2c_slave_obj[ch];

    // Check if this is the first byte after address match
    if (self->state == RA_I2C_SLAVE_STATE_ADDR_MATCH) {
        self->state = RA_I2C_SLAVE_STATE_RX;
    }

    self->rx_in_progress = true;

    // Notify: data ready to read
    ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_RX_READY);
}

// TXI ISR: Transmit data register empty - master is requesting a byte
static void ra_i2c_slave_txi_handler(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    ra_i2c_slave_obj_t *self = &ra_i2c_slave_obj[ch];

    // Check if this is the first TX after address match
    if (self->state == RA_I2C_SLAVE_STATE_ADDR_MATCH ||
        self->state == RA_I2C_SLAVE_STATE_IDLE) {
        self->state = RA_I2C_SLAVE_STATE_TX;
    }

    self->tx_in_progress = false;

    // Notify: ready to write next byte
    ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_TX_READY);
}

// TEI ISR: Transmit end - last byte has been transmitted
static void ra_i2c_slave_tei_handler(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    ra_i2c_slave_obj_t *self = &ra_i2c_slave_obj[ch];

    // Clear TEND flag
    i2c_inst->ICSR2_b.TEND = 0;

    self->tx_in_progress = false;
}

// ERI ISR: Error and event detection
static void ra_i2c_slave_eri_handler(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    ra_i2c_slave_obj_t *self = &ra_i2c_slave_obj[ch];

    // Check for START condition (address match)
    if (i2c_inst->ICSR2_b.START != 0) {
        i2c_inst->ICSR2_b.START = 0;
        self->state = RA_I2C_SLAVE_STATE_ADDR_MATCH;
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_ADDR_MATCH);
    }

    // Check for STOP condition
    if (i2c_inst->ICSR2_b.STOP != 0) {
        i2c_inst->ICSR2_b.STOP = 0;
        self->state = RA_I2C_SLAVE_STATE_IDLE;
        self->tx_in_progress = false;
        self->rx_in_progress = false;
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_STOP);
    }

    // Check for NACK from master
    if (i2c_inst->ICSR2_b.NACKF != 0) {
        i2c_inst->ICSR2_b.NACKF = 0;
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_NACK);
    }

    // Check for timeout
    if (i2c_inst->ICSR2_b.TMOF != 0) {
        i2c_inst->ICSR2_b.TMOF = 0;
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_ERROR);
    }

    // Check for arbitration loss (shouldn't happen in slave mode)
    if (i2c_inst->ICSR2_b.AL != 0) {
        i2c_inst->ICSR2_b.AL = 0;
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_ERROR);
    }
}

// Public ISR entry points
void iic_slave_rxi_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint8_t ch = irq_to_ch[(uint32_t)irq];
    ra_i2c_slave_rxi_handler(ch_to_i2c_inst(ch));
    R_BSP_IrqStatusClear(irq);
}

void iic_slave_txi_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint8_t ch = irq_to_ch[(uint32_t)irq];
    ra_i2c_slave_txi_handler(ch_to_i2c_inst(ch));
    R_BSP_IrqStatusClear(irq);
}

void iic_slave_tei_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint8_t ch = irq_to_ch[(uint32_t)irq];
    ra_i2c_slave_tei_handler(ch_to_i2c_inst(ch));
    R_BSP_IrqStatusClear(irq);
}

void iic_slave_eri_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint8_t ch = irq_to_ch[(uint32_t)irq];
    ra_i2c_slave_eri_handler(ch_to_i2c_inst(ch));
    R_BSP_IrqStatusClear(irq);
}

