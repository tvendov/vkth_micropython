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
#include <string.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "shared/runtime/mpirq.h"
#include "pin.h"
#include "modmachine.h"
#include "ra/ra_encoder.h"

#if defined(MICROPY_HW_ENCODER_A)

// Forward declaration — needed because encoder_pool_get() references the type
// before MP_DEFINE_CONST_OBJ_TYPE at the bottom of this file.
extern const mp_obj_type_t machine_encoder_type;

typedef struct _machine_encoder_obj_t {
    mp_obj_base_t base;
    encoder_config_t cfg;
    mp_obj_t callback;           // Python callback for movement notification
    volatile bool irq_scheduled; // true = deferred callback is queued or currently draining
    volatile uint32_t irq_epoch; // invalidates stale deferred callbacks across reset/recreate
    volatile uint32_t irq_scheduled_epoch; // epoch captured when the current deferred pass was queued
    volatile uint32_t irq_pending;   // ISR events observed since the current deferred dispatch pass began
    volatile uint32_t isr_count;     // total ISR invocations (for diagnostics)
    volatile uint32_t irq_coalesced; // ISR events coalesced (arrived while callback pending)
    volatile uint32_t sched_fail;    // mp_sched_schedule failures (queue full)
    volatile int32_t last_irq_count; // signed GTCNT sampled in hard IRQ context
    bool in_use;                   // true = this pool slot is allocated
} machine_encoder_obj_t;

// Invalidate queued/running deferred work so a stale scheduled callback from
// an older lifetime cannot fire after reset, disable, or recreate.
static inline void encoder_invalidate_deferred_state(machine_encoder_obj_t *self) {
    self->irq_epoch++;
    self->callback = mp_const_none;
    self->irq_scheduled = false;
    self->irq_scheduled_epoch = self->irq_epoch;
    self->irq_pending = 0;
}

// Reset upper-layer deferred-callback state for a fresh encoder lifetime.
// Used on deinit/recreate so old diagnostics and scheduler state do not leak
// into the next use of the same pool slot.
static inline void encoder_soft_state_reset(machine_encoder_obj_t *self) {
    encoder_invalidate_deferred_state(self);
    self->isr_count = 0;
    self->irq_coalesced = 0;
    self->sched_fail = 0;
    self->last_irq_count = 0;
}

// Static pool of encoder objects — one per GPT channel.
static machine_encoder_obj_t encoder_pool[RA_ENCODER_MAX_CH];

// Find or allocate a pool slot for the given GPT channel.
// Returns NULL if channel is out of range.
static machine_encoder_obj_t *encoder_pool_get(uint32_t gpt_ch) {
    if (gpt_ch >= RA_ENCODER_MAX_CH) {
        return NULL;
    }
    machine_encoder_obj_t *obj = &encoder_pool[gpt_ch];
    if (!obj->in_use) {
        memset(obj, 0, sizeof(*obj));
        obj->base.type = &machine_encoder_type;
        encoder_soft_state_reset(obj);
        obj->in_use = true;
    }
    return obj;
}

// ---- Dynamic IRQ vector lookup ----
// Map GPT channel to compare match A/B NVIC vector numbers.
// Returns true if vectors are defined for this channel, fills irq_a/irq_b.
static bool encoder_get_irq_vectors(uint32_t gpt_ch, int8_t *irq_a, int8_t *irq_b) {
    *irq_a = -1;
    *irq_b = -1;
    switch (gpt_ch) {
        #if defined(VECTOR_NUMBER_GPT0_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT0_CAPTURE_COMPARE_B)
        case 0: *irq_a = (int8_t)VECTOR_NUMBER_GPT0_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT0_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT1_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT1_CAPTURE_COMPARE_B)
        case 1: *irq_a = (int8_t)VECTOR_NUMBER_GPT1_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT1_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT2_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT2_CAPTURE_COMPARE_B)
        case 2: *irq_a = (int8_t)VECTOR_NUMBER_GPT2_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT2_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT3_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT3_CAPTURE_COMPARE_B)
        case 3: *irq_a = (int8_t)VECTOR_NUMBER_GPT3_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT3_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT4_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT4_CAPTURE_COMPARE_B)
        case 4: *irq_a = (int8_t)VECTOR_NUMBER_GPT4_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT4_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT5_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT5_CAPTURE_COMPARE_B)
        case 5: *irq_a = (int8_t)VECTOR_NUMBER_GPT5_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT5_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT6_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT6_CAPTURE_COMPARE_B)
        case 6: *irq_a = (int8_t)VECTOR_NUMBER_GPT6_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT6_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT7_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT7_CAPTURE_COMPARE_B)
        case 7: *irq_a = (int8_t)VECTOR_NUMBER_GPT7_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT7_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT8_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT8_CAPTURE_COMPARE_B)
        case 8: *irq_a = (int8_t)VECTOR_NUMBER_GPT8_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT8_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT9_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT9_CAPTURE_COMPARE_B)
        case 9: *irq_a = (int8_t)VECTOR_NUMBER_GPT9_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT9_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT10_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT10_CAPTURE_COMPARE_B)
        case 10: *irq_a = (int8_t)VECTOR_NUMBER_GPT10_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT10_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT11_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT11_CAPTURE_COMPARE_B)
        case 11: *irq_a = (int8_t)VECTOR_NUMBER_GPT11_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT11_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT12_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT12_CAPTURE_COMPARE_B)
        case 12: *irq_a = (int8_t)VECTOR_NUMBER_GPT12_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT12_CAPTURE_COMPARE_B; return true;
        #endif
        #if defined(VECTOR_NUMBER_GPT13_CAPTURE_COMPARE_A) && defined(VECTOR_NUMBER_GPT13_CAPTURE_COMPARE_B)
        case 13: *irq_a = (int8_t)VECTOR_NUMBER_GPT13_CAPTURE_COMPARE_A; *irq_b = (int8_t)VECTOR_NUMBER_GPT13_CAPTURE_COMPARE_B; return true;
        #endif
        default: return false;
    }
}

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
        { MP_QSTR_debounce, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
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

    // Get per-channel pool slot
    machine_encoder_obj_t *self = encoder_pool_get(ch);
    if (self == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("GPT channel out of range"));
    }

    // Start from a clean upper-layer state on every make_new() reuse path.
    encoder_soft_state_reset(self);

    // Deinit if already active (re-creating same channel)
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

/// \method value([new_value])
/// Get or set the encoder position.
///
/// When `debounce > 0`, the getter returns the debounced reported position,
/// not the raw hardware `GTCNT` count. Use `status()["GTCNT"]` when you need
/// the raw counter value for diagnostics.
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
    encoder_soft_state_reset(self);
    ra_encoder_deinit(&self->cfg);
    // Release pool slot. cfg is NOT zeroed here — encoder_pool_get() does
    // memset() on next reuse (lazy reset). This is intentional: the slot
    // remains stale until re-allocated, which is safe because in_use=false
    // prevents any ISR or API path from accessing it.
    self->in_use = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_encoder_deinit_obj, machine_encoder_deinit);

// Deferred trampoline — runs outside ISR context via mp_sched_schedule.
// After calling the Python callback, re-checks irq_pending: if new
// encoder pulses arrived during the callback, loops immediately
// instead of waiting for the next external schedule.  This ensures
// asyncio ThreadSafeFlag / Event wakeups are not lost.
// irq_scheduled stays asserted for the whole drain pass so the ISR only
// coalesces into irq_pending instead of queueing redundant extra callbacks.
static mp_obj_t encoder_deferred_callback(mp_obj_t self_in) {
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t scheduled_epoch = self->irq_scheduled_epoch;

    if (!self->irq_scheduled || scheduled_epoch != self->irq_epoch) {
        return mp_const_none;
    }

    for (;;) {
        // Snapshot and clear pending count *before* calling Python.
        // irq_scheduled remains true while this deferred pass is active,
        // so new ISRs only coalesce into irq_pending.
        self->irq_pending = 0;

        if (self->callback != mp_const_none) {
            nlr_buf_t nlr;
            if (nlr_push(&nlr) == 0) {
                mp_call_function_1(self->callback, self_in);
                nlr_pop();
            } else {
                // Uncaught exception — disable to prevent repeated errors
                self->callback = mp_const_none;
                ra_encoder_irq_disable(&self->cfg);
                self->irq_scheduled = false;
                self->irq_pending = 0;
                mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
                break;
            }
        }

        // Drop irq_scheduled only when no new ISR work arrived during the
        // current pass. The atomic section prevents a lost wakeup window
        // between checking irq_pending and releasing irq_scheduled.
        mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
        if (self->irq_pending == 0) {
            self->irq_scheduled = false;
            MICROPY_END_ATOMIC_SECTION(atomic_state);
            break;
        }
        MICROPY_END_ATOMIC_SECTION(atomic_state);
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
    self->last_irq_count = ra_encoder_read_hw_count(&self->cfg);
    // Coalesce: increment pending count, schedule only once while the
    // deferred callback is queued or actively draining.
    self->isr_count++;
    self->irq_pending++;
    if (self->irq_scheduled) {
        self->irq_coalesced++;
    }
    if (!self->irq_scheduled) {
        self->irq_scheduled = true;
        self->irq_scheduled_epoch = self->irq_epoch;
        if (!mp_sched_schedule(MP_OBJ_FROM_PTR(&encoder_deferred_callback_obj),
                               MP_OBJ_FROM_PTR(self))) {
            // Scheduler queue full — retry on next ISR
            self->irq_scheduled = false;
            self->sched_fail++;
        }
    }
}

/// \method irq(handler=None)
/// Set a callback for encoder movement notification.
///
/// The callback is a **coarse movement notification**, not a per-pulse event.
/// Coalescing happens at two levels:
///   1. Hardware: GPT compare-window (irq_step) — ISR fires only when
///      the counter moves at least `irq_step` counts from the last anchor.
///   2. Software: mp_sched_schedule — multiple ISR events between scheduler
///      runs are merged into a single deferred callback.
///
/// Use enc.value() as the authoritative source of the current position.
///
///     def on_move(enc):
///         print("pos:", enc.value())
///
///     enc.irq(handler=on_move)
///     enc.irq(handler=None)     # disable
static mp_obj_t machine_encoder_irq(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_handler };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_handler, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Dynamic IRQ vector lookup based on the GPT channel this encoder uses
    int8_t vec_a, vec_b;
    if (!encoder_get_irq_vectors(self->cfg.gpt_ch, &vec_a, &vec_b)) {
        mp_raise_ValueError(MP_ERROR_TEXT("encoder IRQ not available for this GPT channel"));
    }

    mp_obj_t handler = args[ARG_handler].u_obj;
    if (handler == MP_OBJ_NULL || handler == mp_const_none) {
        // Disable IRQ
        encoder_invalidate_deferred_state(self);
        ra_encoder_irq_disable(&self->cfg);
    } else if (mp_obj_is_callable(handler)) {
        encoder_soft_state_reset(self);
        self->callback = handler;
        ra_encoder_irq_enable(&self->cfg, vec_a, vec_b,
            encoder_irq_handler, (void *)self);
        self->last_irq_count = ra_encoder_read_hw_count(&self->cfg);
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("handler must be None or callable"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_encoder_irq_obj, 1, machine_encoder_irq);

// status() — return GPT register snapshot plus compare-window diagnostics.
// last_irq_count is sampled in the hard IRQ path, so it reflects the encoder
// count at notification time more closely than a later GTCNT read from Python.
static mp_obj_t machine_encoder_status(mp_obj_t self_in) {
    machine_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    encoder_status_t st;
    ra_encoder_status(&self->cfg, &st);

    mp_obj_dict_t *d = mp_obj_new_dict(15);
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTCNT),  mp_obj_new_int_from_uint(st.gtcnt));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTST),   mp_obj_new_int_from_uint(st.gtst));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTUPSR), mp_obj_new_int_from_uint(st.gtupsr));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTDNSR), mp_obj_new_int_from_uint(st.gtdnsr));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTCR),   mp_obj_new_int_from_uint(st.gtcr));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTIOR),  mp_obj_new_int_from_uint(st.gtior));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTCCRA), mp_obj_new_int_from_uint(st.gtccra));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_GTCCRB), mp_obj_new_int_from_uint(st.gtccrb));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_irq_step), mp_obj_new_int_from_uint(st.irq_step));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_irq_anchor), mp_obj_new_int(st.irq_anchor));
    // Software-level diagnostics
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_isr_count),      mp_obj_new_int_from_uint(self->isr_count));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_irq_coalesced), mp_obj_new_int_from_uint(self->irq_coalesced));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_sched_fail),    mp_obj_new_int_from_uint(self->sched_fail));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_irq_scheduled), mp_obj_new_bool(self->irq_scheduled));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_last_irq_count), mp_obj_new_int(self->last_irq_count));
    // Diagnostic hint only: this is not an exact backlog/queue-depth counter.
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_irq_pending),   mp_obj_new_int_from_uint(self->irq_pending));
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
