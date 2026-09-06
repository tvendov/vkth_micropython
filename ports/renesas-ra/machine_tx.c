/* machine.IQTX: explicit control of the RA6M3 autonomous baseband engine. */
#include "py/runtime.h"
#include "py/mperrno.h"
#include "ra/ra_tx_hw.h"
#include "ra/ra_tx_core.h"

#if MICROPY_HW_ENABLE_TX
typedef struct {
    mp_obj_base_t base;
    uint8_t *allocation;
    bool active;
    ra_tx_config_t config;
} machine_tx_obj_t;

/* Native .bss is not a GC root. Keep the object AND its original allocation
 * alive through stop, partial initialization and failed checked teardown. */
MP_REGISTER_ROOT_POINTER(mp_obj_t machine_tx_owner);
extern const mp_obj_type_t machine_iqtx_type;

static void machine_tx_raise_error(void) {
    ra_tx_status_t s;
    ra_tx_hw_get_status(&s);
    mp_raise_OSError(s.error == RA_TX_ERROR_BUSY ? MP_EBUSY :
        s.error == RA_TX_ERROR_CONFIG ? MP_EINVAL : MP_EIO);
}

static machine_tx_obj_t *machine_tx_require_active(mp_obj_t self_in) {
    machine_tx_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active || MP_STATE_PORT(machine_tx_owner) != self_in) {
        mp_raise_OSError(MP_ENODEV);
    }
    return self;
}

static mp_obj_t machine_tx_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_mode, ARG_rate, ARG_amplitude, ARG_adc_mid, ARG_i_zero, ARG_q_zero, ARG_fm_gain, ARG_ramp_samples };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = RA_TX_MODE_CW} },
        { MP_QSTR_rate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 44000} },
        { MP_QSTR_amplitude, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 800} },
        { MP_QSTR_adc_mid, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2048} },
        { MP_QSTR_i_zero, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2048} },
        { MP_QSTR_q_zero, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2048} },
        { MP_QSTR_fm_gain, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2} },
        { MP_QSTR_ramp_samples, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 220} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed), allowed, args);
    for (size_t i = 0; i < MP_ARRAY_SIZE(allowed); ++i) {
        if (args[i].u_int < 0 || args[i].u_int > UINT16_MAX) {
            mp_raise_ValueError(MP_ERROR_TEXT("TX parameter out of range"));
        }
    }
    if (args[ARG_mode].u_int > RA_TX_MODE_FM) {
        mp_raise_ValueError(MP_ERROR_TEXT("only CW, AM and FM are implemented; SSB requires DSP"));
    }
    if (args[ARG_fm_gain].u_int != 1 && args[ARG_fm_gain].u_int != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("fm_gain must be 1 or 2"));
    }
    ra_tx_config_t config = {
        .mode = args[ARG_mode].u_int,
        .sample_rate_hz = args[ARG_rate].u_int,
        .amplitude = args[ARG_amplitude].u_int,
        .adc_mid = args[ARG_adc_mid].u_int,
        .i_zero = args[ARG_i_zero].u_int,
        .q_zero = args[ARG_q_zero].u_int,
        .fm_gain = args[ARG_fm_gain].u_int,
        .ramp_samples = args[ARG_ramp_samples].u_int,
    };
    if (!ra_tx_core_validate(&config)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid TX rate, offset, amplitude or ramp"));
    }
    if (ra_tx_hw_owns_resources() || MP_STATE_PORT(machine_tx_owner) != MP_OBJ_NULL) {
        mp_raise_OSError(MP_EBUSY);
    }
    machine_tx_obj_t *self = mp_obj_malloc(machine_tx_obj_t, type);
    self->allocation = NULL;
    self->active = false;
    self->config = config;
    MP_STATE_PORT(machine_tx_owner) = MP_OBJ_FROM_PTR(self);
    uint8_t *bank = NULL;
    if (config.mode != RA_TX_MODE_CW) {
        /* The aligned interior must never be freed/rooted instead of the
         * original block. MemoryError leaves release() available for cleanup. */
        self->allocation = m_new(uint8_t, RA_TX_LUT_ALLOCATION_BYTES);
        bank = (uint8_t *)(((uintptr_t)self->allocation + 0xffffU) & ~(uintptr_t)0xffffU);
    }
    if (!ra_tx_hw_init(&config, bank, bank == NULL ? 0 : RA_TX_LUT_BYTES)) {
        ra_tx_status_t failure;
        ra_tx_hw_get_status(&failure);
        if (ra_tx_hw_deinit_checked()) {
            MP_STATE_PORT(machine_tx_owner) = MP_OBJ_NULL;
        }
        /* Failed teardown intentionally retains the owner and bank. */
        mp_raise_OSError(failure.error == RA_TX_ERROR_BUSY ? MP_EBUSY : MP_EIO);
    }
    self->active = true;
    return MP_OBJ_FROM_PTR(self);
}

static void machine_tx_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_tx_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ra_tx_status_t s;
    ra_tx_hw_get_status(&s);
    mp_printf(print, "IQTX(mode=%u, rate=%u, active=%u, running=%u)",
        (unsigned)self->config.mode, (unsigned)self->config.sample_rate_hz,
        self->active, self->active && s.running);
}

static mp_obj_t machine_tx_start(mp_obj_t self_in) {
    (void)machine_tx_require_active(self_in);
    if (!ra_tx_hw_start()) {
        machine_tx_raise_error();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_start_obj, machine_tx_start);

static mp_obj_t machine_tx_stop(mp_obj_t self_in) {
    (void)machine_tx_require_active(self_in);
    if (!ra_tx_hw_stop()) {
        machine_tx_raise_error();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_stop_obj, machine_tx_stop);

static mp_obj_t machine_tx_key(mp_obj_t self_in, mp_obj_t down_in) {
    machine_tx_obj_t *self = machine_tx_require_active(self_in);
    if (self->config.mode != RA_TX_MODE_CW) {
        mp_raise_ValueError(MP_ERROR_TEXT("key is only valid in CW"));
    }
    if (!ra_tx_hw_key(mp_obj_is_true(down_in))) {
        machine_tx_raise_error();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_tx_key_obj, machine_tx_key);

bool machine_tx_deinit_all(void) {
    if (!ra_tx_hw_deinit_checked()) {
        return false;
    }
    if (MP_STATE_PORT(machine_tx_owner) != MP_OBJ_NULL) {
        machine_tx_obj_t *self = MP_OBJ_TO_PTR(MP_STATE_PORT(machine_tx_owner));
        self->active = false;
        self->allocation = NULL;
    }
    MP_STATE_PORT(machine_tx_owner) = MP_OBJ_NULL;
    return true;
}

static mp_obj_t machine_tx_deinit(mp_obj_t self_in) {
    machine_tx_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (MP_STATE_PORT(machine_tx_owner) == self_in && !machine_tx_deinit_all()) {
        machine_tx_raise_error();
    }
    self->active = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_deinit_obj, machine_tx_deinit);

/* Usable even if construction raised before the user received an object. */
static mp_obj_t machine_tx_release(void) {
    if (!machine_tx_deinit_all()) {
        machine_tx_raise_error();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(machine_tx_release_obj, machine_tx_release);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(machine_tx_release_static_obj, MP_ROM_PTR(&machine_tx_release_obj));

static mp_obj_t machine_tx_status(mp_obj_t self_in) {
    machine_tx_obj_t *self = machine_tx_require_active(self_in);
    ra_tx_status_t s;
    ra_tx_hw_get_status(&s);
    mp_obj_t result = mp_obj_new_dict(0);
    #define STORE_INT(key, val) mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_##key), mp_obj_new_int(val))
    #define STORE_BOOL(key, val) mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_##key), mp_obj_new_bool(val))
    STORE_INT(mode, s.mode);
    STORE_BOOL(owned, s.owned);
    STORE_BOOL(running, s.running);
    STORE_BOOL(keyed, s.keyed);
    STORE_BOOL(quiesced, s.quiesced);
    STORE_BOOL(outputs_enabled, s.outputs_enabled);
    STORE_INT(error, s.error);
    STORE_INT(fsp_error, s.fsp_error);
    STORE_INT(timer, s.timer_channel);
    STORE_INT(requested_rate, s.requested_rate_hz);
    STORE_INT(timer_clock, s.timer_clock_hz);
    STORE_INT(period_counts, s.timer_period);
    STORE_INT(last_adc, s.last_adc);
    STORE_INT(phase, s.phase);
    STORE_INT(i_code, s.i_code);
    STORE_INT(q_code, s.q_code);
    STORE_INT(transfer_count, s.transfer_count);
    STORE_INT(unexpected_callbacks, s.unexpected_irqs);
    STORE_INT(lut_allocation_bytes, self->allocation == NULL ? 0 : RA_TX_LUT_ALLOCATION_BYTES);
    STORE_INT(adc_mid, self->config.adc_mid);
    STORE_INT(fm_gain, self->config.fm_gain);
    mp_float_t rate = s.timer_period ? (mp_float_t)s.timer_clock_hz / s.timer_period : 0;
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_actual_rate), mp_obj_new_float(rate));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_cw_pin), mp_const_none);
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_mic_pin), MP_OBJ_NEW_QSTR(MP_QSTR_P001));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_i_pin), MP_OBJ_NEW_QSTR(MP_QSTR_P014));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_q_pin), MP_OBJ_NEW_QSTR(MP_QSTR_P015));
    #undef STORE_INT
    #undef STORE_BOOL
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_tx_status_obj, machine_tx_status);

static const mp_rom_map_elem_t machine_tx_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_CW), MP_ROM_INT(RA_TX_MODE_CW) },
    { MP_ROM_QSTR(MP_QSTR_AM), MP_ROM_INT(RA_TX_MODE_AM) },
    { MP_ROM_QSTR(MP_QSTR_FM), MP_ROM_INT(RA_TX_MODE_FM) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&machine_tx_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&machine_tx_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&machine_tx_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&machine_tx_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_tx_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_release), MP_ROM_PTR(&machine_tx_release_static_obj) },
};
static MP_DEFINE_CONST_DICT(machine_tx_locals_dict, machine_tx_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_iqtx_type,
    MP_QSTR_IQTX,
    MP_TYPE_FLAG_NONE,
    make_new, machine_tx_make_new,
    print, machine_tx_print,
    locals_dict, &machine_tx_locals_dict
    );
#endif
