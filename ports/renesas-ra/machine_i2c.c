/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2018 Damien P. George
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
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "extmod/modmachine.h"
#include "shared/runtime/softtimer.h"

#include "ra_i2c.h"
#if MICROPY_HW_ENABLE_SCI_I2C
#include "ra_sci_i2c.h"
#endif

#if MICROPY_PY_MACHINE_I2C

#define DEFAULT_I2C_FREQ (400000)
#define DEFAULT_I2C_TIMEOUT (1000)

typedef enum {
    MACHINE_I2C_BACKEND_RIIC = 0,
    MACHINE_I2C_BACKEND_SCI,
} machine_i2c_backend_t;

typedef struct _machine_i2c_obj_t {
    mp_obj_base_t base;
    R_IIC0_Type *i2c_inst;
    uint8_t i2c_id;
    uint8_t backend;
    mp_hal_pin_obj_t scl;
    mp_hal_pin_obj_t sda;
    uint32_t freq;
} machine_i2c_obj_t;

typedef struct _machine_i2c_async_obj_t {
    mp_obj_base_t base;
    machine_i2c_obj_t *bus;
    xaction_t action;
    xaction_unit_t unit;
    mp_sched_node_t completion_node;
    soft_timer_entry_t timeout_timer;
    uint32_t start_ms;
    uint32_t timeout_ms;
    size_t transfer_len;
    int result;
    bool active;
    bool timeout_timer_initialized;
    bool timeout_timer_active;
    bool timeout_expired;
    bool cancel_requested;
    volatile bool completion_pending;
    volatile bool suppress_notification;
} machine_i2c_async_obj_t;

extern const mp_obj_type_t machine_i2c_async_type;
MP_REGISTER_ROOT_POINTER(mp_obj_t machine_i2c_async_buffer_roots[3]);
MP_REGISTER_ROOT_POINTER(mp_obj_t machine_i2c_async_callback_roots[3]);
static machine_i2c_async_obj_t machine_i2c_async_obj[3];

static bool machine_i2c_async_update(machine_i2c_async_obj_t *self);
static void machine_i2c_async_complete_node(mp_sched_node_t *node);

static machine_i2c_obj_t machine_i2c_obj[] = {
    #if defined(MICROPY_HW_I2C0_SCL)
    {{&machine_i2c_type}, R_IIC0, 0, MACHINE_I2C_BACKEND_RIIC, MICROPY_HW_I2C0_SCL, MICROPY_HW_I2C0_SDA, 0},
    #endif
    #if defined(MICROPY_HW_I2C1_SCL)
    {{&machine_i2c_type}, R_IIC1, 1, MACHINE_I2C_BACKEND_RIIC, MICROPY_HW_I2C1_SCL, MICROPY_HW_I2C1_SDA, 0},
    #endif
    #if defined(MICROPY_HW_I2C2_SCL)
    {{&machine_i2c_type}, R_IIC2, 2, MACHINE_I2C_BACKEND_RIIC, MICROPY_HW_I2C2_SCL, MICROPY_HW_I2C2_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C0_SCL)
    {{&machine_i2c_type}, NULL, 0, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C0_SCL, MICROPY_HW_SCI_I2C0_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C1_SCL)
    {{&machine_i2c_type}, NULL, 1, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C1_SCL, MICROPY_HW_SCI_I2C1_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C2_SCL)
    {{&machine_i2c_type}, NULL, 2, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C2_SCL, MICROPY_HW_SCI_I2C2_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C3_SCL)
    {{&machine_i2c_type}, NULL, 3, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C3_SCL, MICROPY_HW_SCI_I2C3_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C4_SCL)
    {{&machine_i2c_type}, NULL, 4, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C4_SCL, MICROPY_HW_SCI_I2C4_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C5_SCL)
    {{&machine_i2c_type}, NULL, 5, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C5_SCL, MICROPY_HW_SCI_I2C5_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C6_SCL)
    {{&machine_i2c_type}, NULL, 6, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C6_SCL, MICROPY_HW_SCI_I2C6_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C7_SCL)
    {{&machine_i2c_type}, NULL, 7, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C7_SCL, MICROPY_HW_SCI_I2C7_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C8_SCL)
    {{&machine_i2c_type}, NULL, 8, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C8_SCL, MICROPY_HW_SCI_I2C8_SDA, 0},
    #endif
    #if MICROPY_HW_ENABLE_SCI_I2C && defined(MICROPY_HW_SCI_I2C9_SCL)
    {{&machine_i2c_type}, NULL, 9, MACHINE_I2C_BACKEND_SCI, MICROPY_HW_SCI_I2C9_SCL, MICROPY_HW_SCI_I2C9_SDA, 0},
    #endif
};

static void machine_i2c_async_root_set(machine_i2c_async_obj_t *self, mp_obj_t buffer) {
    if (self->bus != NULL && self->bus->i2c_id < 3) {
        MP_STATE_PORT(machine_i2c_async_buffer_roots)[self->bus->i2c_id] = buffer;
    }
}

static void machine_i2c_async_callback_root_set(machine_i2c_async_obj_t *self, mp_obj_t callback) {
    if (self->bus != NULL && self->bus->i2c_id < 3) {
        MP_STATE_PORT(machine_i2c_async_callback_roots)[self->bus->i2c_id] = callback;
    }
}

static mp_obj_t machine_i2c_async_callback_root_get(machine_i2c_async_obj_t *self) {
    if (self->bus == NULL || self->bus->i2c_id >= 3) {
        return MP_OBJ_NULL;
    }
    return MP_STATE_PORT(machine_i2c_async_callback_roots)[self->bus->i2c_id];
}

static machine_i2c_async_obj_t *machine_i2c_async_from_node(mp_sched_node_t *node) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(machine_i2c_async_obj); ++i) {
        if (&machine_i2c_async_obj[i].completion_node == node) {
            return &machine_i2c_async_obj[i];
        }
    }
    return NULL;
}

static machine_i2c_async_obj_t *machine_i2c_async_from_timer(soft_timer_entry_t *timer) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(machine_i2c_async_obj); ++i) {
        if (&machine_i2c_async_obj[i].timeout_timer == timer) {
            return &machine_i2c_async_obj[i];
        }
    }
    return NULL;
}

static void machine_i2c_async_schedule_completion(machine_i2c_async_obj_t *self) {
    if (self->suppress_notification || self->completion_pending) {
        return;
    }
    mp_obj_t callback = machine_i2c_async_callback_root_get(self);
    if (callback == MP_OBJ_NULL || callback == mp_const_none) {
        return;
    }
    self->completion_pending = true;
    (void)mp_sched_schedule_node(&self->completion_node, machine_i2c_async_complete_node);
}

static void machine_i2c_async_transfer_complete(void *context) {
    machine_i2c_async_schedule_completion((machine_i2c_async_obj_t *)context);
}

static void machine_i2c_async_timeout_callback(soft_timer_entry_t *timer) {
    machine_i2c_async_obj_t *self = machine_i2c_async_from_timer(timer);
    if (self == NULL || !self->active || self->suppress_notification) {
        return;
    }
    self->timeout_timer_active = false;
    self->timeout_expired = true;
    machine_i2c_async_schedule_completion(self);
}

static void machine_i2c_async_timeout_stop(machine_i2c_async_obj_t *self) {
    if (self->timeout_timer_active) {
        soft_timer_remove(&self->timeout_timer);
        self->timeout_timer_active = false;
    }
}

static void machine_i2c_async_abort_and_reinit(machine_i2c_async_obj_t *self) {
    ra_i2c_action_cancel_async(self->bus->i2c_inst, &self->action);
    if (self->bus->freq != 0) {
        ra_i2c_deinit(self->bus->i2c_inst);
        ra_i2c_recover_bus(self->bus->scl->pin, self->bus->sda->pin);
        ra_i2c_init(self->bus->i2c_inst, self->bus->scl->pin,
            self->bus->sda->pin, self->bus->freq);
    }
}

static void machine_i2c_async_shutdown(machine_i2c_async_obj_t *self) {
    self->suppress_notification = true;
    machine_i2c_async_timeout_stop(self);
    if (self->active && self->bus != NULL) {
        ra_i2c_action_cancel_async(self->bus->i2c_inst, &self->action);
    }
    self->active = false;
    self->result = -MP_ECANCELED;
    self->timeout_expired = false;
    self->cancel_requested = false;
    self->completion_pending = false;
    machine_i2c_async_root_set(self, MP_OBJ_NULL);
    machine_i2c_async_callback_root_set(self, MP_OBJ_NULL);
    self->bus = NULL;
}

static int i2c_read(machine_i2c_obj_t *self, uint16_t addr, uint8_t *dest, size_t len, bool stop);
static int i2c_write(machine_i2c_obj_t *self, uint16_t addr, const uint8_t *src, size_t len, bool stop);

static int i2c_read(machine_i2c_obj_t *self, uint16_t addr, uint8_t *dest, size_t len, bool stop) {
    #if MICROPY_HW_ENABLE_SCI_I2C
    if (self->backend == MACHINE_I2C_BACKEND_SCI) {
        int result = ra_sci_i2c_read(self->i2c_id, addr, dest, len, stop, DEFAULT_I2C_TIMEOUT);
        if (result == RA_SCI_I2C_NACK) {
            return -MP_ENODEV;
        }
        if (result == RA_SCI_I2C_TIMEOUT) {
            return -MP_ETIMEDOUT;
        }
        return result < 0 ? -MP_EIO : result;
    }
    #endif
    bool flag;
    xaction_t action;
    xaction_unit_t unit;
    ra_i2c_xunit_init(&unit, (uint8_t *)dest, (uint32_t)len, true, (void *)NULL);
    ra_i2c_xaction_init(&action, (xaction_unit_t *)&unit, 1, (uint32_t)addr, stop);
    flag = ra_i2c_action_execute(self->i2c_inst, &action, false, DEFAULT_I2C_TIMEOUT);
    return flag? len:-1;
}

static int i2c_write(machine_i2c_obj_t *self, uint16_t addr, const uint8_t *src, size_t len, bool stop) {
    #if MICROPY_HW_ENABLE_SCI_I2C
    if (self->backend == MACHINE_I2C_BACKEND_SCI) {
        int result = ra_sci_i2c_write(self->i2c_id, addr, src, len, stop, DEFAULT_I2C_TIMEOUT);
        if (result == RA_SCI_I2C_NACK) {
            return -MP_ENODEV;
        }
        if (result == RA_SCI_I2C_TIMEOUT) {
            return -MP_ETIMEDOUT;
        }
        return result < 0 ? -MP_EIO : result;
    }
    #endif
    bool flag;
    xaction_t action;
    xaction_unit_t unit;
    ra_i2c_xunit_init(&unit, (uint8_t *)src, (uint32_t)len, false, (void *)NULL);
    ra_i2c_xaction_init(&action, (xaction_unit_t *)&unit, 1, (uint32_t)addr, stop);
    flag = ra_i2c_action_execute(self->i2c_inst, &action, false, DEFAULT_I2C_TIMEOUT);
    return flag? len:-1;
}

// MicroPython bindings for machine API

static void machine_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2C(%u, freq=%u, scl=%q, sda=%q)",
        self->i2c_id, self->freq, self->scl->name, self->sda->name);
}

static bool machine_i2c_validate_pins(machine_i2c_obj_t *self, mp_hal_pin_obj_t scl, mp_hal_pin_obj_t sda) {
    #if MICROPY_HW_ENABLE_SCI_I2C
    if (self->backend == MACHINE_I2C_BACKEND_SCI) {
        uint32_t ch;
        return ra_sci_i2c_find_pins(sda->pin, scl->pin, &ch) && ch == self->i2c_id;
    }
    #endif
    uint8_t riic_ch;
    return ra_i2c_find_af_ch(scl->pin, sda->pin, &riic_ch) && riic_ch == self->i2c_id;
}

static void machine_i2c_start_backend(machine_i2c_obj_t *self, uint32_t freq) {
    if (freq == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid frequency"));
    }
    #if MICROPY_HW_ENABLE_SCI_I2C
    if (self->backend == MACHINE_I2C_BACKEND_SCI) {
        if (freq > RA_SCI_I2C_MAX_FREQ) {
            mp_raise_ValueError(MP_ERROR_TEXT("SCI I2C frequency must be <= 400000"));
        }
        if (!ra_sci_i2c_init(self->i2c_id, self->sda->pin, self->scl->pin, freq)) {
            self->freq = 0;
            mp_raise_OSError(MP_EBUSY);
        }
    } else
    #endif
    {
        ra_i2c_init(self->i2c_inst, self->scl->pin, self->sda->pin, freq);
    }
    self->freq = freq;
}

static void machine_i2c_init(mp_obj_base_t *obj, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_i2c_obj_t *self = (machine_i2c_obj_t *)obj;
    enum { ARG_freq, ARG_scl, ARG_sda };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_scl, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_sda, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    bool have_scl = args[ARG_scl].u_obj != MP_OBJ_NULL;
    bool have_sda = args[ARG_sda].u_obj != MP_OBJ_NULL;
    if (have_scl != have_sda) {
        mp_raise_ValueError(MP_ERROR_TEXT("both scl and sda must be specified"));
    }
    if (have_scl) {
        mp_hal_pin_obj_t scl = mp_hal_get_pin_obj(args[ARG_scl].u_obj);
        mp_hal_pin_obj_t sda = mp_hal_get_pin_obj(args[ARG_sda].u_obj);
        if (!machine_i2c_validate_pins(self, scl, sda)) {
            mp_raise_ValueError(MP_ERROR_TEXT("bad SCL/SDA pin"));
        }
        self->scl = scl;
        self->sda = sda;
    }

    mp_int_t freq = args[ARG_freq].u_int;
    if (freq < 0) {
        freq = self->freq == 0 ? DEFAULT_I2C_FREQ : self->freq;
    }
    machine_i2c_start_backend(self, (uint32_t)freq);
}

static mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    // parse args
    enum { ARG_id, ARG_freq, ARG_scl, ARG_sda };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = DEFAULT_I2C_FREQ} },
        { MP_QSTR_scl, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_sda, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // get static peripheral object
    bool found = false;
    int i2c_id = mp_obj_get_int(args[ARG_id].u_obj);
    machine_i2c_obj_t *self = (machine_i2c_obj_t *)&machine_i2c_obj[0];
    for (int i = 0; i < MP_ARRAY_SIZE(machine_i2c_obj); i++) {
        if (i2c_id == self->i2c_id) {
            found = true;
            break;
        }
        ++self;
    }
    if (found != true) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("I2C(%d) doesn't exist"), i2c_id);
    }

    // Optional runtime pin override: scl=/sda= (similar validation logic to I2CTarget).
    // If either is provided, require both.
    bool have_scl = (args[ARG_scl].u_obj != MP_OBJ_NULL);
    bool have_sda = (args[ARG_sda].u_obj != MP_OBJ_NULL);
    if (have_scl || have_sda) {
        if (!(have_scl && have_sda)) {
            mp_raise_ValueError(MP_ERROR_TEXT("both scl and sda must be specified"));
        }

        mp_hal_pin_obj_t scl = mp_hal_get_pin_obj(args[ARG_scl].u_obj);
        mp_hal_pin_obj_t sda = mp_hal_get_pin_obj(args[ARG_sda].u_obj);

        if (!machine_i2c_validate_pins(self, scl, sda)) {
            mp_raise_ValueError(MP_ERROR_TEXT("bad SCL/SDA pin"));
        }

        self->scl = scl;
        self->sda = sda;
    }

    if (n_args > 1 || n_kw > 0 || self->freq == 0) {
        if (args[ARG_freq].u_int <= 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid frequency"));
        }
        machine_i2c_start_backend(self, (uint32_t)args[ARG_freq].u_int);
    }
    return MP_OBJ_FROM_PTR(self);
}

void machine_i2c_deinit_all(void) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(machine_i2c_async_obj); ++i) {
        machine_i2c_async_shutdown(&machine_i2c_async_obj[i]);
    }
    for (size_t i = 0; i < MP_ARRAY_SIZE(machine_i2c_obj); ++i) {
        machine_i2c_obj_t *self = &machine_i2c_obj[i];
        if (self->freq == 0) {
            continue;
        }
        #if MICROPY_HW_ENABLE_SCI_I2C
        if (self->backend == MACHINE_I2C_BACKEND_SCI) {
            ra_sci_i2c_deinit(self->i2c_id);
        } else
        #endif
        {
            ra_i2c_deinit(self->i2c_inst);
        }
        self->freq = 0;
    }
}

static int machine_i2c_transfer_single(mp_obj_base_t *self_in, uint16_t addr, size_t len, uint8_t *buf, unsigned int flags) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int ret;
    bool stop;
    stop = (flags & MP_MACHINE_I2C_FLAG_STOP)? true : false;
    if (flags & MP_MACHINE_I2C_FLAG_READ) {
        ret = i2c_read(self, addr, buf, len, stop);
    } else {
        ret = i2c_write(self, addr, buf, len, stop);
    }
    return ret;
}

static const mp_machine_i2c_p_t machine_i2c_p = {
    .init = machine_i2c_init,
    .transfer = mp_machine_i2c_transfer_adaptor,
    .transfer_single = machine_i2c_transfer_single,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2c_make_new,
    locals_dict, &mp_machine_i2c_locals_dict,
    print, machine_i2c_print,
    protocol, &machine_i2c_p
    );

static int machine_i2c_async_error_to_errno(xaction_error_t error) {
    if (error == RA_I2C_ERROR_TMOF) {
        return MP_ETIMEDOUT;
    }
    if (error == RA_I2C_ERROR_NACK) {
        return MP_ENODEV;
    }
    if (error == RA_I2C_ERROR_BUSY) {
        return MP_EBUSY;
    }
    return MP_EIO;
}

static bool machine_i2c_async_update(machine_i2c_async_obj_t *self) {
    if (!self->active) {
        return true;
    }

    ra_i2c_async_status_t status = ra_i2c_action_poll_async(&self->action);
    if (status == RA_I2C_ASYNC_PENDING) {
        uint32_t elapsed = (uint32_t)mp_hal_ticks_ms() - self->start_ms;
        if (!self->cancel_requested && !self->timeout_expired && elapsed <= self->timeout_ms) {
            return false;
        }

        machine_i2c_async_abort_and_reinit(self);
        self->result = self->cancel_requested ? -MP_ECANCELED : -MP_ETIMEDOUT;
    } else if (status == RA_I2C_ASYNC_COMPLETE) {
        self->result = (int)self->transfer_len;
    } else if (self->cancel_requested) {
        self->result = -MP_ECANCELED;
    } else if (self->timeout_expired) {
        self->result = -MP_ETIMEDOUT;
    } else {
        self->result = -machine_i2c_async_error_to_errno(self->action.m_error);
    }

    self->active = false;
    machine_i2c_async_timeout_stop(self);
    machine_i2c_async_root_set(self, MP_OBJ_NULL);
    machine_i2c_async_schedule_completion(self);
    self->timeout_expired = false;
    self->cancel_requested = false;
    return true;
}

static void machine_i2c_async_complete_node(mp_sched_node_t *node) {
    machine_i2c_async_obj_t *self = machine_i2c_async_from_node(node);
    if (self == NULL || self->suppress_notification) {
        return;
    }
    if (self->active && !machine_i2c_async_update(self)) {
        self->completion_pending = false;
        return;
    }

    self->completion_pending = false;
    mp_obj_t callback = machine_i2c_async_callback_root_get(self);
    if (callback != MP_OBJ_NULL && callback != mp_const_none) {
        mp_call_function_1_protected(callback, MP_OBJ_FROM_PTR(self));
    }
}

static mp_obj_t machine_i2c_async_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    if (!mp_obj_is_type(args[0], &machine_i2c_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("I2C object required"));
    }

    machine_i2c_obj_t *bus = MP_OBJ_TO_PTR(args[0]);
    if (bus->backend != MACHINE_I2C_BACKEND_RIIC || bus->i2c_inst == NULL || bus->i2c_id >= 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("RIIC bus required"));
    }

    machine_i2c_async_obj_t *self = &machine_i2c_async_obj[bus->i2c_id];
    bool reset_static_state = self->bus == NULL;
    if (reset_static_state) {
        self->completion_node.callback = NULL;
        self->completion_node.next = NULL;
        self->completion_pending = false;
        self->timeout_timer_active = false;
        self->timeout_expired = false;
        self->cancel_requested = false;
        self->suppress_notification = false;
    }
    if (self->completion_pending) {
        mp_raise_OSError(MP_EBUSY);
    }
    if (self->active) {
        if (!machine_i2c_async_update(self)) {
            mp_raise_OSError(MP_EBUSY);
        }
        if (self->completion_pending) {
            mp_raise_OSError(MP_EBUSY);
        }
    }
    if (!self->timeout_timer_initialized) {
        self->completion_node.callback = NULL;
        self->completion_node.next = NULL;
        soft_timer_static_init(&self->timeout_timer, SOFT_TIMER_MODE_ONE_SHOT, 0,
            machine_i2c_async_timeout_callback);
        self->timeout_timer_initialized = true;
    }
    self->base.type = type;
    self->bus = bus;
    self->result = 0;
    self->suppress_notification = false;
    if (reset_static_state) {
        machine_i2c_async_callback_root_set(self, MP_OBJ_NULL);
    }
    return MP_OBJ_FROM_PTR(self);
}

static void machine_i2c_async_print(const mp_print_t *print, mp_obj_t self_in,
    mp_print_kind_t kind) {
    (void)kind;
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2CAsync(%u, active=%d)",
        self->bus == NULL ? 0 : self->bus->i2c_id, self->active);
}

static mp_obj_t machine_i2c_async_start_transfer(machine_i2c_async_obj_t *self,
    mp_int_t address, mp_obj_t buffer_obj, bool stop, mp_int_t timeout_ms, bool read) {
    if (self->bus == NULL || self->bus->freq == 0) {
        mp_raise_OSError(MP_ENODEV);
    }
    if (self->completion_pending) {
        mp_raise_OSError(MP_EBUSY);
    }
    if (!machine_i2c_async_update(self)) {
        mp_raise_OSError(MP_EBUSY);
    }
    if (self->completion_pending) {
        mp_raise_OSError(MP_EBUSY);
    }

    if (address < 0 || address > 0x7f) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid address"));
    }
    mp_buffer_info_t buffer;
    mp_get_buffer_raise(buffer_obj, &buffer, read ? MP_BUFFER_WRITE : MP_BUFFER_READ);
    if (buffer.len == 0) {
        self->result = 0;
        machine_i2c_async_schedule_completion(self);
        return mp_const_none;
    }

    if (timeout_ms <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("timeout must be positive"));
    }

    ra_i2c_xunit_init(&self->unit, buffer.buf, buffer.len, read, NULL);
    ra_i2c_xaction_init(&self->action, &self->unit, 1, (uint32_t)address, stop);
    self->start_ms = (uint32_t)mp_hal_ticks_ms();
    self->timeout_ms = (uint32_t)timeout_ms;
    self->transfer_len = buffer.len;
    self->result = 0;
    self->timeout_expired = false;
    self->cancel_requested = false;
    self->active = true;
    machine_i2c_async_root_set(self, buffer_obj);
    ra_i2c_xaction_set_callback(&self->action, machine_i2c_async_transfer_complete, self);
    if (!ra_i2c_action_start_async(self->bus->i2c_inst, &self->action, false)) {
        self->active = false;
        machine_i2c_async_root_set(self, MP_OBJ_NULL);
        self->result = -MP_EBUSY;
        mp_raise_OSError(MP_EBUSY);
    }
    self->timeout_timer_active = true;
    soft_timer_insert(&self->timeout_timer, self->timeout_ms);
    return mp_const_none;
}

static mp_obj_t machine_i2c_async_readinto(size_t n_args, const mp_obj_t *pos_args,
    mp_map_t *kw_args) {
    enum { ARG_address, ARG_buffer, ARG_stop, ARG_timeout_ms };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_address, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_buffer, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_stop, MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_timeout_ms, MP_ARG_INT, {.u_int = DEFAULT_I2C_TIMEOUT} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    return machine_i2c_async_start_transfer(
        MP_OBJ_TO_PTR(pos_args[0]),
        args[ARG_address].u_int,
        args[ARG_buffer].u_obj,
        args[ARG_stop].u_bool,
        args[ARG_timeout_ms].u_int,
        true);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_i2c_async_readinto_obj, 3,
    machine_i2c_async_readinto);

static mp_obj_t machine_i2c_async_writefrom(size_t n_args, const mp_obj_t *pos_args,
    mp_map_t *kw_args) {
    enum { ARG_address, ARG_buffer, ARG_stop, ARG_timeout_ms };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_address, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_buffer, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_stop, MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_timeout_ms, MP_ARG_INT, {.u_int = DEFAULT_I2C_TIMEOUT} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    return machine_i2c_async_start_transfer(
        MP_OBJ_TO_PTR(pos_args[0]),
        args[ARG_address].u_int,
        args[ARG_buffer].u_obj,
        args[ARG_stop].u_bool,
        args[ARG_timeout_ms].u_int,
        false);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_i2c_async_writefrom_obj, 3,
    machine_i2c_async_writefrom);

static mp_obj_t machine_i2c_async_done(mp_obj_t self_in) {
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(machine_i2c_async_update(self));
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_async_done_obj, machine_i2c_async_done);

static mp_obj_t machine_i2c_async_result(mp_obj_t self_in) {
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!machine_i2c_async_update(self)) {
        mp_raise_OSError(MP_EBUSY);
    }
    if (self->result < 0) {
        mp_raise_OSError(-self->result);
    }
    return mp_obj_new_int(self->result);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_async_result_obj, machine_i2c_async_result);

static mp_obj_t machine_i2c_async_result_code(mp_obj_t self_in) {
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!machine_i2c_async_update(self)) {
        return MP_OBJ_NEW_SMALL_INT(-MP_EBUSY);
    }
    return MP_OBJ_NEW_SMALL_INT(self->result);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_async_result_code_obj,
    machine_i2c_async_result_code);

static mp_obj_t machine_i2c_async_wait(mp_obj_t self_in) {
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(self_in);
    while (!machine_i2c_async_update(self)) {
        mp_handle_pending(true);
    }
    return machine_i2c_async_result(self_in);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_async_wait_obj, machine_i2c_async_wait);

static mp_obj_t machine_i2c_async_cancel_obj_fun(mp_obj_t self_in) {
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->active && !machine_i2c_async_update(self)) {
        self->cancel_requested = true;
        machine_i2c_async_abort_and_reinit(self);
        (void)machine_i2c_async_update(self);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_async_cancel_obj,
    machine_i2c_async_cancel_obj_fun);

static mp_obj_t machine_i2c_async_active(mp_obj_t self_in) {
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->active) {
        machine_i2c_async_update(self);
    }
    return mp_obj_new_bool(self->active);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_async_active_obj, machine_i2c_async_active);

static mp_obj_t machine_i2c_async_irq(size_t n_args, const mp_obj_t *args) {
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args == 1) {
        mp_obj_t callback = machine_i2c_async_callback_root_get(self);
        return callback == MP_OBJ_NULL ? mp_const_none : callback;
    }

    mp_obj_t callback = args[1];
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("handler must be callable"));
    }
    machine_i2c_async_callback_root_set(self,
        callback == mp_const_none ? MP_OBJ_NULL : callback);
    return MP_OBJ_FROM_PTR(self);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_i2c_async_irq_obj, 1, 2,
    machine_i2c_async_irq);

static mp_obj_t machine_i2c_async_stats(mp_obj_t self_in) {
    machine_i2c_async_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t values[8] = {
        mp_obj_new_int_from_uint(self->action.m_rxi_irq_count),
        mp_obj_new_int_from_uint(self->action.m_dtc_transfer_count),
        mp_obj_new_int_from_uint(self->action.m_dtc_bytes),
        mp_obj_new_int_from_uint(self->action.m_dtc_fallback_count),
        mp_obj_new_int_from_uint(self->action.m_txi_irq_count),
        mp_obj_new_int_from_uint(self->action.m_dtc_tx_transfer_count),
        mp_obj_new_int_from_uint(self->action.m_dtc_tx_bytes),
        mp_obj_new_int_from_uint(self->action.m_dtc_tx_fallback_count),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(values), values);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_async_stats_obj,
    machine_i2c_async_stats);

static const mp_rom_map_elem_t machine_i2c_async_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&machine_i2c_async_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_writefrom), MP_ROM_PTR(&machine_i2c_async_writefrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_done), MP_ROM_PTR(&machine_i2c_async_done_obj) },
    { MP_ROM_QSTR(MP_QSTR_result), MP_ROM_PTR(&machine_i2c_async_result_obj) },
    { MP_ROM_QSTR(MP_QSTR_result_code), MP_ROM_PTR(&machine_i2c_async_result_code_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&machine_i2c_async_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_cancel), MP_ROM_PTR(&machine_i2c_async_cancel_obj) },
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&machine_i2c_async_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&machine_i2c_async_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_stats), MP_ROM_PTR(&machine_i2c_async_stats_obj) },
};
static MP_DEFINE_CONST_DICT(machine_i2c_async_locals_dict,
    machine_i2c_async_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_async_type,
    MP_QSTR_I2CAsync,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2c_async_make_new,
    print, machine_i2c_async_print,
    locals_dict, &machine_i2c_async_locals_dict
    );

#endif // MICROPY_PY_MACHINE_I2C
