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

#endif // MICROPY_PY_MACHINE_I2C
