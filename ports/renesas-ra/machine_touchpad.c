/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Nick Moore
 * Copyright (c) 2026 Augment Agent
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
#include "modmachine.h"

#if MICROPY_HW_ENABLE_TOUCHPAD
#include "ra/ra_ctsu.h"
#endif

// Renesas RA CTSU (Capacitive Touch Sensing Unit) integration.
//
// This module provides machine.TouchPad for capacitive touch sensing
// using the FSP r_ctsu driver with ra_ctsu HAL abstraction.
//
// Usage:
//   from machine import Pin, TouchPad
//   tp = TouchPad(Pin('P204'))  # TS00 on RA4M2
//   tp.config(500)              # Set threshold
//   value = tp.read()           # Get raw touch count

typedef struct _machine_touchpad_obj_t {
    mp_obj_base_t base;
    mp_hal_pin_obj_t pin;
    uint8_t channel;
    uint16_t threshold;
} machine_touchpad_obj_t;

static mp_obj_t ra_touchpad_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, true);

    #if !MICROPY_HW_ENABLE_TOUCHPAD
    mp_raise_NotImplementedError(MP_ERROR_TEXT("TouchPad not enabled for this board"));
    #else
    machine_touchpad_obj_t *self = m_new_obj(machine_touchpad_obj_t);
    self->base.type = type;
    self->pin = mp_hal_get_pin_obj(args[0]);
    self->threshold = 0;

    // Initialize CTSU HAL if not already done
    if (ra_ctsu_init() != 0) {
        mp_raise_OSError(MP_EIO);
    }

    // Map pin to CTSU TS channel
    int8_t ch = ra_ctsu_pin_to_channel(self->pin->pin);
    if (ch < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pin is not a CTSU touch pin"));
    }
    self->channel = (uint8_t)ch;

    // Configure channel with default threshold
    if (ra_ctsu_channel_config(self->channel, self->threshold) != 0) {
        mp_raise_OSError(MP_EIO);
    }

    return MP_OBJ_FROM_PTR(self);
    #endif
}

static mp_obj_t ra_touchpad_config(size_t n_args, const mp_obj_t *args) {
    machine_touchpad_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    #if !MICROPY_HW_ENABLE_TOUCHPAD
    (void)self;
    mp_raise_NotImplementedError(MP_ERROR_TEXT("TouchPad not enabled for this board"));
    #else
    // Match the ESP32 TouchPad API: config(value) sets the touch threshold.
    if (n_args < 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("value required"));
    }

    mp_int_t value = mp_obj_get_int(args[1]);
    if (value < 0 || value > 0xffff) {
        mp_raise_ValueError(MP_ERROR_TEXT("threshold out of range"));
    }
    self->threshold = (uint16_t)value;

    // Update CTSU channel threshold
    if (ra_ctsu_channel_config(self->channel, self->threshold) != 0) {
        mp_raise_OSError(MP_EIO);
    }

    return mp_const_none;
    #endif
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ra_touchpad_config_obj, 1, 2, ra_touchpad_config);

static mp_obj_t ra_touchpad_read(mp_obj_t self_in) {
    #if !MICROPY_HW_ENABLE_TOUCHPAD
    (void)self_in;
    mp_raise_NotImplementedError(MP_ERROR_TEXT("TouchPad not enabled for this board"));
    #else
    machine_touchpad_obj_t *self = MP_OBJ_TO_PTR(self_in);

    // Read raw CTSU count for this channel
    int32_t count = ra_ctsu_read(self->channel);
    if (count < 0) {
        mp_raise_OSError(MP_EIO);
    }

    return mp_obj_new_int(count);
    #endif
}
static MP_DEFINE_CONST_FUN_OBJ_1(ra_touchpad_read_obj, ra_touchpad_read);

static const mp_rom_map_elem_t ra_touchpad_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_config), MP_ROM_PTR(&ra_touchpad_config_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&ra_touchpad_read_obj) },
};
static MP_DEFINE_CONST_DICT(ra_touchpad_locals_dict, ra_touchpad_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_touchpad_type,
    MP_QSTR_TouchPad,
    MP_TYPE_FLAG_NONE,
    make_new, ra_touchpad_make_new,
    locals_dict, &ra_touchpad_locals_dict
    );
