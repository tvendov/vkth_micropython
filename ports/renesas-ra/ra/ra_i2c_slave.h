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

// IIC ICIER bit definitions (RA4M1 IIC): b7..b0 = TIE,TEIE,RIE,NAKIE,SPIE,STIE,ALIE,TMOIE
#define RA_I2C_SLAVE_ICIER_TIE    (0x80u)
#define RA_I2C_SLAVE_ICIER_TEIE   (0x40u)
#define RA_I2C_SLAVE_ICIER_RIE    (0x20u)
#define RA_I2C_SLAVE_ICIER_NAKIE  (0x10u)
#define RA_I2C_SLAVE_ICIER_SPIE   (0x08u)
#define RA_I2C_SLAVE_ICIER_STIE   (0x04u)
#define RA_I2C_SLAVE_ICIER_ALIE   (0x02u)
#define RA_I2C_SLAVE_ICIER_TMOIE  (0x01u)

// Recovery counter threshold: auto-reset IIC after this many consecutive errors
#ifndef RA_I2C_SLAVE_RECOVERY_THRESHOLD
#define RA_I2C_SLAVE_RECOVERY_THRESHOLD (5)
#endif

// Hardware DTC support: set to 1 to enable real DTC transfers, 0 for software emulation
#ifndef RA_I2C_ENABLE_HW_DTC
#define RA_I2C_ENABLE_HW_DTC (1)
#endif

// Hardware DTC TX support: for sequential reads (memory device pattern)
// Disabled by default on low-RAM MCUs (RA4M1) to save ~100 bytes
#ifndef RA_I2C_ENABLE_HW_DTC_TX
#define RA_I2C_ENABLE_HW_DTC_TX (0)
#endif

#ifndef RA_I2C_ENABLE_DTC
#define RA_I2C_ENABLE_DTC (0)
#endif

// Buffer sizes - reduced to save RAM on RA4M1
#define RA_I2C_DTC_RX_MAX (48u)
#define RA_I2C_DTC_TX_MAX (48u)

// Static RX buffer for DTC-based reception (always allocated for simplicity)
// Used when use_dtc_rx is true; stores incoming bytes until STOP or TX transition.
#ifndef RA_I2C_DTC_RX_BUF_SIZE
#define RA_I2C_DTC_RX_BUF_SIZE  RA_I2C_DTC_RX_MAX
#endif

#if RA_I2C_ENABLE_HW_DTC
#include "r_dtc.h"
#include "r_transfer_api.h"
#endif

#if RA_I2C_ENABLE_DTC
// Single-direction DTC buffer (RX or TX)
typedef struct {
    uint8_t *buf;              // Base address of buffer
    uint16_t capacity;         // Maximum bytes for this transfer
    volatile uint16_t count;   // Bytes actually transferred (capacity - remaining)
    volatile uint16_t remaining; // Remaining bytes (if backend provides it)
    volatile bool active;      // DTC transfer currently active
} ra_i2c_dtc_buf_t;

// Combined DTC state for one I2C slave channel
typedef struct {
    ra_i2c_dtc_buf_t rx;       // RX buffer/state (master -> target)
    ra_i2c_dtc_buf_t tx;       // TX buffer/state (target -> master)

    uint8_t rx_channel;        // DTC channel id for RX
    uint8_t tx_channel;        // DTC channel id for TX
} ra_i2c_dtc_state_t;
#endif

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

    // True once a real hardware address match (AASx/GCA/DID/HOA) has been
    // observed for the current transaction. Cleared on STOP/timeout/error.
    volatile bool addr_match_active;

    // Cached ICIER mask controlling which IIC interrupt sources are enabled
    // for this slave instance. RXI/TXI are always forced on.
    uint8_t icier_mask;

    // Recovery counter: incremented on errors (TIMEOUT, AL), reset on clean STOP.
    // When it reaches RA_I2C_SLAVE_RECOVERY_THRESHOLD, IIC is auto-reset.
    uint8_t recovery_count;

    // DTC-RX software buffering: always available (no hardware DTC yet).
    // When use_dtc_rx is true, RXI handler stores payload bytes here instead
    // of invoking per-byte callback. Data is flushed on STOP or TX transition.
    bool use_dtc_rx;           // True to enable software RX buffering
    uint8_t rx_buf[RA_I2C_DTC_RX_BUF_SIZE];
    volatile uint16_t rx_buf_count;  // Number of valid bytes in rx_buf
    volatile bool rx_first_byte_received; // True after first payload byte (reg pointer)

#if RA_I2C_ENABLE_HW_DTC
    // Hardware DTC RX: after first byte (reg pointer), DTC takes over RX.
    // NVIC RXI is disabled; DTC transfers ICDRR -> rx_buf automatically.
    // NOTE: DTC ctrl/cfg structures are static in ra_i2c_slave.c to save RAM
    bool hw_dtc_rx_active;     // True when hardware DTC is running for RX
    uint16_t hw_dtc_rx_initial_len; // Initial DTC transfer length (for count calc)
#endif

#if RA_I2C_ENABLE_HW_DTC_TX
    // Hardware DTC TX: for sequential reads (memory device pattern).
    // DTC transfers tx_buf -> ICDRT automatically, NVIC TXI disabled during transfer.
    bool hw_dtc_tx_active;     // True when hardware DTC is running for TX
    bool hw_dtc_tx_auto_start; // True if DTC TX should auto-start after first CPU byte
    uint16_t hw_dtc_tx_initial_len; // Initial DTC transfer length (for count calc)
    uint8_t tx_buf[RA_I2C_DTC_TX_MAX]; // TX buffer for DTC
    volatile uint16_t tx_buf_count;    // Number of valid bytes in tx_buf
    volatile uint16_t tx_buf_index;    // Current read index for non-DTC fallback
    volatile uint8_t tx_cpu_byte_count; // Bytes sent by CPU before DTC takes over
    // Statistics for debugging
    uint32_t tx_dtc_transfers;         // Number of DTC TX transfers completed
    uint32_t tx_dtc_bytes_total;       // Total bytes sent via DTC TX
#endif

#if RA_I2C_ENABLE_DTC
    bool use_dtc;              // True if this slave instance uses DTC-based transfers
    ra_i2c_dtc_state_t dtc;    // DTC buffers/state for this channel
#endif
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

// Configure interrupt sources (ICIER bitmask). RXI/TXI remain enabled by design.
void ra_i2c_slave_set_icier_mask(ra_i2c_slave_obj_t *self, uint8_t icier_mask);

// Enable/disable software RX buffering (DTC-RX emulation).
// When enabled, RXI handler stores bytes in internal buffer and flushes on STOP/TX.
void ra_i2c_slave_enable_dtc_rx(ra_i2c_slave_obj_t *self, bool enable);

// Flush buffered RX data: returns pointer to internal buffer and count.
// After calling this, the internal buffer is reset. Called at STOP or TX transition.
size_t ra_i2c_slave_flush_rx(ra_i2c_slave_obj_t *self, uint8_t **buf_out);

#if RA_I2C_ENABLE_HW_DTC
// Hardware DTC RX: start DTC after first byte received
void ra_i2c_slave_hw_dtc_rx_start(ra_i2c_slave_obj_t *self);
// Hardware DTC RX: stop DTC and return byte count
size_t ra_i2c_slave_hw_dtc_rx_stop(ra_i2c_slave_obj_t *self);
#endif

#if RA_I2C_ENABLE_HW_DTC_TX
// Hardware DTC TX: prepare buffer for sequential read (memory device pattern)
void ra_i2c_slave_hw_dtc_tx_prepare(ra_i2c_slave_obj_t *self, const uint8_t *data, size_t len);
// Hardware DTC TX: start DTC transfer from tx_buf to ICDRT
void ra_i2c_slave_hw_dtc_tx_start(ra_i2c_slave_obj_t *self);
// Hardware DTC TX: stop DTC and return byte count
size_t ra_i2c_slave_hw_dtc_tx_stop(ra_i2c_slave_obj_t *self);
#endif

#if RA_I2C_ENABLE_DTC
// DTC initialisation for a given slave instance. mem_size is logical register-file size.
bool ra_i2c_slave_dtc_init(ra_i2c_slave_obj_t *self, size_t mem_size);

// Stop/disable DTC for this slave instance.
void ra_i2c_slave_dtc_deinit(ra_i2c_slave_obj_t *self);

// Prepare RX DTC before a new write transaction from master.
void ra_i2c_slave_dtc_prepare_rx(ra_i2c_slave_obj_t *self, size_t max_len);

// Prepare TX DTC before a read transaction from master.
void ra_i2c_slave_dtc_prepare_tx(ra_i2c_slave_obj_t *self, size_t len);

// Query number of bytes actually transferred after STOP/NACK.
size_t ra_i2c_slave_dtc_get_rx_count(ra_i2c_slave_obj_t *self);
size_t ra_i2c_slave_dtc_get_tx_count(ra_i2c_slave_obj_t *self);
#endif

// ISR handlers (called from vector table)
void iic_slave_rxi_isr(void);
void iic_slave_txi_isr(void);
void iic_slave_tei_isr(void);
void iic_slave_eri_isr(void);

#endif /* RA_RA_I2C_SLAVE_H_ */

