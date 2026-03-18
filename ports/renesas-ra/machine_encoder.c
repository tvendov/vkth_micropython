/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Vekatech Ltd.
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
#include "py/runtime.h"
#include "py/mphal.h"
#include "shared/runtime/mpirq.h"
#include "pin.h"
#include "modmachine.h"
#include "ra/ra_encoder.h"

#if defined(MICROPY_HW_ENCODER_A)

typedef struct _machine_encoder_obj_t {
    mp_obj_base_t base;
    encoder_config_t cfg;
    mp_obj_t callback;           // Python callback for movement notification
    volatile bool irq_scheduled; // true = deferred callback already in scheduler queue
    volatile uint32_t irq_pending; // count of ISR events since last deferred dispatch
} machine_encoder_obj_t;

static machine_encoder_obj_t machine_encoder_obj = {
    {&machine_encoder_type},
    {0},
    mp_const_none,  // callback
    false,          // irq_scheduled
    0,              // irq_pending
};

static void machine_encoder_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const char *mode_str = "X4";
    if (self->cfg.mode == ENCODER_MODE_X1) {
        mode_str = "X1";
    } else if (self->cfg.mode == ENCODER_MODE_X2) {
        mode_str = "X2";
    }
    mp_printf(print, "Encoder(gpt=%u, mode=%s, filter=%u, value=%d, range=[%d,%d], debounce=%u, active=%u)",
        (unsigned)self->cfg.gpt_ch, mode_str, (unsigned)self->cfg.filter,
        (int)self->cfg.value, (int)self->cfg.min_val, (int)self->cfg.max_val,
        (unsigned)self->cfg.debounce, self->cfg.active);
}

static mp_obj_t machine_encoder_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    (void)type;

    enum { ARG_pin_a, ARG_pin_b, ARG_mode, ARG_filter, ARG_value, ARG_min, ARG_max, ARG_debounce };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin_a,    MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_pin_b,    MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_mode,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ENCODER_MODE_X4} },
        { MP_QSTR_filter,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ENCODER_FILTER_PCLKD} },
        { MP_QSTR_value,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_min,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -2147483647} },
        { MP_QSTR_max,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2147483647} },
        { MP_QSTR_debounce, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Get pins (use defaults if not specified)
    const machine_pin_obj_t *pin_a = MICROPY_HW_ENCODER_A;
    const machine_pin_obj_t *pin_b = MICROPY_HW_ENCODER_B;
    if (args[ARG_pin_a].u_obj != MP_OBJ_NULL) {
        pin_a = machine_pin_find(args[ARG_pin_a].u_obj);
    }
    if (args[ARG_pin_b].u_obj != MP_OBJ_NULL) {
        pin_b = machine_pin_find(args[ARG_pin_b].u_obj);
    }

    // Validate mode
    int mode = args[ARG_mode].u_int;
    if (mode != ENCODER_MODE_X1 && mode != ENCODER_MODE_X2 && mode != ENCODER_MODE_X4) {
        mp_raise_ValueError(MP_ERROR_TEXT("mode must be X1(1), X2(2) or X4(4)"));
    }

    // Validate filter
    int filter = args[ARG_filter].u_int;
    if (filter < ENCODER_FILTER_NONE || filter > ENCODER_FILTER_PCLKD64) {
        mp_raise_ValueError(MP_ERROR_TEXT("filter 0-4"));
    }

    // Find GPT channel
    uint32_t ch;
    if (!ra_encoder_find_channel(pin_a->pin, pin_b->pin, &ch)) {
        mp_raise_ValueError(MP_ERROR_TEXT("pins must be GTIOCA/B on same GPT channel"));
    }

    machine_encoder_obj_t *self = &machine_encoder_obj;

    // Deinit if already active
    if (self->cfg.active) {
        ra_encoder_deinit(&self->cfg);
    }

    // Fill config
    self->cfg.pin_a = pin_a->pin;
    self->cfg.pin_b = pin_b->pin;
    self->cfg.gpt_ch = ch;
    self->cfg.mode = (encoder_mode_t)mode;
    self->cfg.filter = (encoder_filter_t)filter;
    self->cfg.init_val = args[ARG_value].u_int;
    self->cfg.min_val = args[ARG_min].u_int;
    self->cfg.max_val = args[ARG_max].u_int;
    self->cfg.debounce = (uint8_t)args[ARG_debounce].u_int;

    // Auto-adjust default range for 16-bit GPT channels (GPT164)
    // If user didn't override defaults, use 16-bit signed range
    if (self->cfg.min_val == -2147483647 && self->cfg.max_val == 2147483647) {
        // Check if this is a 16-bit channel
        #if defined(RA4M2) || defined(RA4M1) || defined(RA4W1) || defined(RA6M5)
        if (ch >= 4) {
            self->cfg.min_val = -32768;
            self->cfg.max_val = 32767;
        }
        #elif defined(RA6M1) || defined(RA6M2) || defined(RA6M3)
        if (ch >= 8) {
            self->cfg.min_val = -32768;
            self->cfg.max_val = 32767;
        }
        #endif
    }

    self->cfg.value = self->cfg.init_val;
    self->cfg.active = false;

    if (!ra_encoder_init(&self->cfg)) {
        mp_raise_ValueError(MP_ERROR_TEXT("encoder init failed"));
    }

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t machine_encoder_value(size_t n_args, const mp_obj_t *args) {
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args == 1) {
        // Get value
        return MP_OBJ_NEW_SMALL_INT(ra_encoder_read(&self->cfg));
    } else {
        // Set value (reset)
        ra_encoder_reset(&self->cfg, mp_obj_get_int(args[1]));
        return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_encoder_value_obj, 1, 2, machine_encoder_value);

static mp_obj_t machine_encoder_deinit(mp_obj_t self_in) {
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->callback = mp_const_none;
    self->irq_scheduled = false;
    self->irq_pending = 0;
    ra_encoder_deinit(&self->cfg);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_encoder_deinit_obj, machine_encoder_deinit);

// Deferred trampoline — runs outside ISR context via mp_sched_schedule.
// Clears the "scheduled" flag, then calls the Python callback with
// the latest hardware position (always up-to-date from GTCNT).
static mp_obj_t encoder_deferred_callback(mp_obj_t self_in) {
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->irq_scheduled = false;  // allow next ISR to schedule again
    self->irq_pending = 0;        // reset coalesced event count
    if (self->callback != mp_const_none) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_call_function_1(self->callback, self_in);
            nlr_pop();
        } else {
            // Uncaught exception — disable to prevent repeated errors
            self->callback = mp_const_none;
            ra_encoder_irq_disable(&self->cfg);
            mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(encoder_deferred_callback_obj, encoder_deferred_callback);

// C-level callback invoked from the GPT Compare Match ISR (hard context).
// The ISR itself (in ra_encoder.c) already updated GTCCRA/B and cleared flags.
// Here we ONLY do: pending++, and schedule ONE deferred callback.
// No Python code ever runs in ISR context.
static void encoder_irq_handler(void *param) {
    machine_encoder_obj_t *self = (machine_encoder_obj_t *)param;
    if (self->callback == mp_const_none) {
        return;
    }
    // Coalesce: increment pending count, schedule only once
    self->irq_pending++;
    if (!self->irq_scheduled) {
        self->irq_scheduled = true;
        if (!mp_sched_schedule(MP_OBJ_FROM_PTR(&encoder_deferred_callback_obj),
                               MP_OBJ_FROM_PTR(self))) {
            // Scheduler queue full — retry on next ISR
            self->irq_scheduled = false;
        }
    }
}

// Vector indices for GPT4 Compare Match A/B (defined in vector_data.h)
#if defined(VECTOR_NUMBER_GPT4_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT4_CAPTURE_COMPARE_B)
#define ENCODER_IRQ_A_VEC  ((int8_t)VECTOR_NUMBER_GPT4_CAPTURE_COMPARE_A)
#define ENCODER_IRQ_B_VEC  ((int8_t)VECTOR_NUMBER_GPT4_CAPTURE_COMPARE_B)
#else
#define ENCODER_IRQ_A_VEC  (-1)
#define ENCODER_IRQ_B_VEC  (-1)
#endif

/// \method irq(handler=None, hard=False)
/// Set a callback for encoder movement notification.
///
/// When the encoder moves by at least 1 step in either direction, `handler`
/// is called with the encoder object as its single argument.
///
///     def on_move(enc):
///         print("pos:", enc.value())
///
///     enc.irq(handler=on_move)
///     enc.irq(handler=None)     # disable
///
/// The callback always runs deferred (via mp_sched_schedule), never in ISR context.
/// Multiple encoder pulses between scheduler runs are coalesced into a single callback.
static mp_obj_t machine_encoder_irq(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_handler };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_handler, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (ENCODER_IRQ_A_VEC < 0 || ENCODER_IRQ_B_VEC < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("encoder IRQ not available on this board"));
    }

    mp_obj_t handler = args[ARG_handler].u_obj;
    if (handler == MP_OBJ_NULL || handler == mp_const_none) {
        // Disable IRQ
        self->callback = mp_const_none;
        self->irq_scheduled = false;
        self->irq_pending = 0;
        ra_encoder_irq_disable(&self->cfg);
    } else if (mp_obj_is_callable(handler)) {
        self->callback = handler;
        self->irq_scheduled = false;
        self->irq_pending = 0;
        ra_encoder_irq_enable(&self->cfg, ENCODER_IRQ_A_VEC, ENCODER_IRQ_B_VEC,
            encoder_irq_handler, (void *)self);
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("handler must be None or callable"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_encoder_irq_obj, 1, machine_encoder_irq);

// status() — return raw GPT register snapshot as dict for diagnostics
static mp_obj_t machine_encoder_status(mp_obj_t self_in) {
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    encoder_status_t st;
    ra_encoder_status(&self->cfg, &st);

    mp_obj_dict_t *d = mp_obj_new_dict(6);
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTCNT),  mp_obj_new_int_from_uint(st.gtcnt));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTST),   mp_obj_new_int_from_uint(st.gtst));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTUPSR), mp_obj_new_int_from_uint(st.gtupsr));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTDNSR), mp_obj_new_int_from_uint(st.gtdnsr));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTCR),   mp_obj_new_int_from_uint(st.gtcr));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTIOR),  mp_obj_new_int_from_uint(st.gtior));
    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_encoder_status_obj, machine_encoder_status);

static const mp_rom_map_elem_t machine_encoder_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&machine_encoder_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_encoder_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&machine_encoder_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&machine_encoder_status_obj) },
    // Constants for mode
    { MP_ROM_QSTR(MP_QSTR_X1), MP_ROM_INT(ENCODER_MODE_X1) },
    { MP_ROM_QSTR(MP_QSTR_X2), MP_ROM_INT(ENCODER_MODE_X2) },
    { MP_ROM_QSTR(MP_QSTR_X4), MP_ROM_INT(ENCODER_MODE_X4) },
    // Constants for filter
    { MP_ROM_QSTR(MP_QSTR_FILTER_NONE), MP_ROM_INT(ENCODER_FILTER_NONE) },
    { MP_ROM_QSTR(MP_QSTR_FILTER_1), MP_ROM_INT(ENCODER_FILTER_PCLKD) },
    { MP_ROM_QSTR(MP_QSTR_FILTER_4), MP_ROM_INT(ENCODER_FILTER_PCLKD4) },
    { MP_ROM_QSTR(MP_QSTR_FILTER_16), MP_ROM_INT(ENCODER_FILTER_PCLKD16) },
    { MP_ROM_QSTR(MP_QSTR_FILTER_64), MP_ROM_INT(ENCODER_FILTER_PCLKD64) },
};

static MP_DEFINE_CONST_DICT(machine_encoder_locals_dict, machine_encoder_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_encoder_type,
    MP_QSTR_Encoder,
    MP_TYPE_FLAG_NONE,
    make_new, machine_encoder_make_new,
    print, machine_encoder_print,
    locals_dict, &machine_encoder_locals_dict
    );

#endif

