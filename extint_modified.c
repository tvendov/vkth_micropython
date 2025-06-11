/*
 * extint_ra6m5_fixed.c – MicroPython ExtInt backend for Renesas RA (final)
 *
 * – HARD vs SOFT прецизно разделени
 * – FLTEN=0, pin enable=1  → няма заглушени импулси
 * – При SOFT‑IRQ ISR‑ът оставя само mp_sched_schedule()
 *
 * 2025‑05‑20
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "py/runtime.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "pendsv.h"
#include "pin.h"
#include "extint.h"
#include "irq.h"
#if defined(RA4M1) || defined(RA4M3) || defined(RA4W1) || defined(RA6M1) || defined(RA6M2) || defined(RA6M3)
#include "ra_icu.h"
#endif

extern const mp_obj_type_t machine_pin_type;   // from pin.c

/* ───── globals ─────────────────────────────────────────── */
static uint8_t pyb_extint_mode[EXTI_NUM_VECTORS];
static bool    pyb_extint_hard_irq[EXTI_NUM_VECTORS];
mp_obj_t       pyb_extint_callback_arg[EXTI_NUM_VECTORS];
uint           extint_irq_no[EXTI_NUM_VECTORS];

/* ───── IRQ shim ─────────────────────────────────────────── */
void extint_callback(void *param) {
    uint irq_no = *((uint *)param);
    mp_obj_t *cb = &MP_STATE_PORT(pyb_extint_callback)[irq_no];
    if (*cb == mp_const_none) {
        return;
    }

    if (pyb_extint_hard_irq[irq_no]) {
        /* ---- HARD IRQ: изпълняваме Python в контекст на прекъсване ---- */
        mp_sched_lock();
        gc_lock();
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_call_function_1(*cb, pyb_extint_callback_arg[irq_no]);
            nlr_pop();
        } else {
            *cb = mp_const_none;
            ra_icu_disable_irq_no((uint8_t)irq_no);
            printf("Uncaught exception in hard ExtInt IRQ %u\n", (unsigned)irq_no);
            mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        }
        gc_unlock();
        mp_sched_unlock();
    } else {
        /* ---- SOFT IRQ: само опашкуваме събитието ----------------------- */
        (void)mp_sched_schedule(*cb, pyb_extint_callback_arg[irq_no]);
        /* ако връща 0 → опашката препълнена, събитието се губи */
    }
}

/* ───── ICU helper ───────────────────────────────────────── */
static inline void icu_set_trigger(uint8_t line, uint32_t mp_mode) {
    uint32_t cond;
    if (mp_mode & MP_HAL_PIN_TRIGGER_LOWLEVEL) {
        cond = 0;            // low level
    } else if ((mp_mode & MP_HAL_PIN_TRIGGER_RISING) && (mp_mode & MP_HAL_PIN_TRIGGER_FALLING)) {
        cond = 3;            // both edges
    } else if (mp_mode & MP_HAL_PIN_TRIGGER_RISING) {
        cond = 2;            // rising edge
    } else {
        cond = 1;            // falling edge
    }
    ra_icu_trigger_irq_no(line, cond);
}

/* ───── enable / disable (API) ──────────────────────────── */
void extint_enable(uint line) {
    if (line >= EXTI_NUM_VECTORS) {
        return;
    }
    mp_uint_t s = disable_irq();
    ra_icu_enable_irq_no((uint8_t)line);
    enable_irq(s);
}

void extint_disable(uint line) {
    if (line >= EXTI_NUM_VECTORS) {
        return;
    }
    mp_uint_t s = disable_irq();
    ra_icu_disable_irq_no((uint8_t)line);
    enable_irq(s);
}

/* ───── регистрация от Python (ExtInt ctor) ─────────────── */
uint extint_register(mp_obj_t pin_obj, uint32_t mode, uint32_t pull,
                     mp_obj_t cb_obj, bool override) {
    if (!mp_obj_is_type(pin_obj, &machine_pin_type)) {
        mp_raise_ValueError(MP_ERROR_TEXT("pin must be Pin"));
    }
    const machine_pin_obj_t *pin = machine_pin_find(pin_obj);
    uint8_t line;
    if (!ra_icu_find_irq_no(pin->pin, &line)) {
        mp_raise_ValueError(MP_ERROR_TEXT("pin has no EXTINT"));
    }
    if (pull != MP_HAL_PIN_PULL_NONE && pull != MP_HAL_PIN_PULL_UP) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid pull"));
    }

    mp_obj_t *slot = &MP_STATE_PORT(pyb_extint_callback)[line];
    if (!override && *slot != mp_const_none && cb_obj != mp_const_none) {
        mp_raise_ValueError(MP_ERROR_TEXT("vector busy"));
    }

    extint_disable(line);
    *slot = cb_obj;
    pyb_extint_mode[line] = mode;
    pyb_extint_hard_irq[line] = false;           // default soft
    pyb_extint_callback_arg[line] = pin_obj;
    extint_irq_no[line] = line;

    if (cb_obj != mp_const_none) {
        ra_icu_set_callback(line, (ICU_CB)extint_callback, &extint_irq_no[line]);
        ra_icu_set_pin(pin->pin, true, false);   // enable, FLTEN=0
        ra_icu_enable_pin(pin->pin);
        icu_set_trigger(line, mode);
        ra_icu_priority_irq_no(line, IRQ_PRI_EXTINT);
        extint_enable(line);
    }
    return line;
}

/* ───── регистрация през Pin.irq() ───────────────────────── */
void extint_register_pin(const machine_pin_obj_t *pin, uint32_t mode,
                         bool hard_irq, mp_obj_t cb_obj) {
    uint8_t line;
    if (!ra_icu_find_irq_no(pin->pin, &line)) {
        mp_raise_ValueError(MP_ERROR_TEXT("pin has no EXTINT"));
    }
    mp_obj_t *slot = &MP_STATE_PORT(pyb_extint_callback)[line];
    if (*slot != mp_const_none && cb_obj != mp_const_none &&
        MP_OBJ_FROM_PTR(pin) != pyb_extint_callback_arg[line]) {
        mp_raise_OSError(MP_EBUSY);
    }

    extint_disable(line);
    *slot = cb_obj;
    pyb_extint_mode[line] = mode;
    pyb_extint_hard_irq[line] = hard_irq;
    pyb_extint_callback_arg[line] = MP_OBJ_FROM_PTR(pin);
    extint_irq_no[line] = line;

    if (cb_obj != mp_const_none) {
        ra_icu_set_callback(line, (ICU_CB)extint_callback, &extint_irq_no[line]);
        ra_icu_set_pin(pin->pin, true, false);
        ra_icu_enable_pin(pin->pin);
        icu_set_trigger(line, mode);
        ra_icu_priority_irq_no(line, IRQ_PRI_EXTINT);
        extint_enable(line);
    }
}

/* ───── runtime update of trigger ───────────────────────── */
void extint_trigger_mode(uint line, uint32_t mode) {
    if (line >= EXTI_NUM_VECTORS) {
        return;
    }
    mp_uint_t s = disable_irq();
    icu_set_trigger((uint8_t)line, mode);
    enable_irq(s);
}

/* ───── boot init ───────────────────────────────────────── */
void extint_init0(void) {
    ra_icu_init();
    for (int i = 0; i < PYB_EXTI_NUM_VECTORS; ++i) {
        MP_STATE_PORT(pyb_extint_callback)[i] = mp_const_none;
    }
}

MP_REGISTER_ROOT_POINTER(mp_obj_t pyb_extint_callback[PYB_EXTI_NUM_VECTORS]);
