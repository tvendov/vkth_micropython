/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 OpenAI
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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hal_data.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "pin.h"
#include "ra/ra_dac.h"

#if MICROPY_HW_ENABLE_DAC

#if defined(MICROPY_HW_DAC1)
#define AUDIOMIXER_DAC_MAX (2)
#else
#define AUDIOMIXER_DAC_MAX (1)
#endif

#define AUDIOMIXER_MAX_VOICES (16)
#define AUDIOMIXER_LEVEL_MAX_Q15 (32767U)
#define AUDIOMIXER_DAC_MIDPOINT (2048U)

typedef enum {
    AUDIOMIXER_FORMAT_AUTO = 0,
    AUDIOMIXER_FORMAT_S8 = 1,
    AUDIOMIXER_FORMAT_U8 = 2,
    AUDIOMIXER_FORMAT_S16 = 3,
    AUDIOMIXER_FORMAT_U16 = 4,
} audiomixer_format_t;

typedef struct _audiomixer_mixer_obj_t audiomixer_mixer_obj_t;

typedef struct _audiomixer_voice_state_t {
    mp_obj_t sample_obj;
    const uint8_t *data;
    size_t sample_count;
    size_t position;
    uint16_t level_q15;
    audiomixer_format_t format;
    bool loop;
    bool active;
} audiomixer_voice_state_t;

typedef struct _audiomixer_voice_obj_t {
    mp_obj_base_t base;
    audiomixer_mixer_obj_t *mixer;
    size_t index;
} audiomixer_voice_obj_t;

struct _audiomixer_mixer_obj_t {
    mp_obj_base_t base;
    uint8_t dac_ch;
    uint32_t dac_pin;
    int8_t requested_timer_ch;
    int8_t timer_ch;
    volatile bool output_active;
    bool deinitialized;
    uint32_t sample_rate;
    size_t buffer_sample_count;
    uint16_t *mix_buffers[2];
    size_t voice_count;
    audiomixer_voice_state_t *voices;
    mp_obj_t voice_tuple_obj;
};

extern const mp_obj_type_t audiomixer_mixer_type;
extern const mp_obj_type_t audiomixer_voice_type;

MP_REGISTER_ROOT_POINTER(mp_obj_t audiomixer_active_mixers[AUDIOMIXER_DAC_MAX]);

static mp_hal_pin_obj_t audiomixer_default_dac_pin(void) {
    #if defined(MICROPY_HW_DAC0)
    return MICROPY_HW_DAC0;
    #elif defined(MICROPY_HW_DAC1)
    return MICROPY_HW_DAC1;
    #else
    return NULL;
    #endif
}

static bool audiomixer_get_dac_for_pin(mp_hal_pin_obj_t pin, uint8_t *dac_ch_out) {
    if (pin == NULL) {
        return false;
    }
    #if defined(MICROPY_HW_DAC0)
    if (pin->pin == MICROPY_HW_DAC0->pin) {
        *dac_ch_out = 0;
        return true;
    }
    #endif
    #if defined(MICROPY_HW_DAC1)
    if (pin->pin == MICROPY_HW_DAC1->pin) {
        *dac_ch_out = 1;
        return true;
    }
    #endif
    return false;
}

static mp_hal_pin_obj_t audiomixer_get_pin_for_dac(uint8_t dac_ch) {
    switch (dac_ch) {
        #if defined(MICROPY_HW_DAC0)
        case 0:
            return MICROPY_HW_DAC0;
        #endif
        #if defined(MICROPY_HW_DAC1)
        case 1:
            return MICROPY_HW_DAC1;
        #endif
        default:
            return NULL;
    }
}

static void audiomixer_root_clear(audiomixer_mixer_obj_t *self) {
    if (self->dac_ch < AUDIOMIXER_DAC_MAX && MP_STATE_PORT(audiomixer_active_mixers)[self->dac_ch] == MP_OBJ_FROM_PTR(self)) {
        MP_STATE_PORT(audiomixer_active_mixers)[self->dac_ch] = MP_OBJ_NULL;
    }
}

static void audiomixer_reset_voice_playback(audiomixer_voice_state_t *voice) {
    voice->sample_obj = MP_OBJ_NULL;
    voice->data = NULL;
    voice->sample_count = 0;
    voice->position = 0;
    voice->format = AUDIOMIXER_FORMAT_AUTO;
    voice->loop = false;
    voice->active = false;
}

static int32_t audiomixer_read_sample(const audiomixer_voice_state_t *voice) {
    const uint8_t *src = voice->data + voice->position * ((voice->format == AUDIOMIXER_FORMAT_S16 || voice->format == AUDIOMIXER_FORMAT_U16) ? 2U : 1U);

    switch (voice->format) {
        case AUDIOMIXER_FORMAT_S8:
            return ((int32_t)(int8_t)src[0]) << 8;
        case AUDIOMIXER_FORMAT_U8:
            return ((int32_t)src[0] - 128) << 8;
        case AUDIOMIXER_FORMAT_S16: {
            int16_t value = (int16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
            return (int32_t)value;
        }
        case AUDIOMIXER_FORMAT_U16: {
            uint16_t value = (uint16_t)src[0] | ((uint16_t)src[1] << 8);
            return (int32_t)value - 32768;
        }
        default:
            return 0;
    }
}

static void audiomixer_fill_midpoint(uint16_t *buf, size_t start, size_t sample_count) {
    if (buf == NULL) {
        return;
    }
    for (size_t i = start; i < sample_count; ++i) {
        buf[i] = AUDIOMIXER_DAC_MIDPOINT;
    }
}

static bool audiomixer_render_buffer(audiomixer_mixer_obj_t *self, uint16_t *buf, size_t sample_count) {
    if (self == NULL || self->deinitialized || buf == NULL) {
        return false;
    }
    bool produced_audio = false;
    for (size_t out = 0; out < sample_count; ++out) {
        int32_t mix = 0;
        bool any_voice_active = false;

        for (size_t i = 0; i < self->voice_count; ++i) {
            audiomixer_voice_state_t *voice = &self->voices[i];
            if (!voice->active || voice->data == NULL || voice->sample_count == 0) {
                continue;
            }

            any_voice_active = true;
            int32_t sample = audiomixer_read_sample(voice);
            sample = (int32_t)(((int64_t)sample * voice->level_q15) / AUDIOMIXER_LEVEL_MAX_Q15);
            mix += sample;

            size_t next_pos = voice->position + 1;
            if (next_pos >= voice->sample_count) {
                if (voice->loop) {
                    next_pos = 0;
                } else {
                    audiomixer_reset_voice_playback(voice);
                    continue;
                }
            }

            voice->position = next_pos;
        }

        if (!any_voice_active) {
            audiomixer_fill_midpoint(buf, out, sample_count);
            return produced_audio;
        }

        if (mix < -32768) {
            mix = -32768;
        } else if (mix > 32767) {
            mix = 32767;
        }

        buf[out] = (uint16_t)(((uint32_t)(mix + 32768)) >> 4);
        produced_audio = true;
    }

    return produced_audio;
}

static bool audiomixer_fill_buffer_callback(void *context, uint16_t *buf, size_t sample_count) {
    return audiomixer_render_buffer((audiomixer_mixer_obj_t *)context, buf, sample_count);
}

static void audiomixer_stream_stopped(void *context) {
    audiomixer_mixer_obj_t *self = (audiomixer_mixer_obj_t *)context;
    if (self == NULL) {
        return;
    }

    self->output_active = false;
    self->timer_ch = self->requested_timer_ch;
    audiomixer_root_clear(self);

    if (!self->deinitialized) {
        ra_dac_write(self->dac_ch, AUDIOMIXER_DAC_MIDPOINT);
    }
}

static bool audiomixer_start_output_locked(audiomixer_mixer_obj_t *self) {
    if (self == NULL || self->deinitialized) {
        return false;
    }

    if (self->output_active) {
        if (ra_dac_stream_is_active(self->dac_ch)) {
            return true;
        }
        self->output_active = false;
        self->timer_ch = self->requested_timer_ch;
        audiomixer_root_clear(self);
    }

    if (ra_dac_stream_is_active(self->dac_ch)) {
        return false;
    }

    mp_obj_t active_obj = MP_STATE_PORT(audiomixer_active_mixers)[self->dac_ch];
    if (active_obj != MP_OBJ_NULL && active_obj != MP_OBJ_FROM_PTR(self)) {
        MP_STATE_PORT(audiomixer_active_mixers)[self->dac_ch] = MP_OBJ_NULL;
    }

    bool buf0_ready = audiomixer_render_buffer(self, self->mix_buffers[0], self->buffer_sample_count);
    if (!buf0_ready) {
        ra_dac_write(self->dac_ch, AUDIOMIXER_DAC_MIDPOINT);
        return true;
    }

    bool buf1_ready = audiomixer_render_buffer(self, self->mix_buffers[1], self->buffer_sample_count);

    MP_STATE_PORT(audiomixer_active_mixers)[self->dac_ch] = MP_OBJ_FROM_PTR(self);
    ra_dac_stream_status_t status = ra_dac_write_timed_double_buffered(
        self->dac_ch,
        self->mix_buffers[0],
        self->mix_buffers[1],
        buf1_ready,
        self->buffer_sample_count,
        self->sample_rate,
        audiomixer_fill_buffer_callback,
        audiomixer_stream_stopped,
        self,
        self->requested_timer_ch);

    if (status != RA_DAC_STREAM_STATUS_OK) {
        /* A bounded hardware cleanup may fail closed while the low-level state
         * still owns this context and its buffers.  Keep the mixer rooted until a
         * later stop/is_active retry completes the Close. */
        bool ownership_retained = ra_dac_stream_is_active(self->dac_ch);
        self->output_active = ownership_retained;
        if (!ownership_retained) {
            audiomixer_root_clear(self);
            self->timer_ch = self->requested_timer_ch;
            ra_dac_write(self->dac_ch, AUDIOMIXER_DAC_MIDPOINT);
        }
        return false;
    }

    self->timer_ch = ra_dac_stream_timer(self->dac_ch);
    self->output_active = true;
    return true;
}

static bool audiomixer_stop_output_locked(audiomixer_mixer_obj_t *self, bool center_output) {
    if (self == NULL) {
        return false;
    }

    if (self->output_active || ra_dac_stream_is_active(self->dac_ch)) {
        if (!ra_dac_stream_stop(self->dac_ch)) {
            return false;
        }
    }

    self->output_active = false;
    self->timer_ch = self->requested_timer_ch;
    audiomixer_root_clear(self);
    if (center_output) {
        ra_dac_write(self->dac_ch, AUDIOMIXER_DAC_MIDPOINT);
    }
    return true;
}

static void audiomixer_stop_all_voices_locked(audiomixer_mixer_obj_t *self) {
    for (size_t i = 0; i < self->voice_count; ++i) {
        audiomixer_reset_voice_playback(&self->voices[i]);
    }
}

static bool audiomixer_mixer_cleanup(audiomixer_mixer_obj_t *self) {
    if (self == NULL) {
        return false;
    }

    mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
    audiomixer_stop_all_voices_locked(self);
    MICROPY_END_ATOMIC_SECTION(atomic_state);
    if (!audiomixer_stop_output_locked(self, true)) {
        return false;
    }

    if (!self->deinitialized) {
        ra_dac_deinit(self->dac_pin, self->dac_ch);
        self->deinitialized = true;
    }
    return true;
}

/* Soft-reset hook: mixer buffers and callback contexts live on the GC heap and
 * must be detached from DMAC before mp_deinit can sweep them. */
bool audiomixer_deinit_all(void) {
    bool all_stopped = true;
    for (size_t ch = 0U; ch < AUDIOMIXER_DAC_MAX; ++ch) {
        mp_obj_t active = MP_STATE_PORT(audiomixer_active_mixers)[ch];
        if (active != MP_OBJ_NULL) {
            audiomixer_mixer_obj_t *self = MP_OBJ_TO_PTR(active);
            if (!audiomixer_mixer_cleanup(self)) {
                all_stopped = false;
            }
        }
    }
    return all_stopped;
}

static void audiomixer_raise_busy(void) {
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("DAC channel busy"));
}

static audiomixer_format_t audiomixer_detect_format(mp_buffer_info_t *bufinfo, audiomixer_format_t requested) {
    if (requested != AUDIOMIXER_FORMAT_AUTO) {
        return requested;
    }

    switch ((char)bufinfo->typecode) {
        case 'b':
            return AUDIOMIXER_FORMAT_S8;
        case 'B':
            return AUDIOMIXER_FORMAT_U8;
        case 'h':
            return AUDIOMIXER_FORMAT_S16;
        case 'H':
            return AUDIOMIXER_FORMAT_U16;
        default:
            return AUDIOMIXER_FORMAT_U8;
    }
}

static size_t audiomixer_bytes_per_sample(audiomixer_format_t format) {
    switch (format) {
        case AUDIOMIXER_FORMAT_S16:
        case AUDIOMIXER_FORMAT_U16:
            return 2;
        case AUDIOMIXER_FORMAT_S8:
        case AUDIOMIXER_FORMAT_U8:
            return 1;
        default:
            return 0;
    }
}

static void audiomixer_mixer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    audiomixer_mixer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Mixer(voices=%u, sample_rate=%u, dac=%u, timer=%d, playing=%u)",
        (unsigned)self->voice_count, (unsigned)self->sample_rate, (unsigned)self->dac_ch,
        (int)self->timer_ch, (unsigned)self->output_active);
}

static mp_obj_t audiomixer_mixer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum {
        ARG_voice_count,
        ARG_sample_rate,
        ARG_channel_count,
        ARG_bits_per_sample,
        ARG_samples_signed,
        ARG_buffer_size,
        ARG_pin,
        ARG_timer,
        ARG_channel,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_voice_count, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_sample_rate, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_channel_count, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_bits_per_sample, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16} },
        { MP_QSTR_samples_signed, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_buffer_size, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 4096} },
        { MP_QSTR_pin, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_timer, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_channel, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };

    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);

    size_t voice_count = (size_t)parsed_args[ARG_voice_count].u_int;
    uint32_t sample_rate = (uint32_t)parsed_args[ARG_sample_rate].u_int;

    if (voice_count == 0 || voice_count > AUDIOMIXER_MAX_VOICES) {
        mp_raise_ValueError(MP_ERROR_TEXT("voice_count should be 1-16"));
    }
    if (sample_rate == 0U || sample_rate > 48000U) {
        mp_raise_ValueError(MP_ERROR_TEXT("sample_rate should be 1-48000"));
    }
    if (parsed_args[ARG_channel_count].u_int != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("only mono output is supported"));
    }
    if (parsed_args[ARG_bits_per_sample].u_int != 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("only 16-bit mixer output is supported"));
    }
    if (parsed_args[ARG_buffer_size].u_int < 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer_size should be >= 4"));
    }

    mp_hal_pin_obj_t dac_pin = audiomixer_default_dac_pin();
    uint8_t dac_ch = 0;
    if (dac_pin == NULL || !audiomixer_get_dac_for_pin(dac_pin, &dac_ch)) {
        mp_raise_ValueError(MP_ERROR_TEXT("no DAC output configured"));
    }

    int requested_channel = parsed_args[ARG_channel].u_int;
    if (parsed_args[ARG_pin].u_obj != mp_const_none) {
        dac_pin = mp_hal_get_pin_obj(parsed_args[ARG_pin].u_obj);
        if (!audiomixer_get_dac_for_pin(dac_pin, &dac_ch)) {
            mp_raise_ValueError(MP_ERROR_TEXT("pin has no DAC output"));
        }
    }
    if (requested_channel >= 0) {
        mp_hal_pin_obj_t channel_pin = audiomixer_get_pin_for_dac((uint8_t)requested_channel);
        if (channel_pin == NULL) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid DAC channel"));
        }
        if (parsed_args[ARG_pin].u_obj != mp_const_none && channel_pin->pin != dac_pin->pin) {
            mp_raise_ValueError(MP_ERROR_TEXT("pin/channel mismatch"));
        }
        dac_pin = channel_pin;
        dac_ch = (uint8_t)requested_channel;
    }

    int timer_ch = -1;
    if (parsed_args[ARG_timer].u_obj != mp_const_none) {
        timer_ch = mp_obj_get_int(parsed_args[ARG_timer].u_obj);
        if (timer_ch < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid AGT timer"));
        }
    }

    size_t buffer_sample_count = (size_t)parsed_args[ARG_buffer_size].u_int / (2U * sizeof(uint16_t));
    if (buffer_sample_count == 0 || buffer_sample_count > UINT16_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid buffer_size"));
    }

    audiomixer_mixer_obj_t *self = mp_obj_malloc(audiomixer_mixer_obj_t, &audiomixer_mixer_type);
    self->dac_ch = dac_ch;
    self->dac_pin = dac_pin->pin;
    self->requested_timer_ch = (int8_t)timer_ch;
    self->timer_ch = (int8_t)timer_ch;
    self->output_active = false;
    self->deinitialized = false;
    self->sample_rate = sample_rate;
    self->buffer_sample_count = buffer_sample_count;
    self->mix_buffers[0] = m_new(uint16_t, buffer_sample_count);
    self->mix_buffers[1] = m_new(uint16_t, buffer_sample_count);
    audiomixer_fill_midpoint(self->mix_buffers[0], 0, buffer_sample_count);
    audiomixer_fill_midpoint(self->mix_buffers[1], 0, buffer_sample_count);
    self->voice_count = voice_count;
    self->voice_tuple_obj = MP_OBJ_NULL;
    self->voices = m_new(audiomixer_voice_state_t, voice_count);
    memset(self->voices, 0, sizeof(audiomixer_voice_state_t) * voice_count);

    mp_obj_t *voice_items = m_new(mp_obj_t, voice_count);
    for (size_t i = 0; i < voice_count; ++i) {
        audiomixer_voice_obj_t *voice_obj = mp_obj_malloc(audiomixer_voice_obj_t, &audiomixer_voice_type);
        voice_obj->mixer = self;
        voice_obj->index = i;
        self->voices[i].level_q15 = AUDIOMIXER_LEVEL_MAX_Q15;
        audiomixer_reset_voice_playback(&self->voices[i]);
        voice_items[i] = MP_OBJ_FROM_PTR(voice_obj);
    }
    self->voice_tuple_obj = mp_obj_new_tuple(voice_count, voice_items);

    ra_dac_init(self->dac_pin, self->dac_ch);
    ra_dac_write(self->dac_ch, AUDIOMIXER_DAC_MIDPOINT);

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t audiomixer_mixer_stop(mp_obj_t self_in) {
    audiomixer_mixer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
    audiomixer_stop_all_voices_locked(self);
    MICROPY_END_ATOMIC_SECTION(atomic_state);
    if (!audiomixer_stop_output_locked(self, true)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("DAC stop timeout"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(audiomixer_mixer_stop_obj, audiomixer_mixer_stop);

static mp_obj_t audiomixer_mixer_playing(mp_obj_t self_in) {
    audiomixer_mixer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->output_active) {
        (void)ra_dac_stream_is_active(self->dac_ch);
    }
    return mp_obj_new_bool(self->output_active);
}
static MP_DEFINE_CONST_FUN_OBJ_1(audiomixer_mixer_playing_obj, audiomixer_mixer_playing);

static mp_obj_t audiomixer_mixer_deinit(mp_obj_t self_in) {
    audiomixer_mixer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!audiomixer_mixer_cleanup(self)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("DAC stop timeout"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(audiomixer_mixer_deinit_obj, audiomixer_mixer_deinit);

static void audiomixer_mixer_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    audiomixer_mixer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        if (attr == MP_QSTR_voice) {
            dest[0] = self->voice_tuple_obj;
        } else if (attr == MP_QSTR_sample_rate) {
            dest[0] = mp_obj_new_int_from_uint(self->sample_rate);
        } else if (attr == MP_QSTR_voice_count) {
            dest[0] = mp_obj_new_int_from_uint(self->voice_count);
        } else if (attr == MP_QSTR_channel) {
            dest[0] = mp_obj_new_int(self->dac_ch);
        } else if (attr == MP_QSTR_timer) {
            dest[0] = mp_obj_new_int(self->timer_ch);
        } else {
            dest[1] = MP_OBJ_SENTINEL;
        }
    }
}

static const mp_rom_map_elem_t audiomixer_mixer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&audiomixer_mixer_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_playing), MP_ROM_PTR(&audiomixer_mixer_playing_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&audiomixer_mixer_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(audiomixer_mixer_locals_dict, audiomixer_mixer_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    audiomixer_mixer_type,
    MP_QSTR_Mixer,
    MP_TYPE_FLAG_NONE,
    make_new, audiomixer_mixer_make_new,
    print, audiomixer_mixer_print,
    attr, audiomixer_mixer_attr,
    locals_dict, &audiomixer_mixer_locals_dict
    );

static void audiomixer_voice_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    audiomixer_voice_obj_t *self = MP_OBJ_TO_PTR(self_in);
    audiomixer_voice_state_t *voice = &self->mixer->voices[self->index];
    mp_printf(print, "Voice(index=%u, playing=%u, loop=%u, level=%.3f)",
        (unsigned)self->index, (unsigned)voice->active, (unsigned)voice->loop,
        (double)voice->level_q15 / (double)AUDIOMIXER_LEVEL_MAX_Q15);
}

static mp_obj_t audiomixer_voice_play(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    audiomixer_voice_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    audiomixer_mixer_obj_t *mixer = self->mixer;
    audiomixer_voice_state_t *voice = &mixer->voices[self->index];

    enum { ARG_sample, ARG_repeat, ARG_format };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_sample, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_repeat, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_format, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = AUDIOMIXER_FORMAT_AUTO} },
    };
    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(parsed_args[ARG_sample].u_obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.buf == NULL || bufinfo.len == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("sample must not be empty"));
    }

    int format_arg = parsed_args[ARG_format].u_int;
    if (format_arg < AUDIOMIXER_FORMAT_AUTO || format_arg > AUDIOMIXER_FORMAT_U16) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid sample format"));
    }

    audiomixer_format_t format = audiomixer_detect_format(&bufinfo, (audiomixer_format_t)format_arg);
    size_t bytes_per_sample = audiomixer_bytes_per_sample(format);
    if (bytes_per_sample == 0 || (bufinfo.len % bytes_per_sample) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("sample size/format mismatch"));
    }

    mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
    voice->sample_obj = parsed_args[ARG_sample].u_obj;
    voice->data = (const uint8_t *)bufinfo.buf;
    voice->sample_count = bufinfo.len / bytes_per_sample;
    voice->position = 0;
    voice->format = format;
    voice->loop = parsed_args[ARG_repeat].u_bool;
    voice->active = true;
    MICROPY_END_ATOMIC_SECTION(atomic_state);

    bool started = audiomixer_start_output_locked(mixer);
    if (!started) {
        atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
        audiomixer_reset_voice_playback(voice);
        MICROPY_END_ATOMIC_SECTION(atomic_state);
        audiomixer_raise_busy();
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(audiomixer_voice_play_obj, 1, audiomixer_voice_play);

static mp_obj_t audiomixer_voice_stop(mp_obj_t self_in) {
    audiomixer_voice_obj_t *self = MP_OBJ_TO_PTR(self_in);
    audiomixer_mixer_obj_t *mixer = self->mixer;
    mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
    audiomixer_reset_voice_playback(&mixer->voices[self->index]);
    bool any_voice_active = false;
    for (size_t i = 0; i < mixer->voice_count; ++i) {
        if (mixer->voices[i].active) {
            any_voice_active = true;
            break;
        }
    }
    MICROPY_END_ATOMIC_SECTION(atomic_state);
    if (!any_voice_active) {
        if (!audiomixer_stop_output_locked(mixer, true)) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("DAC stop timeout"));
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(audiomixer_voice_stop_obj, audiomixer_voice_stop);

static mp_obj_t audiomixer_voice_playing(mp_obj_t self_in) {
    audiomixer_voice_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(self->mixer->voices[self->index].active);
}
static MP_DEFINE_CONST_FUN_OBJ_1(audiomixer_voice_playing_obj, audiomixer_voice_playing);

static void audiomixer_voice_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    audiomixer_voice_obj_t *self = MP_OBJ_TO_PTR(self_in);
    audiomixer_voice_state_t *voice = &self->mixer->voices[self->index];

    if (dest[0] == MP_OBJ_NULL) {
        if (attr == MP_QSTR_level) {
            dest[0] = mp_obj_new_float((mp_float_t)voice->level_q15 / (mp_float_t)AUDIOMIXER_LEVEL_MAX_Q15);
        } else if (attr == MP_QSTR_index) {
            dest[0] = mp_obj_new_int_from_uint(self->index);
        } else {
            dest[1] = MP_OBJ_SENTINEL;
        }
    } else if (dest[1] != MP_OBJ_NULL) {
        if (attr == MP_QSTR_level) {
            mp_float_t level = mp_obj_get_float(dest[1]);
            if (level < 0.0f || level > 1.0f) {
                mp_raise_ValueError(MP_ERROR_TEXT("level should be 0.0-1.0"));
            }
            voice->level_q15 = (uint16_t)(level * (mp_float_t)AUDIOMIXER_LEVEL_MAX_Q15 + 0.5f);
            dest[0] = MP_OBJ_NULL;
        }
    }
}

static const mp_rom_map_elem_t audiomixer_voice_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_play), MP_ROM_PTR(&audiomixer_voice_play_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&audiomixer_voice_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_playing), MP_ROM_PTR(&audiomixer_voice_playing_obj) },
};
static MP_DEFINE_CONST_DICT(audiomixer_voice_locals_dict, audiomixer_voice_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    audiomixer_voice_type,
    MP_QSTR_MixerVoice,
    MP_TYPE_FLAG_NONE,
    print, audiomixer_voice_print,
    attr, audiomixer_voice_attr,
    locals_dict, &audiomixer_voice_locals_dict
    );

static const mp_rom_map_elem_t audiomixer_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_audiomixer) },
    { MP_ROM_QSTR(MP_QSTR_Mixer), MP_ROM_PTR(&audiomixer_mixer_type) },
    { MP_ROM_QSTR(MP_QSTR_FORMAT_AUTO), MP_ROM_INT(AUDIOMIXER_FORMAT_AUTO) },
    { MP_ROM_QSTR(MP_QSTR_FORMAT_S8), MP_ROM_INT(AUDIOMIXER_FORMAT_S8) },
    { MP_ROM_QSTR(MP_QSTR_FORMAT_U8), MP_ROM_INT(AUDIOMIXER_FORMAT_U8) },
    { MP_ROM_QSTR(MP_QSTR_FORMAT_S16), MP_ROM_INT(AUDIOMIXER_FORMAT_S16) },
    { MP_ROM_QSTR(MP_QSTR_FORMAT_U16), MP_ROM_INT(AUDIOMIXER_FORMAT_U16) },
    { MP_ROM_QSTR(MP_QSTR_AUTO), MP_ROM_INT(AUDIOMIXER_FORMAT_AUTO) },
    { MP_ROM_QSTR(MP_QSTR_S8), MP_ROM_INT(AUDIOMIXER_FORMAT_S8) },
    { MP_ROM_QSTR(MP_QSTR_U8), MP_ROM_INT(AUDIOMIXER_FORMAT_U8) },
    { MP_ROM_QSTR(MP_QSTR_S16), MP_ROM_INT(AUDIOMIXER_FORMAT_S16) },
    { MP_ROM_QSTR(MP_QSTR_U16), MP_ROM_INT(AUDIOMIXER_FORMAT_U16) },
};
static MP_DEFINE_CONST_DICT(audiomixer_module_globals, audiomixer_module_globals_table);

const mp_obj_module_t mp_module_audiomixer = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&audiomixer_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_audiomixer, mp_module_audiomixer);

#endif // MICROPY_HW_ENABLE_DAC
