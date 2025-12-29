/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 MicroPython contributors
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

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "ra/ra_opamp.h"

#if MICROPY_HW_ENABLE_OPAMP

typedef struct _machine_opamp_obj_t {
    mp_obj_base_t base;
    uint8_t channel;
    bool active;
} machine_opamp_obj_t;

// Class constants
#define OPAMP_MODE_LOW_POWER    0
#define OPAMP_MODE_HIGH_SPEED   1

static machine_opamp_obj_t opamp_obj[RA_OPAMP_MAX_CH];
static bool module_initialized = false;

static void machine_opamp_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_opamp_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "OPAMP(%u, active=%s)", self->channel, self->active ? "True" : "False");
}

static mp_obj_t machine_opamp_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 2, true);

    // Get channel number
    int channel = mp_obj_get_int(args[0]);
    if (channel < 0 || channel >= RA_OPAMP_MAX_CH) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid channel"));
    }

    // Parse keyword arguments
    enum { ARG_mode };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = OPAMP_MODE_LOW_POWER} },
    };
    mp_arg_val_t kw_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args - 1, n_kw, args + 1, MP_ARRAY_SIZE(allowed_args), allowed_args, kw_args);

    int mode = kw_args[ARG_mode].u_int;
    if (mode != OPAMP_MODE_LOW_POWER && mode != OPAMP_MODE_HIGH_SPEED) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid mode"));
    }

    // Initialize module if not already done
    if (!module_initialized) {
        if (!ra_opamp_init((ra_opamp_mode_t)mode)) {
            mp_raise_OSError(MP_EIO);
        }
        module_initialized = true;
    }

    // Get or create object
    machine_opamp_obj_t *self = &opamp_obj[channel];
    self->base.type = type;
    self->channel = channel;
    self->active = false;

    return MP_OBJ_FROM_PTR(self);
}

// Start the OPAMP channel
static mp_obj_t machine_opamp_start(mp_obj_t self_in) {
    machine_opamp_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (!ra_opamp_start(1 << self->channel)) {
        mp_raise_OSError(MP_EIO);
    }
    self->active = true;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_opamp_start_obj, machine_opamp_start);

// Stop the OPAMP channel
static mp_obj_t machine_opamp_stop(mp_obj_t self_in) {
    machine_opamp_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (!ra_opamp_stop(1 << self->channel)) {
        mp_raise_OSError(MP_EIO);
    }
    self->active = false;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_opamp_stop_obj, machine_opamp_stop);

// Get status of the OPAMP channel
static mp_obj_t machine_opamp_status(mp_obj_t self_in) {
    machine_opamp_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t status = ra_opamp_status();
    return mp_obj_new_bool((status >> self->channel) & 1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_opamp_status_obj, machine_opamp_status);

// Deinitialize the OPAMP
static mp_obj_t machine_opamp_deinit(mp_obj_t self_in) {
    machine_opamp_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ra_opamp_stop(1 << self->channel);
    self->active = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_opamp_deinit_obj, machine_opamp_deinit);

static const mp_rom_map_elem_t machine_opamp_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&machine_opamp_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&machine_opamp_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&machine_opamp_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_opamp_deinit_obj) },
    // Constants
    { MP_ROM_QSTR(MP_QSTR_LOW_POWER), MP_ROM_INT(OPAMP_MODE_LOW_POWER) },
    { MP_ROM_QSTR(MP_QSTR_HIGH_SPEED), MP_ROM_INT(OPAMP_MODE_HIGH_SPEED) },
};
static MP_DEFINE_CONST_DICT(machine_opamp_locals_dict, machine_opamp_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_opamp_type,
    MP_QSTR_OPAMP,
    MP_TYPE_FLAG_NONE,
    make_new, machine_opamp_make_new,
    print, machine_opamp_print,
    locals_dict, &machine_opamp_locals_dict
    );

#endif // MICROPY_HW_ENABLE_OPAMP

