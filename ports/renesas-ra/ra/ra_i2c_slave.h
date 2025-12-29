/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Renesas Electronics Corporation
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

#ifndef RA_RA_I2C_SLAVE_H_
#define RA_RA_I2C_SLAVE_H_

#include <stdint.h>
#include <stdbool.h>

// Slave state machine states
typedef enum {
    RA_I2C_SLAVE_STATE_IDLE = 0,
    RA_I2C_SLAVE_STATE_ADDR_MATCH,
    RA_I2C_SLAVE_STATE_TX,       // Slave transmitting (master reading)
    RA_I2C_SLAVE_STATE_RX,       // Slave receiving (master writing)
    RA_I2C_SLAVE_STATE_STOP,
} ra_i2c_slave_state_t;

// Slave event flags (for IRQ callbacks)
typedef enum {
    RA_I2C_SLAVE_EVENT_NONE        = 0x00,
    RA_I2C_SLAVE_EVENT_ADDR_MATCH  = 0x01,
    RA_I2C_SLAVE_EVENT_RX_READY    = 0x02,  // Byte received, ready to read
    RA_I2C_SLAVE_EVENT_TX_READY    = 0x04,  // TX buffer empty, ready to write
    RA_I2C_SLAVE_EVENT_STOP        = 0x08,  // STOP condition detected
    RA_I2C_SLAVE_EVENT_ERROR       = 0x10,  // Error occurred
    RA_I2C_SLAVE_EVENT_NACK        = 0x20,  // NACK received from master
} ra_i2c_slave_event_t;

// Forward declaration for callback
struct ra_i2c_slave_obj;

// Callback function type for slave events
typedef void (*ra_i2c_slave_callback_t)(struct ra_i2c_slave_obj *self, ra_i2c_slave_event_t event);

// Slave object structure
typedef struct ra_i2c_slave_obj {
    R_IIC0_Type *i2c_inst;           // IIC peripheral instance
    uint32_t scl_pin;                // SCL pin
    uint32_t sda_pin;                // SDA pin
    uint16_t addr;                   // Slave address (7-bit or 10-bit)
    bool addr_10bit;                 // True if 10-bit address mode
    volatile ra_i2c_slave_state_t state;
    volatile ra_i2c_slave_event_t pending_events;
    ra_i2c_slave_callback_t callback; // Event callback
    void *callback_arg;              // User argument for callback
    // RX/TX state
    volatile bool tx_in_progress;
    volatile bool rx_in_progress;
} ra_i2c_slave_obj_t;

// Maximum number of I2C slave instances
#define RA_I2C_SLAVE_MAX_INSTANCES  2

// Get slave object by channel
ra_i2c_slave_obj_t *ra_i2c_slave_get_obj(uint32_t ch);

// Initialize I2C slave mode
void ra_i2c_slave_init(ra_i2c_slave_obj_t *self, R_IIC0_Type *i2c_inst,
    uint32_t scl, uint32_t sda, uint16_t addr, bool addr_10bit);

// Deinitialize I2C slave
void ra_i2c_slave_deinit(ra_i2c_slave_obj_t *self);

// Set callback for slave events
void ra_i2c_slave_set_callback(ra_i2c_slave_obj_t *self,
    ra_i2c_slave_callback_t callback, void *arg);

// Read received bytes (call from RX_READY event)
// Returns number of bytes actually read
size_t ra_i2c_slave_read(ra_i2c_slave_obj_t *self, uint8_t *buf, size_t len);

// Write bytes to transmit buffer (call from TX_READY event)
// Returns number of bytes actually written
size_t ra_i2c_slave_write(ra_i2c_slave_obj_t *self, const uint8_t *buf, size_t len);

// Check if RX data is available
bool ra_i2c_slave_rx_available(ra_i2c_slave_obj_t *self);

// Check if TX buffer is ready for more data
bool ra_i2c_slave_tx_ready(ra_i2c_slave_obj_t *self);

// Get current slave state
ra_i2c_slave_state_t ra_i2c_slave_get_state(ra_i2c_slave_obj_t *self);

// Clear pending events
void ra_i2c_slave_clear_events(ra_i2c_slave_obj_t *self, ra_i2c_slave_event_t events);

// Get and clear pending events
ra_i2c_slave_event_t ra_i2c_slave_get_events(ra_i2c_slave_obj_t *self);

// ISR handlers (called from vector table)
void iic_slave_rxi_isr(void);
void iic_slave_txi_isr(void);
void iic_slave_tei_isr(void);
void iic_slave_eri_isr(void);

#endif /* RA_RA_I2C_SLAVE_H_ */

