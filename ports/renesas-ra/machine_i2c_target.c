/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 MicroPython I2CTarget implementation for Renesas RA
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

// This file is never compiled standalone, it's included directly from
// extmod/machine_i2c_target.c via MICROPY_PY_MACHINE_I2C_TARGET_INCLUDEFILE.

#include "py/mphal.h"
#include "ra/ra_i2c.h"
#include "ra/ra_i2c_slave.h"

typedef struct _machine_i2c_target_obj_t {
    mp_obj_base_t base;
    ra_i2c_slave_obj_t *slave;      // Pointer to low-level slave object
    uint32_t i2c_id;                // I2C channel (0 or 1)
    mp_hal_pin_obj_t scl;           // SCL pin
    mp_hal_pin_obj_t sda;           // SDA pin
    uint8_t state;
    bool stop_pending;
    bool irq_active;
} machine_i2c_target_obj_t;

// Static peripheral objects - initialized at runtime since pins may not be constant
static machine_i2c_target_obj_t machine_i2c_target_obj[MICROPY_PY_MACHINE_I2C_TARGET_MAX];

/******************************************************************************/
// Callback from low-level driver

static void i2c_target_callback(ra_i2c_slave_obj_t *slave, ra_i2c_slave_event_t event) {
    // Find the corresponding machine object
    uint32_t ch = slave - ra_i2c_slave_get_obj(0);
    if (ch >= MICROPY_PY_MACHINE_I2C_TARGET_MAX) {
        return;
    }

    machine_i2c_target_obj_t *self = &machine_i2c_target_obj[ch];
    machine_i2c_target_data_t *data = &machine_i2c_target_data[ch];

    self->irq_active = true;

    // NOTE: We intentionally do not "reset" the higher-level state on
    // RA_I2C_SLAVE_EVENT_ADDR_MATCH. Combined transactions of the form
    //   WRITE(reg_pointer) + repeated START + READ(...)
    // must preserve the register pointer across the repeated START. True
    // transaction boundaries are detected via STOP/NACK and handled in the
    // generic I2CTarget data helpers.

    if (event & RA_I2C_SLAVE_EVENT_RX_READY) {
        // Master wrote data - we need to read it
        if (self->state != STATE_WRITING) {
            machine_i2c_target_data_addr_match(data, false);
        }
        machine_i2c_target_data_write_request(self, data);
        self->state = STATE_WRITING;
    }

    if (event & RA_I2C_SLAVE_EVENT_TX_READY) {
        // Master is reading - we need to provide data
        if (self->state != STATE_READING) {
            machine_i2c_target_data_addr_match(data, true);
        }
        machine_i2c_target_data_read_request(self, data);
        self->state = STATE_READING;
    }

    if (event & RA_I2C_SLAVE_EVENT_STOP) {
        // STOP condition - end of transaction.
        // If we already handled end-of-read via NACK, avoid double-finalizing.
        if (self->stop_pending) {
            self->stop_pending = false;
        } else {
            machine_i2c_target_data_restart_or_stop(data);
        }
        self->state = STATE_IDLE;
    }

    if (event & RA_I2C_SLAVE_EVENT_NACK) {
        // NACK from master: end-of-read. Treat as a transaction boundary.
        machine_i2c_target_data_restart_or_stop(data);
        self->stop_pending = true;
        self->state = STATE_IDLE;
    }

    self->irq_active = false;
}

/******************************************************************************/
// I2CTarget port implementation

static inline size_t mp_machine_i2c_target_get_index(machine_i2c_target_obj_t *self) {
    return self - &machine_i2c_target_obj[0];
}

static inline void mp_machine_i2c_target_event_callback(machine_i2c_target_irq_obj_t *irq) {
    mp_irq_handler(&irq->base);
}

static size_t mp_machine_i2c_target_read_bytes(machine_i2c_target_obj_t *self, size_t len, uint8_t *buf) {
    if (self->slave == NULL) {
        return 0;
    }
    return ra_i2c_slave_read(self->slave, buf, len);
}

static size_t mp_machine_i2c_target_write_bytes(machine_i2c_target_obj_t *self, size_t len, const uint8_t *buf) {
    if (self->slave == NULL) {
        return 0;
    }
    return ra_i2c_slave_write(self->slave, buf, len);
}

static inline void mp_machine_i2c_target_irq_config(machine_i2c_target_obj_t *self, unsigned int trigger) {
	    if (self == NULL || self->slave == NULL) {
	        return;
	    }

	    // Prefer build-time definitions from extmod when available; fall back
	    // to a sane bit layout if missing.
	    #ifndef MP_MACHINE_I2C_TARGET_IRQ_ADDR_MATCH_READ
	    #define MP_MACHINE_I2C_TARGET_IRQ_ADDR_MATCH_READ   (1u << 0)
	    #define MP_MACHINE_I2C_TARGET_IRQ_ADDR_MATCH_WRITE  (1u << 1)
	    #define MP_MACHINE_I2C_TARGET_IRQ_READ_REQ          (1u << 2)
	    #define MP_MACHINE_I2C_TARGET_IRQ_WRITE_REQ         (1u << 3)
	    #define MP_MACHINE_I2C_TARGET_IRQ_END_READ          (1u << 4)
	    #define MP_MACHINE_I2C_TARGET_IRQ_END_WRITE         (1u << 5)
	    #endif

	    (void)MP_MACHINE_I2C_TARGET_IRQ_ADDR_MATCH_READ;
	    (void)MP_MACHINE_I2C_TARGET_IRQ_ADDR_MATCH_WRITE;
	    (void)MP_MACHINE_I2C_TARGET_IRQ_READ_REQ;
	    (void)MP_MACHINE_I2C_TARGET_IRQ_WRITE_REQ;

	    // Always keep the slave engine functional (RXI/TXI/STOP).
	    uint8_t icier = (uint8_t)(
	        RA_I2C_SLAVE_ICIER_TIE |
	        RA_I2C_SLAVE_ICIER_RIE |
	        RA_I2C_SLAVE_ICIER_SPIE);

	    // If no IRQs are requested by the user, minimise wakeups: leave NACK
	    // and error sources disabled.
	    if (trigger == 0) {
	        ra_i2c_slave_set_icier_mask(self->slave, icier);
	        return;
	    }

	    // END_READ benefits from NACK reception (master NACKs the last byte).
	    if (trigger & MP_MACHINE_I2C_TARGET_IRQ_END_READ) {
	        icier |= RA_I2C_SLAVE_ICIER_NAKIE;
	    }

	    // When any trigger is active keep error sources enabled so that
	    // TIMEOUT/AL events can propagate up.
	    icier |= (RA_I2C_SLAVE_ICIER_ALIE | RA_I2C_SLAVE_ICIER_TMOIE);

	    ra_i2c_slave_set_icier_mask(self->slave, icier);
}

static mp_obj_t mp_machine_i2c_target_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_addr, ARG_addrsize, ARG_mem, ARG_mem_addrsize, ARG_scl, ARG_sda };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_addr, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_addrsize, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 7} },
        { MP_QSTR_mem, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_mem_addrsize, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_scl, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_sda, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    // Parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int i2c_id = args[ARG_id].u_int;

    // Check if the I2C bus is valid
    if (i2c_id < 0 || i2c_id >= MP_ARRAY_SIZE(machine_i2c_target_obj)) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("I2CTarget(%d) doesn't exist"), i2c_id);
    }

    // Get static peripheral object
    machine_i2c_target_obj_t *self = &machine_i2c_target_obj[i2c_id];

    // Set SCL/SDA pins if configured
    if (args[ARG_scl].u_obj != mp_const_none) {
        self->scl = mp_hal_get_pin_obj(args[ARG_scl].u_obj);
    }
    if (args[ARG_sda].u_obj != mp_const_none) {
        self->sda = mp_hal_get_pin_obj(args[ARG_sda].u_obj);
    }

    // Check that pins are set
    if (self->scl == NULL || self->sda == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("SCL and SDA pins required"));
    }

    // Validate SCL/SDA pins for I2C channel
    uint8_t ch;
    if (!ra_i2c_find_af_ch(self->scl->pin, self->sda->pin, &ch) || ch != i2c_id) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad SCL/SDA pin"));
    }

    // Get IIC instance
    R_IIC0_Type *i2c_inst = (i2c_id == 0) ? R_IIC0 : R_IIC1;

    // Get low-level slave object
    self->slave = ra_i2c_slave_get_obj(i2c_id);
    if (self->slave == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("I2C slave init failed"));
    }

    // Initialize object state
    self->base.type = &machine_i2c_target_type;
    self->i2c_id = i2c_id;
    self->state = STATE_IDLE;
    self->stop_pending = false;
    self->irq_active = false;
    MP_STATE_PORT(machine_i2c_target_mem_obj)[i2c_id] = args[ARG_mem].u_obj;
    machine_i2c_target_data_t *data = &machine_i2c_target_data[i2c_id];
    machine_i2c_target_data_init(data, args[ARG_mem].u_obj, args[ARG_mem_addrsize].u_int);

    // Initialize low-level I2C slave hardware
    ra_i2c_slave_init(self->slave, i2c_inst, self->scl->pin, self->sda->pin,
        args[ARG_addr].u_int, args[ARG_addrsize].u_int == 10);

    // Set callback for slave events
    ra_i2c_slave_set_callback(self->slave, i2c_target_callback, self);

    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_i2c_target_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_i2c_target_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->scl && self->sda) {
        mp_printf(print, "I2CTarget(%u, addr=%u, scl=%q, sda=%q)",
            self->i2c_id,
            self->slave ? self->slave->addr : 0,
            mp_hal_pin_name(self->scl),
            mp_hal_pin_name(self->sda));
    } else {
        mp_printf(print, "I2CTarget(%u, addr=%u)",
            self->i2c_id,
            self->slave ? self->slave->addr : 0);
    }
}

static void mp_machine_i2c_target_deinit(machine_i2c_target_obj_t *self) {
    if (self->slave != NULL) {
        ra_i2c_slave_deinit(self->slave);
        self->slave = NULL;
    }
    self->state = STATE_IDLE;
}

