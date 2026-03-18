/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2021 Renesas Electronics Corporation
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

#include <stdint.h>
#include <string.h>

#include "py/objfun.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "shared/runtime/mpirq.h"
#include "timer.h"
#include "pin.h"
#include "irq.h"
#include "ra/ra_timer.h"
#include "ra/ra_utils.h"

#define TIMER_SIZE MICROPY_HW_MAX_TIMER
#define TIMER_CHANNEL (1)
#define TIMER_COUNTER (1)
#define TIMER_PERIOD (1)
#define TIMER_CHANNEL_MODE_OC (1)
#define TIMER_CHANNEL_MODE_IC (2)
#define TIMER_IC_MEASURE_PERIOD (1)
#define TIMER_IC_MEASURE_PULSE_WIDTH_LOW (2)
#define TIMER_IC_MEASURE_PULSE_WIDTH_HIGH (3)
#define TIMER_IC_MEASURE_EVENT_COUNT (4)

void timer_irq_handler(void *param);
static mp_obj_t pyb_timer_freq(size_t n_args, const mp_obj_t *args);
static mp_obj_t pyb_timer_period(size_t n_args, const mp_obj_t *args);

#if defined(TIMER_CHANNEL)
static const mp_obj_type_t pyb_timer_channel_type;

typedef struct _pyb_timer_channel_obj_t {
    mp_obj_base_t base;
    struct _pyb_timer_obj_t *timer;
    uint8_t channel;
    uint8_t mode;
    uint8_t measure;
    mp_obj_t callback;
    struct _pyb_timer_channel_obj_t *next;
} pyb_timer_channel_obj_t;
#endif

typedef struct _pyb_timer_obj_t {
    mp_obj_base_t base;
    uint8_t tim_id;
    mp_obj_t callback;
    bool ishard;
    #if defined(TIMER_CHANNEL)
    pyb_timer_channel_obj_t *channel;
    #endif
} pyb_timer_obj_t;
#define PYB_TIMER_OBJ_ALL_NUM MP_ARRAY_SIZE(MP_STATE_PORT(pyb_timer_obj_all))

static mp_obj_t pyb_timer_deinit(mp_obj_t self_in);
static mp_obj_t pyb_timer_callback(mp_obj_t self_in, mp_obj_t callback);
#if defined(TIMER_CHANNEL)
static mp_obj_t pyb_timer_channel_callback(mp_obj_t self_in, mp_obj_t callback);
#endif
void timer_init0(void) {
    for (uint i = 0; i < PYB_TIMER_OBJ_ALL_NUM; i++) {
        MP_STATE_PORT(pyb_timer_obj_all)[i] = NULL;
    }
}

// unregister all interrupt sources
void timer_deinit(void) {
    for (uint i = 0; i < PYB_TIMER_OBJ_ALL_NUM; i++) {
        pyb_timer_obj_t *tim = MP_STATE_PORT(pyb_timer_obj_all)[i];
        if (tim != NULL && !ra_agt_timer_is_reserved(i)) {
            pyb_timer_deinit(MP_OBJ_FROM_PTR(tim));
        }
    }
    // Clear all reservation bits so they don't survive soft reset.
    // Internal drivers (e.g. WS2812) will re-reserve on next init.
    ra_agt_timer_clear_all_reservations();
    ra_dmac_clear_all_reservations();
}

#if defined(TIMER_CHANNEL)
static bool timer_has_channel_callbacks(pyb_timer_obj_t *self) {
    pyb_timer_channel_obj_t *chan = self->channel;
    while (chan != NULL) {
        if (chan->callback != mp_const_none) {
            return true;
        }
        chan = chan->next;
    }
    return false;
}
#endif

static void timer_update_backend_callback(pyb_timer_obj_t *self) {
    uint32_t ch = self->tim_id - 1;
    bool enable = self->callback != mp_const_none;

    #if defined(TIMER_CHANNEL)
    if (!enable) {
        enable = timer_has_channel_callbacks(self);
    }
    #endif

    if (enable) {
        ra_agt_timer_set_callback(ch, (AGT_TIMER_CB)timer_irq_handler, (void *)&self->tim_id);
    } else {
        ra_agt_timer_set_callback(ch, NULL, NULL);
    }
}

/*
 * Timer Class
 */

typedef mp_uint_t (*timer_fast_asm_fun_t)(mp_uint_t);

static void *timer_get_fast_asm_entry(mp_obj_t handler) {
    if (handler == mp_const_none) {
        return NULL;
    }

    #if MICROPY_EMIT_INLINE_THUMB
    if (mp_obj_is_type(handler, &mp_type_fun_asm)) {
        mp_obj_fun_asm_t *fun = MP_OBJ_TO_PTR(handler);
        if (fun->n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("fast IRQ handler must take 1 argument"));
        }
        return MICROPY_MAKE_POINTER_CALLABLE((void *)fun->fun_data);
    }
    #endif

    return NULL;
}

static ra_agt_timer_mode_t timer_get_mode(mp_obj_t mode_in) {
    mp_int_t mode = mp_obj_get_int(mode_in);

    if (mode == 1) {
        return RA_AGT_TIMER_MODE_ONE_SHOT;
    }
    if (mode == 2) {
        return RA_AGT_TIMER_MODE_PERIODIC;
    }

    mp_raise_ValueError(MP_ERROR_TEXT("invalid timer mode"));
}

static uint8_t timer_get_channel_mode(mp_obj_t mode_in) {
    mp_int_t mode = mp_obj_get_int(mode_in);

    if (mode == TIMER_CHANNEL_MODE_OC || mode == TIMER_CHANNEL_MODE_IC) {
        return (uint8_t)mode;
    }

    mp_raise_ValueError(MP_ERROR_TEXT("invalid channel mode"));
}

static ra_agt_timer_capture_edge_t timer_get_capture_edge(mp_obj_t edge_in) {
    mp_int_t edge = mp_obj_get_int(edge_in);

    if (edge == RA_AGT_TIMER_CAPTURE_EDGE_RISING ||
        edge == RA_AGT_TIMER_CAPTURE_EDGE_FALLING ||
        edge == RA_AGT_TIMER_CAPTURE_EDGE_BOTH) {
        return (ra_agt_timer_capture_edge_t)edge;
    }

    mp_raise_ValueError(MP_ERROR_TEXT("invalid capture edge"));
}

static ra_agt_timer_capture_measure_t timer_get_capture_measure(mp_obj_t measure_in) {
    mp_int_t measure = mp_obj_get_int(measure_in);

    if (measure == TIMER_IC_MEASURE_PERIOD) {
        return RA_AGT_TIMER_CAPTURE_MEASURE_PERIOD;
    }
    if (measure == TIMER_IC_MEASURE_PULSE_WIDTH_LOW) {
        return RA_AGT_TIMER_CAPTURE_MEASURE_PULSE_WIDTH_LOW;
    }
    if (measure == TIMER_IC_MEASURE_PULSE_WIDTH_HIGH) {
        return RA_AGT_TIMER_CAPTURE_MEASURE_PULSE_WIDTH_HIGH;
    }
    if (measure == TIMER_IC_MEASURE_EVENT_COUNT) {
        return RA_AGT_TIMER_CAPTURE_MEASURE_EVENT_COUNT;
    }

    mp_raise_ValueError(MP_ERROR_TEXT("invalid capture measure"));
}

static void pyb_timer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    pyb_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Timer(%u)", self->tim_id);
}

/// \method init(*, mode=Timer.PERIODIC, freq=None, period=None, callback=None, hard=True, fast=False)
/// Initialise the hardware timer.
///
/// Exactly one of `freq` or `period` must be provided:
///
///     tim.init(freq=100)                  # trigger at 100Hz
///     tim.init(period=1000)               # raw AGT period counts
///
/// Keyword arguments:
///
///   - `mode` - `Timer.ONE_SHOT` or `Timer.PERIODIC`
///   - `freq` - timer frequency in Hz
///   - `period` - timer period in raw AGT counts
///   - `callback` - as per Timer.callback()
///   - `hard` - whether Python callbacks run in hard-IRQ context
///   - `fast` - if true, `callback` must be `@micropython.asm_thumb` and is
///              called directly from the timer IRQ with the timer id as its
///              single argument
static mp_obj_t pyb_timer_init_helper(pyb_timer_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_freq, ARG_period, ARG_callback, ARG_hard, ARG_fast };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode,         MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(2)} },
        { MP_QSTR_freq,         MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_period,       MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_callback,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_hard,         MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_fast,         MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };
    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    uint32_t ch = self->tim_id - 1;
    bool have_freq = args[ARG_freq].u_obj != mp_const_none;
    bool have_period = args[ARG_period].u_obj != mp_const_none;
    mp_obj_t effective_callback = args[ARG_callback].u_obj != mp_const_none ? args[ARG_callback].u_obj : self->callback;
    void *fast_entry = NULL;

    if (!ra_agt_timer_is_valid(ch)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer doesn't exist"));
    }
    if (ra_agt_timer_is_reserved(ch)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
    }
    if (have_freq == have_period) {
        mp_raise_TypeError(MP_ERROR_TEXT("must specify exactly one of freq or period"));
    }
    if (args[ARG_fast].u_bool) {
        fast_entry = timer_get_fast_asm_entry(effective_callback);
        if (fast_entry == NULL) {
            mp_raise_TypeError(MP_ERROR_TEXT("fast IRQ requires @micropython.asm_thumb handler"));
        }
    }

    // init TIM
    ra_agt_timer_set_mode(ch, timer_get_mode(args[ARG_mode].u_obj));
    ra_agt_timer_init(ch, 1.0f);
    if (have_freq) {
        mp_obj_t freq_args[2];
        freq_args[0] = self;
        freq_args[1] = args[ARG_freq].u_obj;
        pyb_timer_freq(2, (const mp_obj_t *)&freq_args);
    } else {
        mp_obj_t period_args[2];
        period_args[0] = self;
        period_args[1] = args[ARG_period].u_obj;
        pyb_timer_period(2, (const mp_obj_t *)&period_args);
    }
    self->ishard = args[ARG_hard].u_bool;
    pyb_timer_callback(MP_OBJ_FROM_PTR(self), effective_callback);
    if (fast_entry != NULL) {
        ra_agt_timer_set_fast_irq(ch, true, fast_entry, self->tim_id);
    }
    return mp_const_none;
}

/// \classmethod \constructor(id, ...)
/// Construct a new timer object of the given id.  If additional
/// arguments are given, then the timer is initialised by `init(...)`.
/// Hardware timer `id` values for this type are `1..MICROPY_HW_MAX_TIMER`.
static mp_obj_t pyb_timer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    // get the timer id
    mp_int_t tim_id = mp_obj_get_int(args[0]);
    if (tim_id < 1 || tim_id > PYB_TIMER_OBJ_ALL_NUM) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer doesn't exist"));
    }
    // create new Timer object
    pyb_timer_obj_t *tim;
    if (MP_STATE_PORT(pyb_timer_obj_all)[tim_id - 1] == NULL) {
        // create new Timer object
        tim = m_new_obj(pyb_timer_obj_t);
        memset(tim, 0, sizeof(*tim));
        tim->base.type = &pyb_timer_type;
        tim->tim_id = tim_id;
        tim->callback = mp_const_none;
        tim->ishard = true;
        MP_STATE_PORT(pyb_timer_obj_all)[tim_id - 1] = tim;
    } else {
        // reference existing Timer object
        tim = MP_STATE_PORT(pyb_timer_obj_all)[tim_id - 1];
    }
    if (n_args > 1 || n_kw > 0) {
        // start the peripheral
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        pyb_timer_init_helper(tim, n_args - 1, args + 1, &kw_args);
    }
    return MP_OBJ_FROM_PTR(tim);
}

static mp_obj_t pyb_timer_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return pyb_timer_init_helper(MP_OBJ_TO_PTR(args[0]), n_args - 1, args + 1, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pyb_timer_init_obj, 1, pyb_timer_init);

/// \method deinit()
/// Deinitialises the timer.
///
/// Disables the callback (and the associated irq).
/// Disables any channel callbacks (and the associated irq).
/// Releases any assigned AGT pins, stops the timer, and disables the
/// timer peripheral.
static mp_obj_t pyb_timer_deinit(mp_obj_t self_in) {
    pyb_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (ra_agt_timer_is_reserved(self->tim_id - 1)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
    }

    // Disable the base interrupt
    pyb_timer_callback(self_in, mp_const_none);

    #if defined(TIMER_CHANNEL)
    pyb_timer_channel_obj_t *chan = self->channel;
    self->channel = NULL;

    // Disable the channel interrupts
    while (chan != NULL) {
        pyb_timer_channel_callback(MP_OBJ_FROM_PTR(chan), mp_const_none);
        if (chan->mode == TIMER_CHANNEL_MODE_IC) {
            ra_agt_timer_input_pin_release(self->tim_id - 1);
        } else {
            ra_agt_timer_channel_pin_release(self->tim_id - 1, chan->channel);
        }
        pyb_timer_channel_obj_t *prev_chan = chan;
        chan = chan->next;
        prev_chan->next = NULL;
    }
    #endif

    ra_agt_timer_deinit(self->tim_id - 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pyb_timer_deinit_obj, pyb_timer_deinit);

#if defined(TIMER_CHANNEL)
/// \method channel(channel, mode, ...)
///
/// If only a channel number is passed, then a previously initialized channel
/// object is returned (or `None` if there is no previous channel).
///
/// Otherwise, a TimerChannel object is initialized and returned.
///
/// This RA AGT implementation supports:
///   - `channel(0, mode=Timer.IC, ...)` for input capture
///   - `channel(1, mode=Timer.OC, ...)` and `channel(2, mode=Timer.OC, ...)`
///     for output compare, where the selected timer/channel/pin combination is
///     validated against the board pin map
///
/// Keyword arguments:
///   - `mode` - `Timer.OC` or `Timer.IC`
///   - `pin` - required for input capture, optional for output compare
///   - `callback` - channel callback; event-count input capture does not
///                  support callbacks
///   - `compare` - initial compare value for output compare
///   - `edge` - `Timer.RISING`, `Timer.FALLING`, or `Timer.BOTH`
///   - `measure` - `Timer.IC_PERIOD`, `Timer.IC_PULSE_WIDTH_LOW`,
///                 `Timer.IC_PULSE_WIDTH_HIGH`, or `Timer.IC_EVENT_COUNT`
///
static mp_obj_t pyb_timer_channel(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_pin, ARG_callback, ARG_compare, ARG_edge, ARG_measure };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode,                MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(TIMER_CHANNEL_MODE_OC)} },
        { MP_QSTR_pin,                 MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_callback,            MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_compare,             MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_edge,                MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(RA_AGT_TIMER_CAPTURE_EDGE_RISING)} },
        { MP_QSTR_measure,             MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(TIMER_IC_MEASURE_PERIOD)} },
    };

    pyb_timer_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    uint32_t timer_ch = self->tim_id - 1;
    mp_int_t channel = mp_obj_get_int(pos_args[1]);
    uint8_t channel_mode;
    ra_agt_timer_capture_edge_t edge;
    ra_agt_timer_capture_measure_t measure;

    if (ra_agt_timer_is_reserved(timer_ch)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
    }

    if (n_args == 2 && kw_args->used == 0) {
        pyb_timer_channel_obj_t *existing = self->channel;
        while (existing != NULL) {
            if (existing->channel == channel) {
                return MP_OBJ_FROM_PTR(existing);
            }
            existing = existing->next;
        }
        return mp_const_none;
    }

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 2, pos_args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    channel_mode = timer_get_channel_mode(args[ARG_mode].u_obj);
    edge = timer_get_capture_edge(args[ARG_edge].u_obj);
    measure = timer_get_capture_measure(args[ARG_measure].u_obj);

    if (channel_mode == TIMER_CHANNEL_MODE_IC) {
        if (channel != 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("input capture uses channel 0"));
        }
        if (!ra_agt_timer_is_input_capture_supported(timer_ch)) {
            mp_raise_ValueError(MP_ERROR_TEXT("input capture not supported"));
        }
        if (ra_agt_timer_get_mode(timer_ch) != RA_AGT_TIMER_MODE_PERIODIC) {
            mp_raise_ValueError(MP_ERROR_TEXT("input capture requires periodic timer"));
        }
    } else if (!ra_agt_timer_is_output_channel(timer_ch, channel)) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid channel (%d)"), channel);
    }

    pyb_timer_channel_obj_t *chan = self->channel;
    pyb_timer_channel_obj_t *prev_chan = NULL;

    while (chan != NULL) {
        if (chan->channel == channel && chan->mode == channel_mode) {
            break;
        }
        prev_chan = chan;
        chan = chan->next;
    }

    // If there was already a channel, then remove it from the list. Note that
    // the order we do things here is important so as to appear atomic to
    // the IRQ handler.
    if (chan) {
        // Turn off any IRQ associated with the channel.
        pyb_timer_channel_callback(MP_OBJ_FROM_PTR(chan), mp_const_none);
        if (chan->mode == TIMER_CHANNEL_MODE_IC) {
            ra_agt_timer_input_pin_release(timer_ch);
        } else {
            ra_agt_timer_channel_pin_release(timer_ch, chan->channel);
        }

        // Unlink the channel from the list.
        if (prev_chan) {
            prev_chan->next = chan->next;
        } else {
            self->channel = chan->next;
        }
        chan->next = NULL;
    }

    chan = m_new_obj(pyb_timer_channel_obj_t);
    memset(chan, 0, sizeof(*chan));
    chan->base.type = &pyb_timer_channel_type;
    chan->timer = self;
    chan->channel = channel;
    chan->mode = channel_mode;
    chan->measure = (uint8_t)measure;

    if (channel_mode == TIMER_CHANNEL_MODE_IC &&
        measure == RA_AGT_TIMER_CAPTURE_MEASURE_EVENT_COUNT &&
        args[ARG_callback].u_obj != mp_const_none) {
        mp_raise_ValueError(MP_ERROR_TEXT("event count callback not supported"));
    }
    if (args[ARG_callback].u_obj != mp_const_none && !mp_obj_is_callable(args[ARG_callback].u_obj)) {
        mp_raise_ValueError(MP_ERROR_TEXT("callback must be None or a callable object"));
    }

    mp_obj_t pin_obj = args[ARG_pin].u_obj;
    if (channel_mode == TIMER_CHANNEL_MODE_IC) {
        if (pin_obj == mp_const_none) {
            mp_raise_ValueError(MP_ERROR_TEXT("input capture pin required"));
        }
        const machine_pin_obj_t *pin = machine_pin_find(pin_obj);
        if (!ra_agt_timer_input_pin_assign(timer_ch, pin->pin, measure, edge)) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid AGT input pin"));
        }
    } else if (pin_obj != mp_const_none) {
        const machine_pin_obj_t *pin = machine_pin_find(pin_obj);
        if (!ra_agt_timer_channel_pin_assign(timer_ch, channel, pin->pin)) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid AGT output pin"));
        }
    }
    if (channel_mode == TIMER_CHANNEL_MODE_OC && args[ARG_compare].u_obj != mp_const_none) {
        if (!ra_agt_timer_set_compare(timer_ch, channel, (uint32_t)mp_obj_get_int(args[ARG_compare].u_obj))) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid compare value"));
        }
    }
    // Link the channel to the timer before we turn the channel on.
    // Note that this needs to appear atomic to the IRQ handler (the write
    // to self->channel is atomic, so we're good, but I thought I'd mention
    // in case this was ever changed in the future).
    chan->next = self->channel;
    self->channel = chan;

    chan->callback = mp_const_none;
    pyb_timer_channel_callback(MP_OBJ_FROM_PTR(chan), args[ARG_callback].u_obj);
    return MP_OBJ_FROM_PTR(chan);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pyb_timer_channel_obj, 2, pyb_timer_channel);
#endif

#if TIMER_COUNTER
/// \method counter([value])
/// Get or set the current raw AGT counter value.
static mp_obj_t pyb_timer_counter(size_t n_args, const mp_obj_t *args) {
    pyb_timer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint32_t ch = self->tim_id - 1;
    if (n_args == 1) {
        // get
        return mp_obj_new_int_from_uint(ra_agt_timer_get_counter(ch));
    } else {
        // set
        if (ra_agt_timer_is_reserved(ch)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
        }
        if (!ra_agt_timer_set_counter(ch, (uint32_t)mp_obj_get_int(args[1]))) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid counter"));
        }
        return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_timer_counter_obj, 1, 2, pyb_timer_counter);
#endif

/// \method freq([value])
/// Get or set the timer frequency in Hz.
/// Setting the frequency restarts the timer with a new clock/period setup.
static mp_obj_t pyb_timer_freq(size_t n_args, const mp_obj_t *args) {
    pyb_timer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int ch = self->tim_id - 1;
    if (n_args == 1) {
        // get
        #if MICROPY_PY_BUILTINS_FLOAT
        float freq = ra_agt_timer_get_freq(ch);
        return mp_obj_new_float(freq);
        #else
        uint32_t freq = (uint32_t)ra_agt_timer_get_freq(ch);
        return mp_obj_new_int(freq);
        #endif
    } else {
        // set
        if (ra_agt_timer_is_reserved(ch)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
        }
        mp_float_t freq;
        if (0) {
        #if MICROPY_PY_BUILTINS_FLOAT
        } else if (mp_obj_is_type(args[1], &mp_type_float)) {
            freq = mp_obj_get_float(args[1]);
        #endif
        } else {
            freq = mp_obj_get_int(args[1]);
        }
        if (freq == 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("freq must not be 0"));
        }
        ra_agt_timer_stop(ch);
        ra_agt_timer_set_freq(ch, freq);
        ra_agt_timer_start(ch);
        return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_timer_freq_obj, 1, 2, pyb_timer_freq);

#if TIMER_PERIOD
/// \method period([value])
/// Get or set the timer period in raw AGT counts.
static mp_obj_t pyb_timer_period(size_t n_args, const mp_obj_t *args) {
    pyb_timer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint32_t ch = self->tim_id - 1;
    if (n_args == 1) {
        // get
        return mp_obj_new_int_from_uint(ra_agt_timer_get_period(ch));
    } else {
        // set
        if (ra_agt_timer_is_reserved(ch)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
        }
        uint32_t period = (uint32_t)mp_obj_get_int(args[1]);
        if (period == 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("period must not be 0"));
        }
        ra_agt_timer_stop(ch);
        if (!ra_agt_timer_set_period(ch, period)) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid period"));
        }
        ra_agt_timer_start(ch);
        return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_timer_period_obj, 1, 2, pyb_timer_period);
#endif

/// \method callback(fun)
/// Set the base timer callback for cycle-end events.
/// `fun` is passed 1 argument, the timer object.
/// If `fun` is `None` then the callback will be disabled.
/// `fast=True` is configured via Timer.init(), not here.
static mp_obj_t pyb_timer_callback(mp_obj_t self_in, mp_obj_t callback) {
    pyb_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t ch = self->tim_id - 1;

    if (ra_agt_timer_is_reserved(ch)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
    }

    ra_agt_timer_set_fast_irq(ch, false, NULL, 0);

    if (callback == mp_const_none) {
        self->callback = mp_const_none;
    } else if (mp_obj_is_callable(callback)) {
        self->callback = callback;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("callback must be None or a callable object"));
    }
    timer_update_backend_callback(self);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(pyb_timer_callback_obj, pyb_timer_callback);

static const mp_rom_map_elem_t pyb_timer_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&pyb_timer_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&pyb_timer_deinit_obj) },
    #if TIMER_COUNTER
    { MP_ROM_QSTR(MP_QSTR_counter), MP_ROM_PTR(&pyb_timer_counter_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_freq), MP_ROM_PTR(&pyb_timer_freq_obj) },
    #if TIMER_PERIOD
    { MP_ROM_QSTR(MP_QSTR_period), MP_ROM_PTR(&pyb_timer_period_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_callback), MP_ROM_PTR(&pyb_timer_callback_obj) },
    #if defined(TIMER_CHANNEL)
    { MP_ROM_QSTR(MP_QSTR_channel), MP_ROM_PTR(&pyb_timer_channel_obj) },
    #endif
};
static MP_DEFINE_CONST_DICT(pyb_timer_locals_dict, pyb_timer_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    pyb_timer_type,
    MP_QSTR_Timer,
    MP_TYPE_FLAG_NONE,
    make_new, pyb_timer_make_new,
    locals_dict, &pyb_timer_locals_dict,
    print, pyb_timer_print
    );

#if defined(TIMER_CHANNEL)
/*
 * Timer Channel
 */

/// \moduleref pyb
/// \class TimerChannel - setup a channel for a timer.
///
/// Timer channels are used for AGT input capture and output compare.
///
/// TimerChannel objects are created using the Timer.channel() method.
static void pyb_timer_channel_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    pyb_timer_channel_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_printf(print, "TimerChannel(timer=%u, channel=%u, mode=%s",
        self->timer->tim_id,
        self->channel,
        self->mode == TIMER_CHANNEL_MODE_IC ? "IC" : "OC");
    mp_printf(print, ")");
}

/// \method capture([value])
/// Get the captured value associated with an input capture channel.
/// For input capture, this value is read-only.

/// \method compare([value])
/// Get or set the compare value associated with an output compare channel.
static mp_obj_t pyb_timer_channel_capture_compare(size_t n_args, const mp_obj_t *args) {
    pyb_timer_channel_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint32_t ch = self->timer->tim_id - 1;
    if (n_args == 1) {
        // get
        if (self->mode == TIMER_CHANNEL_MODE_IC) {
            return mp_obj_new_int_from_uint(ra_agt_timer_get_capture(ch));
        }
        return mp_obj_new_int_from_uint(ra_agt_timer_get_compare(ch, self->channel));
    } else {
        // set
        if (ra_agt_timer_is_reserved(ch)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
        }
        if (self->mode == TIMER_CHANNEL_MODE_IC) {
            mp_raise_ValueError(MP_ERROR_TEXT("capture is read-only"));
        }
        if (!ra_agt_timer_set_compare(ch, self->channel, (uint32_t)mp_obj_get_int(args[1]))) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid compare value"));
        }
        return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_timer_channel_capture_compare_obj, 1, 2, pyb_timer_channel_capture_compare);

/// \method callback(fun)
/// Set the function to be called when the timer channel triggers.
/// For input capture this is called on capture events.
/// For output compare this is called on compare-match A/B events.
/// `fun` is passed 1 argument, the timer object.
/// If `fun` is `None` then the callback will be disabled.
/// Event-count input capture does not support callbacks.
static mp_obj_t pyb_timer_channel_callback(mp_obj_t self_in, mp_obj_t callback) {
    pyb_timer_channel_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t ch = self->timer->tim_id - 1;

    if (ra_agt_timer_is_reserved(ch)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer channel is reserved"));
    }

    if (self->mode == TIMER_CHANNEL_MODE_IC &&
        self->measure == RA_AGT_TIMER_CAPTURE_MEASURE_EVENT_COUNT &&
        callback != mp_const_none) {
        mp_raise_ValueError(MP_ERROR_TEXT("event count callback not supported"));
    }

    if (callback != mp_const_none) {
        ra_agt_timer_set_fast_irq(ch, false, NULL, 0);
    }

    if (callback == mp_const_none) {
        self->callback = mp_const_none;
    } else if (mp_obj_is_callable(callback)) {
        self->callback = callback;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("callback must be None or a callable object"));
    }

    if (self->mode == TIMER_CHANNEL_MODE_OC) {
        ra_agt_timer_set_compare_irq(ch, self->channel, callback != mp_const_none);
    }
    timer_update_backend_callback(self->timer);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(pyb_timer_channel_callback_obj, pyb_timer_channel_callback);

static const mp_rom_map_elem_t pyb_timer_channel_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_callback), MP_ROM_PTR(&pyb_timer_channel_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_capture), MP_ROM_PTR(&pyb_timer_channel_capture_compare_obj) },
    { MP_ROM_QSTR(MP_QSTR_compare), MP_ROM_PTR(&pyb_timer_channel_capture_compare_obj) },
};
static MP_DEFINE_CONST_DICT(pyb_timer_channel_locals_dict, pyb_timer_channel_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    pyb_timer_channel_type,
    MP_QSTR_TimerChannel,
    MP_TYPE_FLAG_NONE,
    locals_dict, &pyb_timer_channel_locals_dict,
    print, pyb_timer_channel_print
    );
#endif

static void timer_handle_irq_timer(pyb_timer_obj_t *tim, mp_obj_t callback) {
    if (mp_irq_dispatch(callback, MP_OBJ_FROM_PTR(tim), tim->ishard) < 0) {
        // Uncaught exception; disable the callback so it doesn't run again.
        tim->callback = mp_const_none;
        timer_update_backend_callback(tim);
    }
}

static void timer_handle_irq_channel(pyb_timer_obj_t *tim, pyb_timer_channel_obj_t *chan) {
    if (mp_irq_dispatch(chan->callback, MP_OBJ_FROM_PTR(tim), tim->ishard) < 0) {
        // Uncaught exception; disable the callback so it doesn't run again.
        chan->callback = mp_const_none;
        if (chan->mode == TIMER_CHANNEL_MODE_OC) {
            ra_agt_timer_set_compare_irq(tim->tim_id - 1, chan->channel, false);
        }
        timer_update_backend_callback(tim);
    }
}

void timer_irq_handler(void *param) {
    uint tim_id = *(uint *)param;
    if ((tim_id != 0) && (tim_id - 1 < PYB_TIMER_OBJ_ALL_NUM)) {
        // get the timer object
        pyb_timer_obj_t *tim = MP_STATE_PORT(pyb_timer_obj_all)[tim_id - 1];
        uint32_t ch = tim_id - 1;
        ra_agt_timer_irq_event_t event;

        if (tim == NULL) {
            // do nohting
            return;
        }
        event = ra_agt_timer_get_irq_event(ch);
        if (event == RA_AGT_TIMER_IRQ_EVENT_CYCLE_END && tim->callback != mp_const_none) {
            timer_handle_irq_timer(tim, tim->callback);
        }

        #if defined(TIMER_CHANNEL)
        pyb_timer_channel_obj_t *chan = tim->channel;
        while (chan != NULL) {
            if (((event == RA_AGT_TIMER_IRQ_EVENT_CAPTURE) &&
                chan->mode == TIMER_CHANNEL_MODE_IC &&
                chan->callback != mp_const_none) ||
                ((event == RA_AGT_TIMER_IRQ_EVENT_COMPARE_A) &&
                chan->mode == TIMER_CHANNEL_MODE_OC &&
                chan->channel == 1 &&
                chan->callback != mp_const_none) ||
                ((event == RA_AGT_TIMER_IRQ_EVENT_COMPARE_B) &&
                chan->mode == TIMER_CHANNEL_MODE_OC &&
                chan->channel == 2 &&
                chan->callback != mp_const_none)) {
                timer_handle_irq_channel(tim, chan);
            }
            chan = chan->next;
        }
        #endif
    }
}

MP_REGISTER_ROOT_POINTER(struct _pyb_timer_obj_t *pyb_timer_obj_all[MICROPY_HW_MAX_TIMER]);
