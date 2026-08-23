/*
 * AudioADC — DTC-driven ADC circular buffer, Python wrapper.
 * Wraps ra_storm_adc (AGT + ELC + ADC12 + DTC double-buffer).
 * API: AudioADC(pin, fs, frame) .start() .stop() .ready() .read_f32(buf) .deinit()
 */

#include <string.h>

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/binary.h"
#include "pin.h"
#include "ra/ra_storm_adc.h"

/* ------------------------------------------------------------------ */
/* Object                                                               */
/* ------------------------------------------------------------------ */

typedef struct _machine_audioadc_obj_t {
    mp_obj_base_t base;
    bool          active;
    uint32_t      pin;
    uint32_t      fs;
    uint16_t      frame;
} machine_audioadc_obj_t;

static machine_audioadc_obj_t machine_audioadc_obj;

extern const mp_obj_type_t machine_audioadc_type;

/* ------------------------------------------------------------------ */
/* print                                                                */
/* ------------------------------------------------------------------ */

static void machine_audioadc_print(const mp_print_t *print,
                                   mp_obj_t self_in,
                                   mp_print_kind_t kind) {
    (void)kind;
    machine_audioadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "AudioADC(fs=%u, frame=%u, active=%u)",
              (unsigned)self->fs, (unsigned)self->frame, self->active);
}

/* ------------------------------------------------------------------ */
/* make_new: AudioADC(pin, fs=8000, frame=128)                         */
/* ------------------------------------------------------------------ */

static mp_obj_t machine_audioadc_make_new(const mp_obj_type_t *type,
                                          size_t n_args, size_t n_kw,
                                          const mp_obj_t *all_args) {
    (void)type;
    enum { ARG_pin, ARG_fs, ARG_frame };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin,   MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_fs,    MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 8000} },
        { MP_QSTR_frame, MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 128} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
                              MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_fs].u_int <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad fs"));
    }
    if (args[ARG_frame].u_int <= 0 ||
        args[ARG_frame].u_int > RA_STORM_ADC_MAX_FRAME_SAMPLES) {
        mp_raise_ValueError(MP_ERROR_TEXT("frame out of range"));
    }

    const machine_pin_obj_t *pin = machine_pin_find(args[ARG_pin].u_obj);

    machine_audioadc_obj_t *self = &machine_audioadc_obj;

    if (self->active) {
        if (!ra_storm_adc_deinit_checked()) {
            mp_raise_OSError(MP_EIO);
        }
        self->active = false;
    }

    self->base.type = &machine_audioadc_type;
    self->pin   = pin->pin;
    self->fs    = (uint32_t)args[ARG_fs].u_int;
    self->frame = (uint16_t)args[ARG_frame].u_int;

    if (!ra_storm_adc_init(self->pin, self->fs, self->frame)) {
        mp_raise_OSError(MP_EIO);
    }

    self->active = true;
    return MP_OBJ_FROM_PTR(self);
}

/* ------------------------------------------------------------------ */
/* Methods                                                              */
/* ------------------------------------------------------------------ */

static mp_obj_t machine_audioadc_start(mp_obj_t self_in) {
    machine_audioadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }
    if (!ra_storm_adc_start()) { mp_raise_OSError(MP_EIO); }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_audioadc_start_obj, machine_audioadc_start);

static mp_obj_t machine_audioadc_stop(mp_obj_t self_in) {
    machine_audioadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->active) { ra_storm_adc_stop(); }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_audioadc_stop_obj, machine_audioadc_stop);

static mp_obj_t machine_audioadc_ready(mp_obj_t self_in) {
    machine_audioadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { return mp_const_false; }
    return ra_storm_adc_ready() ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_audioadc_ready_obj, machine_audioadc_ready);

/* read_f32(buf) — buf must be array('f', frame)
 * Copies the ready int16 frame into buf as float32 scaled to [-1.0, +1.0].
 * Returns number of samples written, or raises OSError if not ready.       */
static mp_obj_t machine_audioadc_read_f32(mp_obj_t self_in, mp_obj_t buf_in) {
    machine_audioadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) { mp_raise_OSError(MP_ENODEV); }

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);

    if (bufinfo.typecode != 'f') {
        mp_raise_ValueError(MP_ERROR_TEXT("buf must be array('f')"));
    }
    size_t n = bufinfo.len / sizeof(float);
    if (n < self->frame) {
        mp_raise_ValueError(MP_ERROR_TEXT("buf too small"));
    }

    size_t got = 0;
    uint32_t seq = 0;
    const int16_t *src = ra_storm_adc_acquire_ready_buffer(&got, &seq);
    if (src == NULL) { mp_raise_OSError(MP_EAGAIN); }

    float *dst = (float *)bufinfo.buf;
    for (size_t i = 0; i < got; ++i) {
        dst[i] = (float)src[i] * (1.0f / 2048.0f);
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)got);
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_audioadc_read_f32_obj, machine_audioadc_read_f32);

static mp_obj_t machine_audioadc_deinit(mp_obj_t self_in) {
    machine_audioadc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->active) {
        if (!ra_storm_adc_deinit_checked()) {
            mp_raise_OSError(MP_EIO);
        }
        self->active = false;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_audioadc_deinit_obj, machine_audioadc_deinit);

/* Static singleton cleanup for the soft-reset boundary.  This releases both
 * its AGT reservation and the ADC0_SCAN_END DTC activation vector. */
bool machine_audioadc_deinit_all(void) {
    /* Call through even when Python never reached active=true: a failed
     * constructor may still own an AGT, ADC or DTC activation vector. */
    if (!ra_storm_adc_deinit_checked()) {
        return false;
    }
    machine_audioadc_obj.active = false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Type                                                                 */
/* ------------------------------------------------------------------ */

static const mp_rom_map_elem_t machine_audioadc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_start),    MP_ROM_PTR(&machine_audioadc_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),     MP_ROM_PTR(&machine_audioadc_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready),    MP_ROM_PTR(&machine_audioadc_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_f32), MP_ROM_PTR(&machine_audioadc_read_f32_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),   MP_ROM_PTR(&machine_audioadc_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(machine_audioadc_locals_dict,
                             machine_audioadc_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_audioadc_type,
    MP_QSTR_AudioADC,
    MP_TYPE_FLAG_NONE,
    make_new, machine_audioadc_make_new,
    print,    machine_audioadc_print,
    locals_dict, &machine_audioadc_locals_dict
);
