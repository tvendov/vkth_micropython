/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 MicroPython contributors
 *
 * Machine Comparator class for RA4M1 ACMPLP
 */

#include "py/runtime.h"
#include "py/mphal.h"

#if MICROPY_PY_MACHINE_COMPARATOR

#include "ra/ra_acmplp.h"

typedef struct _machine_comparator_obj_t {
    mp_obj_base_t base;
    uint8_t channel;
    mp_obj_t callback;
    bool enabled;
} machine_comparator_obj_t;

static machine_comparator_obj_t *comparator_objs[RA_ACMPLP_NUM_CHANNELS] = {NULL, NULL};

// Callback dispatcher
static void comparator_irq_callback(uint8_t channel) {
    if (channel < RA_ACMPLP_NUM_CHANNELS && comparator_objs[channel] != NULL) {
        machine_comparator_obj_t *self = comparator_objs[channel];
        if (self->callback != mp_const_none) {
            mp_sched_schedule(self->callback, MP_OBJ_FROM_PTR(self));
        }
    }
}

// Print
static void machine_comparator_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_comparator_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Comparator(%u, enabled=%s)", self->channel, self->enabled ? "True" : "False");
}

// Constructor: Comparator(channel, *, input=0, ref=0, filter=0, edge=0, speed=0, invert=False, output=False)
static mp_obj_t machine_comparator_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, true);

    int channel = mp_obj_get_int(args[0]);
    if (channel < 0 || channel >= RA_ACMPLP_NUM_CHANNELS) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid channel"));
    }

    // Parse keyword arguments
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_input,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_ref,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_filter,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_edge,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_speed,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_invert,   MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_output,   MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };
    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(0, n_kw, args + 1, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);

    // Create object
    machine_comparator_obj_t *self = mp_obj_malloc(machine_comparator_obj_t, type);
    self->channel = channel;
    self->callback = mp_const_none;
    self->enabled = false;

    // Configure
    ra_acmplp_config_t config = {
        .input = parsed_args[0].u_int,
        .reference = parsed_args[1].u_int,
        .filter = parsed_args[2].u_int,
        .edge = parsed_args[3].u_int,
        .speed = parsed_args[4].u_int,
        .invert = parsed_args[5].u_bool,
        .output_pin = parsed_args[6].u_bool,
        .window_mode = false,
    };

    if (!ra_acmplp_init(channel, &config)) {
        mp_raise_ValueError(MP_ERROR_TEXT("init failed"));
    }

    comparator_objs[channel] = self;
    return MP_OBJ_FROM_PTR(self);
}

// enable()
static mp_obj_t machine_comparator_enable(mp_obj_t self_in) {
    machine_comparator_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ra_acmplp_enable(self->channel);
    self->enabled = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_comparator_enable_obj, machine_comparator_enable);

// disable()
static mp_obj_t machine_comparator_disable(mp_obj_t self_in) {
    machine_comparator_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ra_acmplp_disable(self->channel);
    self->enabled = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_comparator_disable_obj, machine_comparator_disable);

// value() - read comparator output
static mp_obj_t machine_comparator_value(mp_obj_t self_in) {
    machine_comparator_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(ra_acmplp_get_output(self->channel));
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_comparator_value_obj, machine_comparator_value);

// irq(callback) - set interrupt callback
static mp_obj_t machine_comparator_irq(mp_obj_t self_in, mp_obj_t callback) {
    machine_comparator_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->callback = callback;
    if (callback != mp_const_none) {
        ra_acmplp_irq_enable(self->channel, comparator_irq_callback);
    } else {
        ra_acmplp_irq_disable(self->channel);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_comparator_irq_obj, machine_comparator_irq);

// deinit()
static mp_obj_t machine_comparator_deinit(mp_obj_t self_in) {
    machine_comparator_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ra_acmplp_deinit(self->channel);
    comparator_objs[self->channel] = NULL;
    self->enabled = false;
    self->callback = mp_const_none;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_comparator_deinit_obj, machine_comparator_deinit);

// Class methods table
static const mp_rom_map_elem_t machine_comparator_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_enable),  MP_ROM_PTR(&machine_comparator_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable), MP_ROM_PTR(&machine_comparator_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),   MP_ROM_PTR(&machine_comparator_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq),     MP_ROM_PTR(&machine_comparator_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),  MP_ROM_PTR(&machine_comparator_deinit_obj) },

    // Filter constants
    { MP_ROM_QSTR(MP_QSTR_FILTER_OFF),    MP_ROM_INT(RA_ACMPLP_FILTER_OFF) },
    { MP_ROM_QSTR(MP_QSTR_FILTER_PCLK8),  MP_ROM_INT(RA_ACMPLP_FILTER_PCLK8) },
    { MP_ROM_QSTR(MP_QSTR_FILTER_PCLK16), MP_ROM_INT(RA_ACMPLP_FILTER_PCLK16) },
    { MP_ROM_QSTR(MP_QSTR_FILTER_PCLK32), MP_ROM_INT(RA_ACMPLP_FILTER_PCLK32) },

    // Edge constants
    { MP_ROM_QSTR(MP_QSTR_RISING),  MP_ROM_INT(RA_ACMPLP_EDGE_RISING) },
    { MP_ROM_QSTR(MP_QSTR_FALLING), MP_ROM_INT(RA_ACMPLP_EDGE_FALLING) },
    { MP_ROM_QSTR(MP_QSTR_BOTH),    MP_ROM_INT(RA_ACMPLP_EDGE_BOTH) },

    // Speed constants
    { MP_ROM_QSTR(MP_QSTR_SPEED_LOW),  MP_ROM_INT(RA_ACMPLP_SPEED_LOW) },
    { MP_ROM_QSTR(MP_QSTR_SPEED_HIGH), MP_ROM_INT(RA_ACMPLP_SPEED_HIGH) },

    // Input constants
    { MP_ROM_QSTR(MP_QSTR_INPUT0), MP_ROM_INT(RA_ACMPLP_INPUT_CMPIN0) },
    { MP_ROM_QSTR(MP_QSTR_INPUT1), MP_ROM_INT(RA_ACMPLP_INPUT_CMPIN1) },
    { MP_ROM_QSTR(MP_QSTR_INPUT2), MP_ROM_INT(RA_ACMPLP_INPUT_CMPIN2) },
    { MP_ROM_QSTR(MP_QSTR_INPUT3), MP_ROM_INT(RA_ACMPLP_INPUT_CMPIN3) },

    // Reference constants
    { MP_ROM_QSTR(MP_QSTR_REF_EXT0), MP_ROM_INT(RA_ACMPLP_REF_CMPREF0) },
    { MP_ROM_QSTR(MP_QSTR_REF_EXT1), MP_ROM_INT(RA_ACMPLP_REF_CMPREF1) },
    { MP_ROM_QSTR(MP_QSTR_REF_DAC0), MP_ROM_INT(RA_ACMPLP_REF_DAC8_CH0) },
    { MP_ROM_QSTR(MP_QSTR_REF_DAC1), MP_ROM_INT(RA_ACMPLP_REF_DAC8_CH1) },
    { MP_ROM_QSTR(MP_QSTR_REF_IVREF), MP_ROM_INT(RA_ACMPLP_REF_IVREF) },
};
static MP_DEFINE_CONST_DICT(machine_comparator_locals_dict, machine_comparator_locals_dict_table);

// Type definition
MP_DEFINE_CONST_OBJ_TYPE(
    machine_comparator_type,
    MP_QSTR_Comparator,
    MP_TYPE_FLAG_NONE,
    make_new, machine_comparator_make_new,
    print, machine_comparator_print,
    locals_dict, &machine_comparator_locals_dict
);

#endif // MICROPY_PY_MACHINE_COMPARATOR

