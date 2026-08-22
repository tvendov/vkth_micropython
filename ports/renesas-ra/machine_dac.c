/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Nick Moore
 * Copyright (c) 2023 Vekatech Ltd.
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
#include "py/mperrno.h"
#include "pin.h"
#include "ra/ra_dac.h"
#include "modmachine.h"

#if MICROPY_HW_ENABLE_IQ_ADC
#include "hal_data.h"
#include "ra/ra_iq_adc.h"
#endif

#if MICROPY_PY_MACHINE_DAC

typedef struct _machine_dac_obj_t {
    mp_obj_base_t base;
    uint8_t active;
    uint8_t ch;
    uint16_t mv;
    mp_hal_pin_obj_t dac;
    mp_obj_t buffer_obj;
} machine_dac_obj_t;

enum {
    MACHINE_DAC_MODE_NORMAL = 0,
    MACHINE_DAC_MODE_CIRCULAR = 1,
};

static machine_dac_obj_t machine_dac_obj[] = {
    #if defined(MICROPY_HW_DAC0)
    {{&machine_dac_type}, 0, 0, 0, MICROPY_HW_DAC0, MP_OBJ_NULL},
    #endif
    #if defined(MICROPY_HW_DAC1)
    {{&machine_dac_type}, 0, 1, 0, MICROPY_HW_DAC1, MP_OBJ_NULL}
    #endif
};

static uint16_t machine_dac_raw_to_mv(uint16_t raw) {
    return (uint16_t)((raw * 3300U) / 4095U);
}

#if MICROPY_HW_ENABLE_IQ_ADC
extern const mp_obj_type_t machine_iqadc_type;

/* One DAC channel streams a demod source at a time on this board, so a single
 * ping-pong pair is enough.  DMAC clocks these to DADR, so they must outlive the
 * transfer (static) and be aligned for the transfer size (REQ-RT-004). */
static uint16_t BSP_ALIGN_VARIABLE(4) machine_dac_stream_buf_a[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2];
static uint16_t BSP_ALIGN_VARIABLE(4) machine_dac_stream_buf_b[RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2];

/* DAC ping-pong refill, runs in the DMAC ISR.  Pulls the demod audio ring; the
 * pull writes mid-scale silence on underrun so the stream never stops.  No
 * allocation, no Python (REQ-RT-002/003). */
static bool machine_dac_iq_fill(void *ctx, uint16_t *buf, size_t n) {
    (void)ctx;
    ra_iq_adc_audio_pull(buf, n);
    /* Capture after the pull, so SCOPE sees the exact DAC codes including the
     * 2048 mid-scale silence inserted on an underrun.  The push is a gated no-op
     * unless the native LCD is currently in SCOPE view. */
    ra_iq_adc_scope_push(buf, n);
    return true;
}
#endif

static void machine_dac_stop_stream(machine_dac_obj_t *self) {
    if (ra_dac_stream_is_active(self->ch)) {
        ra_dac_stream_stop(self->ch);
    }
    self->buffer_obj = MP_OBJ_NULL;
}

static void machine_dac_raise_stream_error(machine_dac_obj_t *self, ra_dac_stream_status_t status) {
    switch (status) {
        case RA_DAC_STREAM_STATUS_INVALID_FREQ:
            mp_raise_ValueError(MP_ERROR_TEXT("freq should be > 0"));
            break;
        case RA_DAC_STREAM_STATUS_INVALID_LENGTH:
            mp_raise_ValueError(MP_ERROR_TEXT("data must be 16-bit aligned samples"));
            break;
        case RA_DAC_STREAM_STATUS_INVALID_TIMER:
            mp_raise_ValueError(MP_ERROR_TEXT("invalid AGT timer"));
            break;
        case RA_DAC_STREAM_STATUS_TIMER_BUSY:
            mp_raise_ValueError(MP_ERROR_TEXT("no free AGT timer"));
            break;
        case RA_DAC_STREAM_STATUS_TRANSFER_BUSY:
            mp_raise_ValueError(MP_ERROR_TEXT("no free DMAC channel"));
            break;
        case RA_DAC_STREAM_STATUS_LOOP_UNSUPPORTED:
            mp_raise_ValueError(MP_ERROR_TEXT("circular mode needs DTC and <=256 samples"));
            break;
        default:
            mp_raise_msg_varg(&mp_type_OSError,
                MP_ERROR_TEXT("timed DAC hw error stage=%d fsp=%d"),
                (int)ra_dac_stream_last_stage(self->ch),
                (int)ra_dac_stream_last_error(self->ch));
            break;
    }
}

static void machine_dac_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(self_in);     // const char *qstr_str(qstr q);
    uint16_t raw = ra_dac_read(self->ch);
    mp_printf(print, "DAC(DA%d [#%d], active=%u, playing=%u, out=%u mV)",
        self->ch, self->dac->pin, self->active, ra_dac_stream_is_active(self->ch), machine_dac_raw_to_mv(raw));
}

static mp_obj_t machine_dac_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_hal_pin_obj_t pin_id = MP_OBJ_NULL;
    machine_dac_obj_t *self = MP_OBJ_NULL;

    enum { ARG_pin };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin,  MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} }
    };

    mp_arg_check_num(n_args, n_kw, 1, 1, true);
    mp_arg_val_t init_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, init_args);

    // Get GPIO and optional device to connect to DAC.
    pin_id = mp_hal_get_pin_obj(init_args[ARG_pin].u_obj);

    if (pin_id) {
        for (int i = 0; i < MP_ARRAY_SIZE(machine_dac_obj); i++) {
            if (pin_id->pin == machine_dac_obj[i].dac->pin) {
                self = &machine_dac_obj[i];
                break;
            }
        }

        if (self) {
            if (ra_dac_is_dac_pin(self->dac->pin)) {
                ra_dac_init(self->dac->pin, self->ch);
                self->active = ra_dac_is_running(self->ch);
                self->mv = machine_dac_raw_to_mv(ra_dac_read(self->ch));
            } else {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Pin(%d) has no DAC output"), self->dac->pin);
            }
        } else {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Pin(%d) is used with other peripheral"), pin_id->pin);
        }
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Pin doesn't exist"));
    }

    return MP_OBJ_FROM_PTR(self);
}

// DAC.deinit()
static mp_obj_t machine_dac_deinit(mp_obj_t self_in) {
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(self_in);

    machine_dac_stop_stream(self);
    ra_dac_deinit(self->dac->pin, self->ch);
    self->active = ra_dac_is_running(self->ch);
    self->mv = 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_dac_deinit_obj, machine_dac_deinit);

// DAC.write(value)
static mp_obj_t machine_dac_write(mp_obj_t self_in, mp_obj_t data) { // mp_obj_t value_in
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(data);

    if (value < 0 || value > 4095) {
        mp_raise_ValueError(MP_ERROR_TEXT("value should be 0-4095"));
    } else
    if (self->active) {
        machine_dac_stop_stream(self);
        ra_dac_write(self->ch, value);
        self->mv = machine_dac_raw_to_mv((uint16_t)value);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_dac_write_obj, machine_dac_write);

// DAC.read()
static mp_obj_t machine_dac_read(mp_obj_t self_in) {
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(self_in);

    return MP_OBJ_NEW_SMALL_INT(ra_dac_read(self->ch));
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_dac_read_obj, machine_dac_read);

// DAC.write_mv(Vout)
static mp_obj_t machine_dac_write_mv(mp_obj_t self_in, mp_obj_t data) {  // mp_obj_t self_in, mp_obj_t value_in
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t Vout = mp_obj_get_int(data);

    if (Vout < 0 || Vout > 3300) {
        mp_raise_ValueError(MP_ERROR_TEXT("value should be 0-3300"));
    } else
    if (self->active) {
        machine_dac_stop_stream(self);
        uint16_t Dout = (Vout * 4095) / 3300;
        ra_dac_write(self->ch, Dout);
        self->mv = machine_dac_raw_to_mv(Dout);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_dac_write_mv_obj, machine_dac_write_mv);

// DAC.read_mv()
static mp_obj_t machine_dac_read_mv(mp_obj_t self_in) {
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(self_in);

    return MP_OBJ_NEW_SMALL_INT(machine_dac_raw_to_mv(ra_dac_read(self->ch)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_dac_read_mv_obj, machine_dac_read_mv);

// DAC.write_timed(data, freq, *, mode=DAC.NORMAL, transfer=DAC.TRANSFER_AUTO, timer=None)
static mp_obj_t machine_dac_write_timed(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    enum { ARG_data, ARG_freq, ARG_mode, ARG_transfer, ARG_timer };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_freq, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = MACHINE_DAC_MODE_NORMAL} },
        { MP_QSTR_transfer, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = RA_DAC_TRANSFER_AUTO} },
        { MP_QSTR_timer, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_data].u_obj, &bufinfo, MP_BUFFER_READ);

    if (bufinfo.buf == NULL || bufinfo.len == 0 || (bufinfo.len & 1U) != 0 || (((uintptr_t)bufinfo.buf) & 1U) != 0U) {
        mp_raise_ValueError(MP_ERROR_TEXT("data must be 16-bit aligned samples"));
    }
    size_t sample_count = bufinfo.len / sizeof(uint16_t);
    const uint16_t *samples = (const uint16_t *)bufinfo.buf;
    for (size_t i = 0; i < sample_count; ++i) {
        if (samples[i] > 4095U) {
            mp_raise_ValueError(MP_ERROR_TEXT("data values should be 0-4095"));
        }
    }

    bool loop;
    if (args[ARG_mode].u_int == MACHINE_DAC_MODE_NORMAL) {
        loop = false;
    } else if (args[ARG_mode].u_int == MACHINE_DAC_MODE_CIRCULAR) {
        loop = true;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid mode"));
    }

    if (args[ARG_transfer].u_int < RA_DAC_TRANSFER_AUTO || args[ARG_transfer].u_int > RA_DAC_TRANSFER_DTC) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid transfer"));
    }

    int timer_ch = -1;
    if (args[ARG_timer].u_obj != mp_const_none) {
        timer_ch = mp_obj_get_int(args[ARG_timer].u_obj);
        if (timer_ch < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid AGT timer"));
        }
    }

    ra_dac_stream_status_t status = ra_dac_write_timed(self->ch, (const uint16_t *)bufinfo.buf,
        sample_count, (uint32_t)args[ARG_freq].u_int, loop,
        (ra_dac_transfer_t)args[ARG_transfer].u_int, (int8_t)timer_ch);
    if (status != RA_DAC_STREAM_STATUS_OK) {
        machine_dac_raise_stream_error(self, status);
    }

    self->buffer_obj = args[ARG_data].u_obj;
    self->active = ra_dac_is_running(self->ch);
    self->mv = machine_dac_raw_to_mv(ra_dac_read(self->ch));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_dac_write_timed_obj, 1, machine_dac_write_timed);

#if MICROPY_HW_ENABLE_IQ_ADC
// DAC.stream(source, *, freq=None)
// Plays a demodulator's generic audio stream through this DAC's own double-buffered
// DMAC path with no CPU per sample.  source is an IQADC instance; its audio params
// (rate, block) drive the stream unless freq is overridden.  Stop with DAC.stop().
static mp_obj_t machine_dac_stream(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    enum { ARG_source, ARG_freq };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_source, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_freq, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (!mp_obj_is_type(args[ARG_source].u_obj, &machine_iqadc_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("source must be IQADC"));
    }

    uint32_t freq;
    size_t sample_count;
    ra_iq_adc_get_audio_params(&freq, &sample_count);
    if (args[ARG_freq].u_obj != mp_const_none) {
        mp_int_t f = mp_obj_get_int(args[ARG_freq].u_obj);
        if (f <= 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("freq should be > 0"));
        }
        freq = (uint32_t)f;
    }
    if (sample_count == 0 || sample_count > (RA_IQ_ADC_MAX_BLOCK_SAMPLES / 2)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid audio block"));
    }

    for (size_t i = 0; i < sample_count; ++i) {
        machine_dac_stream_buf_a[i] = 2048U;
        machine_dac_stream_buf_b[i] = 2048U;
    }

    ra_dac_stream_status_t status = ra_dac_write_timed_double_buffered(
        self->ch, machine_dac_stream_buf_a, machine_dac_stream_buf_b, true,
        sample_count, freq, machine_dac_iq_fill, NULL, NULL, -1);
    if (status != RA_DAC_STREAM_STATUS_OK) {
        machine_dac_raise_stream_error(self, status);
    }

    self->buffer_obj = MP_OBJ_NULL;
    self->active = ra_dac_is_running(self->ch);
    self->mv = machine_dac_raw_to_mv(ra_dac_read(self->ch));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_dac_stream_obj, 1, machine_dac_stream);
#endif

// DAC.stop()
static mp_obj_t machine_dac_stop(mp_obj_t self_in) {
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    machine_dac_stop_stream(self);
    self->active = ra_dac_is_running(self->ch);
    self->mv = machine_dac_raw_to_mv(ra_dac_read(self->ch));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_dac_stop_obj, machine_dac_stop);

// DAC.playing()
static mp_obj_t machine_dac_playing(mp_obj_t self_in) {
    machine_dac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    bool active = ra_dac_stream_is_active(self->ch);
    if (!active) {
        self->buffer_obj = MP_OBJ_NULL;
    }
    return mp_obj_new_bool(active);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_dac_playing_obj, machine_dac_playing);

// MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_dac_write_obj, mp_machine_dac_write);

static const mp_rom_map_elem_t machine_dac_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_dac_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&machine_dac_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&machine_dac_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_timed), MP_ROM_PTR(&machine_dac_write_timed_obj) },
    #if MICROPY_HW_ENABLE_IQ_ADC
    { MP_ROM_QSTR(MP_QSTR_stream_from), MP_ROM_PTR(&machine_dac_stream_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&machine_dac_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_playing), MP_ROM_PTR(&machine_dac_playing_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_mv), MP_ROM_PTR(&machine_dac_read_mv_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_mv), MP_ROM_PTR(&machine_dac_write_mv_obj) },
    { MP_ROM_QSTR(MP_QSTR_NORMAL), MP_ROM_INT(MACHINE_DAC_MODE_NORMAL) },
    { MP_ROM_QSTR(MP_QSTR_CIRCULAR), MP_ROM_INT(MACHINE_DAC_MODE_CIRCULAR) },
    { MP_ROM_QSTR(MP_QSTR_TRANSFER_AUTO), MP_ROM_INT(RA_DAC_TRANSFER_AUTO) },
    { MP_ROM_QSTR(MP_QSTR_TRANSFER_DMAC), MP_ROM_INT(RA_DAC_TRANSFER_DMAC) },
    { MP_ROM_QSTR(MP_QSTR_TRANSFER_DTC), MP_ROM_INT(RA_DAC_TRANSFER_DTC) },
};

static MP_DEFINE_CONST_DICT(machine_dac_locals_dict, machine_dac_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_dac_type,
    MP_QSTR_DAC,
    MP_TYPE_FLAG_NONE,
    make_new, machine_dac_make_new,
    print, machine_dac_print,
    locals_dict, &machine_dac_locals_dict
    );

// Deinitialize all DAC instances
void dac_deinit_all(void) {
    for (int i = 0; i < MP_ARRAY_SIZE(machine_dac_obj); i++) {
        if (machine_dac_obj[i].active) {
            machine_dac_stop_stream(&machine_dac_obj[i]);
            ra_dac_deinit(machine_dac_obj[i].dac->pin, machine_dac_obj[i].ch);
            machine_dac_obj[i].active = 0;
            machine_dac_obj[i].mv = 0;
        }
    }
}

#endif // MICROPY_PY_MACHINE_DAC
