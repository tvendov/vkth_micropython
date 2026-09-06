/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
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

// This file is never compiled standalone, it's included directly from
// extmod/machine_adc.c via MICROPY_PY_MACHINE_ADC_INCLUDEFILE.

#include "py/mphal.h"
#include "py/mperrno.h"
#include "ra_adc.h"
#include "ra_tx_hw.h"
#if defined(RA6M3) && defined(MICROPY_HW_ENABLE_IQ_ADC) && MICROPY_HW_ENABLE_IQ_ADC
#include "ra_iq_adc.h"
#endif
#if defined(MICROPY_HW_ENABLE_AUDIOADC) && MICROPY_HW_ENABLE_AUDIOADC
#include "ra_storm_adc.h"
#endif

#define ADC_SAMPLETIME_DEFAULT  1
#define ADC_CHANNEL_VREFINT     (ADC_REF)
#define ADC_CHANNEL_TEMPSENSOR  (ADC_TEMP)
#define ADC_SAMPLETIME_DEFAULT_INT  1

typedef struct {
    uint8_t dummy;
} ADC_TypeDef;

static void machine_adc_validate_bits(mp_int_t bits) {
    #if defined(RA4M2)
    if (bits != 8 && bits != 10 && bits != 12) {
        mp_raise_ValueError(MP_ERROR_TEXT("bits must be 8, 10 or 12"));
    }
    #elif defined(RA4M1) || defined(RA4W1)
    if (bits != 12 && bits != 14) {
        mp_raise_ValueError(MP_ERROR_TEXT("bits must be 12 or 14"));
    }
    #else
    if (bits != 8 && bits != 10 && bits != 12) {
        mp_raise_ValueError(MP_ERROR_TEXT("bits must be 8, 10 or 12"));
    }
    #endif
}

// Timeout for waiting for end-of-conversion
#define ADC_EOC_TIMEOUT_MS (10)

// This is a synthesised channel representing the maximum ADC reading (useful to scale other channels)
#define ADC_CHANNEL_VREF (0xffff)

/******************************************************************************/
// MicroPython bindings for machine.ADC

#if defined(ADC_CHANNEL_VBAT)
#define MICROPY_PY_MACHINE_ADC_CLASS_CONSTANTS_CORE_VBAT \
    { MP_ROM_QSTR(MP_QSTR_CORE_VBAT), MP_ROM_INT(ADC_CHANNEL_VBAT) },
#else
#define MICROPY_PY_MACHINE_ADC_CLASS_CONSTANTS_CORE_VBAT
#endif

#define MICROPY_PY_MACHINE_ADC_CLASS_CONSTANTS \
    { MP_ROM_QSTR(MP_QSTR_VREF), MP_ROM_INT(ADC_CHANNEL_VREF) }, \
    { MP_ROM_QSTR(MP_QSTR_CORE_VREF), MP_ROM_INT(ADC_CHANNEL_VREFINT) }, \
    { MP_ROM_QSTR(MP_QSTR_CORE_TEMP), MP_ROM_INT(ADC_CHANNEL_TEMPSENSOR) }, \
    { MP_ROM_QSTR(MP_QSTR_REF_AVCC), MP_ROM_INT(RA_ADC_VREF_AVCC) }, \
    { MP_ROM_QSTR(MP_QSTR_REF_EXTERNAL), MP_ROM_INT(RA_ADC_VREF_EXTERNAL) }, \
    { MP_ROM_QSTR(MP_QSTR_REF_INTERNAL), MP_ROM_INT(RA_ADC_VREF_INTERNAL) }, \
    MICROPY_PY_MACHINE_ADC_CLASS_CONSTANTS_CORE_VBAT \

typedef struct _machine_adc_obj_t {
    mp_obj_base_t base;
    ADC_TypeDef *adc;
    uint32_t channel;
    uint32_t pin;
    uint32_t sample_time;
    #if MICROPY_HW_ENABLE_TX
    uint32_t tx_adc_epoch;
    #endif
} machine_adc_obj_t;

/* ADC0/ADC1 scan selection, resolution and S&H registers are shared with
 * IQADC.  A standalone ADC read used to overwrite that live configuration and
 * leave the SDR corrupted until a full peripheral reconstruction/reset. */
static void machine_adc_require_unowned(void) {
    if (ra_tx_hw_owns_adc()) {
        mp_raise_OSError(MP_EBUSY);
    }
    #if defined(RA6M3) && defined(MICROPY_HW_ENABLE_IQ_ADC) && MICROPY_HW_ENABLE_IQ_ADC
    if (ra_iq_adc_owns_adc()) {
        mp_raise_OSError(MP_EBUSY);
    }
    #endif
    #if defined(MICROPY_HW_ENABLE_AUDIOADC) && MICROPY_HW_ENABLE_AUDIOADC
    if (ra_storm_adc_owns_adc()) {
        mp_raise_OSError(MP_EBUSY);
    }
    #endif
}

static void machine_adc_require_current(machine_adc_obj_t *self) {
    machine_adc_require_unowned();
    #if MICROPY_HW_ENABLE_TX
    /* AM/FM may have closed or reconfigured the shared ADC since this object
     * was constructed. Reconstruct it instead of reading stale/gated hardware. */
    if (self->tx_adc_epoch != ra_tx_hw_adc_epoch()) {
        mp_raise_OSError(MP_ENODEV);
    }
    #else
    (void)self;
    #endif
}

static void mp_machine_adc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    #if MICROPY_HW_ENABLE_TX
    machine_adc_require_current(self);
    #endif
    uint8_t resolution = (uint8_t)ra_adc_get_resolution();
    mp_printf(print, "<ADC%u channel=%u>", resolution, self->channel);
}

static ra_adc_vref_t machine_adc_get_vref_arg(mp_obj_t vref_in) {
    if (mp_obj_is_str(vref_in)) {
        qstr vref_qstr = mp_obj_str_get_qstr(vref_in);
        if (vref_qstr == MP_QSTR_avcc) {
            return RA_ADC_VREF_AVCC;
        }
        if (vref_qstr == MP_QSTR_external) {
            return RA_ADC_VREF_EXTERNAL;
        }
        if (vref_qstr == MP_QSTR_internal) {
            return RA_ADC_VREF_INTERNAL;
        }
        mp_raise_ValueError(MP_ERROR_TEXT("vref must be 'avcc', 'external' or 'internal'"));
    }

    mp_int_t vref = mp_obj_get_int(vref_in);
    if (vref == RA_ADC_VREF_AVCC || vref == RA_ADC_VREF_EXTERNAL || vref == RA_ADC_VREF_INTERNAL) {
        return (ra_adc_vref_t)vref;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("invalid vref"));
}

// ADC(source, *, bits=..., vref=...)
static mp_obj_t mp_machine_adc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_source, ARG_bits, ARG_vref };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_bits, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_vref, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_obj_t source = args[ARG_source].u_obj;
    mp_int_t bits = args[ARG_bits].u_int;
    mp_obj_t vref_in = args[ARG_vref].u_obj;
    if (bits != -1) {
        machine_adc_validate_bits(bits);
    }

    bool find = false;
    uint8_t channel;
    uint32_t pin;
    uint32_t sample_time = ADC_SAMPLETIME_DEFAULT;
    if (mp_obj_is_int(source)) {
        channel = (uint8_t)mp_obj_get_int(source);
        find = ra_adc_ch_to_pin((uint8_t)channel, (uint32_t *)&pin);
        if (!find) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Channel(%q) does not have ADC capabilities"), channel);
        }
    } else {
        const machine_pin_obj_t *pin_obj = machine_pin_find(source);
        find = ra_adc_pin_to_ch((uint32_t)pin_obj->pin, (uint8_t *)&channel);
        if (!find) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Pin(%q) does not have ADC capabilities"), pin_obj->name);
        }
        pin = pin_obj->pin;
    }

    machine_adc_require_unowned();
    ra_adc_init();
    if (bits != -1) {
        // ADC resolution is global to the peripheral in this port.
        ra_adc_set_resolution((uint8_t)bits);
    }
    if (vref_in != MP_OBJ_NULL) {
        // ADC reference selection is global to the peripheral in this port.
        if (!ra_adc_set_vref(machine_adc_get_vref_arg(vref_in))) {
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported vref"));
        }
    }

    machine_adc_obj_t *o = mp_obj_malloc(machine_adc_obj_t, &machine_adc_type);
    o->adc = (ADC_TypeDef *)NULL;
    o->channel = channel;
    o->pin = pin;
    o->sample_time = sample_time;
    ra_adc_enable((uint8_t)pin);
    #if defined(RA6M3)
    /* A plain machine.ADC object is always a direct ADC measurement.  Do not
     * inherit stale special-input routing left by an earlier owner/run. */
    if (!ra_adc_prepare_direct_ch((uint8_t)channel)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("direct ADC routing failed"));
    }
    #endif
    #if MICROPY_HW_ENABLE_TX
    o->tx_adc_epoch = ra_tx_hw_adc_epoch();
    #endif
    return MP_OBJ_FROM_PTR(o);
}

static mp_int_t mp_machine_adc_read(machine_adc_obj_t *self) {
    machine_adc_require_current(self);
    return ra_adc_read_ch(self->channel);
}

static mp_int_t mp_machine_adc_read_u16(machine_adc_obj_t *self) {
    machine_adc_require_current(self);
    mp_uint_t raw = (mp_uint_t)ra_adc_read_ch(self->channel);
    mp_int_t bits = (mp_int_t)ra_adc_get_resolution();
    // Scale raw reading to 16 bit value using a Taylor expansion (for 8 <= bits <= 16)
    mp_uint_t u16 = raw << (16 - bits) | raw >> (2 * bits - 16);
    return u16;
}
