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

// Set to 1 to enable debug printf output for I2C slave driver
#ifndef RA_I2C_SLAVE_DEBUG
#define RA_I2C_SLAVE_DEBUG 0
#endif
#include "ra_icu.h"
#include "ra_int.h"
#include "ra_utils.h"
#include "ra_i2c.h"
#include "ra_i2c_slave.h"

 #if !defined(RA_PRI_I2C_SLAVE)
 #define RA_PRI_I2C_SLAVE (8)
 #endif

// IIC ICSR1 address-detect flags (RA4M1 IIC): b0..b7 = AAS0,AAS1,AAS2,GCA,-,DID,-,HOA
// We use these to confirm that the current transaction is actually targeting
// this slave before treating any RXI/TXI as an address match.
 #define RA_I2C_SLAVE_ICSR1_ADDR_MASK (0xAFu) // HOA|DID|GCA|AAS2|AAS1|AAS0

static inline bool ra_i2c_slave_hw_addr_match(R_IIC0_Type *i2c) {
    return (0u != (i2c->ICSR1 & RA_I2C_SLAVE_ICSR1_ADDR_MASK));
}

 #if defined(__GNUC__)
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wconversion"
 #endif

// Static slave objects for each channel
static ra_i2c_slave_obj_t ra_i2c_slave_obj[RA_I2C_SLAVE_MAX_INSTANCES];

// IRQ vector tables are declared in ra_i2c.h and defined in ra_i2c.c

#if RA_I2C_ENABLE_DTC
// Reset DTC state structure to a known idle state.
static void ra_i2c_slave_dtc_reset_state(ra_i2c_dtc_state_t *dtc) {
    if (dtc == NULL) {
        return;
    }

    dtc->rx.buf = NULL;
    dtc->rx.capacity = 0u;
    dtc->rx.count = 0u;
    dtc->rx.remaining = 0u;
    dtc->rx.active = false;

    dtc->tx.buf = NULL;
    dtc->tx.capacity = 0u;
    dtc->tx.count = 0u;
    dtc->tx.remaining = 0u;
    dtc->tx.active = false;

    dtc->rx_channel = 0u;
    dtc->tx_channel = 0u;
}

// For now, the DTC backend is a stub: it only tracks software state and does not
// program the hardware DTC engine. This keeps the API and data structures
// stable while we design the real backend in the next step.
bool ra_i2c_slave_dtc_init(ra_i2c_slave_obj_t *self, size_t mem_size) {
    (void) mem_size;  // logical size of the register file, not used yet
    if (self == NULL) {
        return false;
    }

    self->use_dtc = false;
    ra_i2c_slave_dtc_reset_state(&self->dtc);
    return true;
}

void ra_i2c_slave_dtc_deinit(ra_i2c_slave_obj_t *self) {
    if (self == NULL) {
        return;
    }

    self->use_dtc = false;
    ra_i2c_slave_dtc_reset_state(&self->dtc);
}

void ra_i2c_slave_dtc_prepare_rx(ra_i2c_slave_obj_t *self, size_t max_len) {
    (void) self;
    (void) max_len;
}

void ra_i2c_slave_dtc_prepare_tx(ra_i2c_slave_obj_t *self, size_t len) {
    (void) self;
    (void) len;
}

size_t ra_i2c_slave_dtc_get_rx_count(ra_i2c_slave_obj_t *self) {
    (void) self;
    return 0u;
}

size_t ra_i2c_slave_dtc_get_tx_count(ra_i2c_slave_obj_t *self) {
    (void) self;
    return 0u;
}
#endif

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

// Auto-recovery: soft-reset IIC peripheral when error threshold is reached.
// Called from ERI handler when recovery_count >= RA_I2C_SLAVE_RECOVERY_THRESHOLD.
static void ra_i2c_slave_auto_reset(ra_i2c_slave_obj_t *self) {
    R_IIC0_Type *i2c_inst = self->i2c_inst;

    #if RA_I2C_SLAVE_DEBUG
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    printf("[ra_i2c_slave] ch%lu: AUTO-RESET triggered (recovery_count=%u)\n",
           (unsigned long) ch, (unsigned int) self->recovery_count);
    #endif

    // Disable IRQs during reset
    ra_i2c_slave_irq_disable(i2c_inst);

    // Save current ICIER and slave address settings
    uint8_t saved_icier = self->icier_mask;
    uint8_t saved_sarl = i2c_inst->SAR[0].L;
    uint8_t saved_saru = i2c_inst->SAR[0].U;
    uint8_t saved_icser = i2c_inst->ICSER;

    // Disable I2C and assert internal reset
    i2c_inst->ICIER = 0x00;
    i2c_inst->ICCR1_b.ICE = 0;
    i2c_inst->ICCR1_b.IICRST = 1;

    // Short delay for reset to take effect (hardware requires a few cycles)
    for (volatile int i = 0; i < 10; i++) {
        __asm__ volatile ("nop");
    }

    // Clear all status flags
    i2c_inst->ICSR1 = 0x00;
    i2c_inst->ICSR2 = 0x00;

    // Clear pending IRQs
    ra_i2c_slave_clear_ir(i2c_inst);

    // Re-enable I2C
    i2c_inst->ICCR1_b.ICE = 1;

    // Restore slave address configuration
    i2c_inst->SAR[0].L = saved_sarl;
    i2c_inst->SAR[0].U = saved_saru;
    i2c_inst->ICSER = saved_icser;

    // Reset ACK bit
    i2c_inst->ICMR3_b.ACKWP = 1;
    i2c_inst->ICMR3_b.ACKBT = 0;
    i2c_inst->ICMR3_b.ACKWP = 0;

    // Restore interrupt enables
    i2c_inst->ICIER = saved_icier;

    // Release internal reset
    i2c_inst->ICCR1_b.IICRST = 0;

    // Reset state machine
    self->state = RA_I2C_SLAVE_STATE_IDLE;
    self->addr_match_active = false;
    self->tx_in_progress = false;
    self->rx_in_progress = false;
    self->recovery_count = 0u;

    // Re-enable IRQs
    ra_i2c_slave_irq_enable(i2c_inst);
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
    self->addr_match_active = false;
    self->icier_mask = 0u;
    self->recovery_count = 0u;

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

    // Program the default interrupt sources for slave operation:
    // - RIE:  Receive data full (RXI)
    // - TIE:  Transmit data empty (TXI)
    // - NAKIE: NACK detection (used for END_READ)
    // - SPIE: Stop condition detection (END_WRITE) - dynamically enabled
    // - ALIE/TMOIE: Arbitration loss / timeout as error sources
    //
    // START-detect (STIE) and transmit-end (TEIE) are intentionally kept
    // disabled here. START is not a reliable indicator of address match on
    // a shared bus, and TX end can be inferred from NACK/STOP.
    self->addr_match_active = false;

    // Base IRQ mask (without SPIE). SPIE is enabled dynamically only while an
    // addressed transaction is active (see ra_i2c_slave_set_icier_mask()).
    self->icier_mask = (uint8_t)(
        RA_I2C_SLAVE_ICIER_TIE
        | RA_I2C_SLAVE_ICIER_RIE
        | RA_I2C_SLAVE_ICIER_NAKIE
        | RA_I2C_SLAVE_ICIER_ALIE
        | RA_I2C_SLAVE_ICIER_TMOIE);

    ra_i2c_slave_set_icier_mask(self, self->icier_mask);

    // Debug: log configured slave address and key registers so we can
    // verify at runtime that the hardware is programmed with the
    // expected 7-bit/10-bit address and that slave detection is
    // enabled on the correct channel.
    #if RA_I2C_SLAVE_DEBUG
    {
        uint32_t ch = i2c_inst_to_ch(i2c_inst);
        printf("[ra_i2c_slave] init ch%lu addr=0x%04x SARL=0x%02x SARU=0x%02x ICSER=0x%02x ICIER=0x%02x ICSR2=0x%02x\n",
            (unsigned long) ch,
            (unsigned int) addr,
            (unsigned int) i2c_inst->SAR[0].L,
            (unsigned int) i2c_inst->SAR[0].U,
            (unsigned int) i2c_inst->ICSER,
            (unsigned int) i2c_inst->ICIER,
            (unsigned int) i2c_inst->ICSR2);
    }
    #endif

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
    self->addr_match_active = false;
    self->icier_mask = 0u;
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
	        uint8_t value = i2c_inst->ICDRR;
	        buf[count++] = value;
	        self->rx_in_progress = false;
	        #if RA_I2C_SLAVE_DEBUG
	        // Debug: log received data to confirm RX path is active and
	        // machine_i2c_target_data_write_request() is calling into this helper.
	        uint32_t ch = i2c_inst_to_ch(i2c_inst);
	        printf("[ra_i2c_slave] ch%lu READ byte=0x%02x\n",
	               (unsigned long) ch, (unsigned int) value);
	        #endif
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

// Update the enabled interrupt sources for this slave instance. RXI/TXI are
// always required for correct operation and are therefore forced on.
void ra_i2c_slave_set_icier_mask(ra_i2c_slave_obj_t *self, uint8_t mask) {
    if (self == NULL || self->i2c_inst == NULL) {
        return;
    }

    // Always keep the core slave engine functional.
    mask = (uint8_t)(mask | RA_I2C_SLAVE_ICIER_TIE | RA_I2C_SLAVE_ICIER_RIE);

    // Store the "base" mask requested by upper layers (without dynamic bits).
    self->icier_mask = mask;

    // E1 fix: Dynamic STOP interrupt gating.
    // On some RA IIC configurations STOP detection can wake the CPU even when this
    // slave is not addressed. Keep SPIE enabled only while we are inside an
    // addressed transaction (addr_match_active).
    uint8_t hw_mask = mask;
    if (self->addr_match_active) {
        hw_mask = (uint8_t)(hw_mask | RA_I2C_SLAVE_ICIER_SPIE);
    } else {
        hw_mask = (uint8_t)(hw_mask & (uint8_t)~RA_I2C_SLAVE_ICIER_SPIE);
    }

    self->i2c_inst->ICIER = hw_mask;
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

    // Confirm address match using ICSR1 flags. RXI is generated only when the
    // target address matched, but on a shared bus multiple slaves may see
    // START. We use AASx/GCA/DID/HOA to decide if this RXI really belongs to
    // this slave.
    if (!self->addr_match_active
        && self->state == RA_I2C_SLAVE_STATE_IDLE
        && ra_i2c_slave_hw_addr_match(i2c_inst)) {
        self->addr_match_active = true;
        // E1 fix: enable SPIE only while addressed.
        ra_i2c_slave_set_icier_mask(self, self->icier_mask);
        self->state = RA_I2C_SLAVE_STATE_ADDR_MATCH;
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_ADDR_MATCH);
    }

    // On RA hardware, the first RXI after an address match delivers the
    // SLA+R/W byte via ICDRR. That byte is part of the address phase and
    // must be consumed here to complete the handshake, but must not be
    // reported up as payload data to the generic I2CTarget layer.
    if (self->state == RA_I2C_SLAVE_STATE_ADDR_MATCH) {
        (void)i2c_inst->ICDRR; // Discard SLA+R/W
        self->state = RA_I2C_SLAVE_STATE_RX;
        self->rx_in_progress = false;
        return;
    }

    self->rx_in_progress = true;

    // Notify: data ready to read
    ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_RX_READY);
}

// TXI ISR: Transmit data register empty - master is requesting a byte
static void ra_i2c_slave_txi_handler(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    ra_i2c_slave_obj_t *self = &ra_i2c_slave_obj[ch];

    // For read transactions the first interrupt after address match may be
    // TXI rather than RXI. Use the same ICSR1-based check to latch a true
    // address match.
    if (!self->addr_match_active
        && self->state == RA_I2C_SLAVE_STATE_IDLE
        && ra_i2c_slave_hw_addr_match(i2c_inst)) {
        self->addr_match_active = true;
        self->state = RA_I2C_SLAVE_STATE_ADDR_MATCH;
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_ADDR_MATCH);
    }

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

    // ERI ISR: Error and event detection (START/STOP/NACK/TIMEOUT)
static void ra_i2c_slave_eri_handler(R_IIC0_Type *i2c_inst) {
    uint32_t ch = i2c_inst_to_ch(i2c_inst);
    ra_i2c_slave_obj_t *self = &ra_i2c_slave_obj[ch];

    #if RA_I2C_SLAVE_DEBUG
    // Debug: print raw ICSR2 status and decoded flag bits on each ERI
    // entry to see exactly which events fire on the bus.
    printf("[ra_i2c_slave] ERI ch=%lu ICSR2=0x%02x START=%u STOP=%u NACKF=%u RDRF=%u TMOF=%u AL=%u\n",
           (unsigned long) ch,
           (unsigned int) i2c_inst->ICSR2,
           (unsigned int) i2c_inst->ICSR2_b.START,
           (unsigned int) i2c_inst->ICSR2_b.STOP,
           (unsigned int) i2c_inst->ICSR2_b.NACKF,
           (unsigned int) i2c_inst->ICSR2_b.RDRF,
           (unsigned int) i2c_inst->ICSR2_b.TMOF,
           (unsigned int) i2c_inst->ICSR2_b.AL);
    #endif

	    // Check for START condition.
	    //
	    // IMPORTANT: Do NOT treat START as an automatic address match. On a
	    // multi-drop I2C bus every slave will see START for all traffic, but
	    // only the addressed slave will observe AASx/GCA/DID/HOA in ICSR1 and
	    // subsequently receive RXI/TXI interrupts. Real address match is now
	    // detected lazily inside the RXI/TXI handlers using ICSR1.
	    if (i2c_inst->ICSR2_b.START != 0) {
	        i2c_inst->ICSR2_b.START = 0; // clear only
	    }

    // E1 fix: If we are idle and not in an addressed transaction, ignore STOP/NAK
    // events (but still clear the HW flags). This avoids spurious wakeups on a
    // shared bus when the bus is used by other masters/slaves.
    if (!self->addr_match_active && self->state == RA_I2C_SLAVE_STATE_IDLE) {
        if (i2c_inst->ICSR2_b.STOP != 0) {
            i2c_inst->ICSR2_b.STOP = 0;
        }
        if (i2c_inst->ICSR2_b.NACKF != 0) {
            i2c_inst->ICSR2_b.NACKF = 0;
        }
        // Still process TIMEOUT/AL below for recovery purposes
    }

    // Check for STOP condition
    if (i2c_inst->ICSR2_b.STOP != 0) {
        i2c_inst->ICSR2_b.STOP = 0;
        self->addr_match_active = false;
        // E1 fix: disable SPIE when idle.
        ra_i2c_slave_set_icier_mask(self, self->icier_mask);
        self->state = RA_I2C_SLAVE_STATE_IDLE;
        self->tx_in_progress = false;
        self->rx_in_progress = false;
        // Clean STOP: reset recovery counter (successful transaction)
        self->recovery_count = 0u;
        // Force ACK for next transaction (reset ACKBT to 0)
        // E2 fix: correct ACKWP sequence (0 to allow write, then 1 to protect)
        i2c_inst->ICMR3_b.ACKWP = 0;
        i2c_inst->ICMR3_b.ACKBT = 0;
        i2c_inst->ICMR3_b.ACKWP = 1;
        #if RA_I2C_SLAVE_DEBUG
        printf("[ra_i2c_slave] ch%lu: STOP (recovery_count reset)\n", (unsigned long) ch);
        #endif
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_STOP);
    }

    // Check for NACK from master
    if (i2c_inst->ICSR2_b.NACKF != 0) {
        i2c_inst->ICSR2_b.NACKF = 0;
        #if RA_I2C_SLAVE_DEBUG
        printf("[ra_i2c_slave] ch%lu: NACK\n", (unsigned long) ch);
        #endif
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_NACK);
    }

    // Check for timeout
    if (i2c_inst->ICSR2_b.TMOF != 0) {
        i2c_inst->ICSR2_b.TMOF = 0;
        self->addr_match_active = false;
        // E1 fix: disable SPIE when idle.
        ra_i2c_slave_set_icier_mask(self, self->icier_mask);
        // Increment recovery counter
        self->recovery_count++;
        #if RA_I2C_SLAVE_DEBUG
        printf("[ra_i2c_slave] ch%lu: TIMEOUT (recovery_count=%u)\n",
               (unsigned long) ch, (unsigned int) self->recovery_count);
        #endif
        // Check if auto-reset threshold reached
        if (self->recovery_count >= RA_I2C_SLAVE_RECOVERY_THRESHOLD) {
            ra_i2c_slave_auto_reset(self);
            return; // Auto-reset already clears state and resets counter
        }
        // Force ACK for next transaction (reset ACKBT to 0)
        // E2 fix: correct ACKWP sequence
        i2c_inst->ICMR3_b.ACKWP = 0;
        i2c_inst->ICMR3_b.ACKBT = 0;
        i2c_inst->ICMR3_b.ACKWP = 1;
        ra_i2c_slave_invoke_callback(self, RA_I2C_SLAVE_EVENT_ERROR);
    }

    // Check for arbitration loss (shouldn't happen in slave mode)
    if (i2c_inst->ICSR2_b.AL != 0) {
        i2c_inst->ICSR2_b.AL = 0;
        self->addr_match_active = false;
        // E1 fix: disable SPIE when idle.
        ra_i2c_slave_set_icier_mask(self, self->icier_mask);
        // Increment recovery counter
        self->recovery_count++;
        #if RA_I2C_SLAVE_DEBUG
        printf("[ra_i2c_slave] ch%lu: ARBITRATION_LOST (recovery_count=%u)\n",
               (unsigned long) ch, (unsigned int) self->recovery_count);
        #endif
        // Check if auto-reset threshold reached
        if (self->recovery_count >= RA_I2C_SLAVE_RECOVERY_THRESHOLD) {
            ra_i2c_slave_auto_reset(self);
            return; // Auto-reset already clears state and resets counter
        }
        // Force ACK for next transaction (reset ACKBT to 0)
        // E2 fix: correct ACKWP sequence
        i2c_inst->ICMR3_b.ACKWP = 0;
        i2c_inst->ICMR3_b.ACKBT = 0;
        i2c_inst->ICMR3_b.ACKWP = 1;
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

