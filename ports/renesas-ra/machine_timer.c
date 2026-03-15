#include "py/runtime.h"
#include "py/mphal.h"
#include "shared/runtime/softtimer.h"
#include "extmod/modmachine.h"
#include "timer.h"

#if MICROPY_PY_MACHINE_TIMER

typedef soft_timer_entry_t ra_machine_soft_timer_obj_t;

const mp_obj_type_t ra_machine_soft_timer_type;
const mp_obj_type_t machine_timer_type;

static void ra_machine_soft_timer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    ra_machine_soft_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qstr mode = self->mode == SOFT_TIMER_MODE_ONE_SHOT ? MP_QSTR_ONE_SHOT : MP_QSTR_PERIODIC;
    mp_printf(print, "Timer(mode=%q, period=%u)", mode, self->delta_ms);
}

static mp_obj_t ra_machine_soft_timer_init_helper(ra_machine_soft_timer_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_callback, ARG_period, ARG_tick_hz, ARG_freq, ARG_hard };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = SOFT_TIMER_MODE_PERIODIC} },
        { MP_QSTR_callback, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_period, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0xffffffff} },
        { MP_QSTR_tick_hz, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000} },
        { MP_QSTR_freq, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_hard, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    self->mode = args[ARG_mode].u_int;

    uint64_t delta_ms = self->delta_ms;
    if (args[ARG_freq].u_obj != mp_const_none) {
        #if MICROPY_PY_BUILTINS_FLOAT
        delta_ms = (uint32_t)(MICROPY_FLOAT_CONST(1000.0) / mp_obj_get_float(args[ARG_freq].u_obj));
        #else
        delta_ms = 1000 / mp_obj_get_int(args[ARG_freq].u_obj);
        #endif
    } else if (args[ARG_period].u_int != 0xffffffff) {
        delta_ms = (uint64_t)args[ARG_period].u_int * 1000 / args[ARG_tick_hz].u_int;
    }

    if (delta_ms < 1) {
        delta_ms = 1;
    } else if (delta_ms >= 0x40000000) {
        mp_raise_ValueError(MP_ERROR_TEXT("period too large"));
    }
    self->delta_ms = (uint32_t)delta_ms;

    if (args[ARG_callback].u_obj != MP_OBJ_NULL) {
        self->py_callback = args[ARG_callback].u_obj;
    }

    if (args[ARG_hard].u_bool) {
        self->flags |= SOFT_TIMER_FLAG_HARD_CALLBACK;
    } else {
        self->flags &= ~SOFT_TIMER_FLAG_HARD_CALLBACK;
    }

    if (self->py_callback != mp_const_none) {
        soft_timer_insert(self, self->delta_ms);
    }

    return mp_const_none;
}

static mp_obj_t ra_machine_soft_timer_make_new(size_t n_args, size_t n_kw, const mp_obj_t *args) {
    ra_machine_soft_timer_obj_t *self = m_new_obj(ra_machine_soft_timer_obj_t);
    self->pairheap.base.type = &ra_machine_soft_timer_type;
    self->flags = SOFT_TIMER_FLAG_PY_CALLBACK | SOFT_TIMER_FLAG_GC_ALLOCATED;
    self->delta_ms = 1000;
    self->py_callback = mp_const_none;

    if (n_args > 0 || n_kw > 0) {
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        ra_machine_soft_timer_init_helper(self, n_args, args, &kw_args);
    }

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t ra_machine_soft_timer_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    ra_machine_soft_timer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    soft_timer_remove(self);
    return ra_machine_soft_timer_init_helper(self, n_args - 1, args + 1, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(ra_machine_soft_timer_init_obj, 1, ra_machine_soft_timer_init);

static mp_obj_t ra_machine_soft_timer_deinit(mp_obj_t self_in) {
    ra_machine_soft_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    soft_timer_remove(self);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ra_machine_soft_timer_deinit_obj, ra_machine_soft_timer_deinit);

static const mp_rom_map_elem_t ra_machine_soft_timer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&ra_machine_soft_timer_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&ra_machine_soft_timer_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(ra_machine_soft_timer_locals_dict, ra_machine_soft_timer_locals_dict_table);

static mp_obj_t machine_timer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;

    mp_int_t id = -1;
    size_t timer_n_args = n_args;
    const mp_obj_t *timer_args = args;
    if (n_args > 0) {
        id = mp_obj_get_int(args[0]);
        timer_n_args -= 1;
        timer_args += 1;
    }

    #if defined(MICROPY_HW_MACHINE_TIMER_HARDWARE) && MICROPY_HW_MACHINE_TIMER_HARDWARE
    if (id >= 1 && id <= MICROPY_HW_MAX_TIMER) {
        return MP_OBJ_TYPE_GET_SLOT(&pyb_timer_type, make_new)(&pyb_timer_type, n_args, n_kw, args);
    }
    #endif

    if (id == -1) {
        return ra_machine_soft_timer_make_new(timer_n_args, n_kw, timer_args);
    }

    mp_raise_ValueError(MP_ERROR_TEXT("Timer doesn't exist"));
}

static const mp_rom_map_elem_t machine_timer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_ONE_SHOT), MP_ROM_INT(SOFT_TIMER_MODE_ONE_SHOT) },
    { MP_ROM_QSTR(MP_QSTR_PERIODIC), MP_ROM_INT(SOFT_TIMER_MODE_PERIODIC) },
    { MP_ROM_QSTR(MP_QSTR_OC), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_IC), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_IC_PERIOD), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_IC_PULSE_WIDTH_LOW), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_IC_PULSE_WIDTH_HIGH), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_IC_EVENT_COUNT), MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_RISING), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_FALLING), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_BOTH), MP_ROM_INT(8) },
};
static MP_DEFINE_CONST_DICT(machine_timer_locals_dict, machine_timer_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    ra_machine_soft_timer_type,
    MP_QSTR_Timer,
    MP_TYPE_FLAG_NONE,
    make_new, NULL,
    print, ra_machine_soft_timer_print,
    locals_dict, &ra_machine_soft_timer_locals_dict
    );

MP_DEFINE_CONST_OBJ_TYPE(
    machine_timer_type,
    MP_QSTR_Timer,
    MP_TYPE_FLAG_NONE,
    make_new, machine_timer_make_new,
    locals_dict, &machine_timer_locals_dict
    );

#endif // MICROPY_PY_MACHINE_TIMER
