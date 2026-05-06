/*
 * lorawan/glue/timer_board.c
 *
 * Phase 3 v1 implementation of the LoRaWAN timer service.
 *
 * Uses the canonical upstream `TimerEvent_t` struct (from
 * `lorawan/system/timer.h`) so that imported LoRaMac sources compile
 * unmodified against this layer. The extension fields (precision flag,
 * fine_us offset) live in a small sidecar table keyed by TimerEvent_t
 * pointer — this keeps the upstream struct binary-compatible.
 *
 * Two-stage timing:
 *   AGT4 fires at 1 kHz (PERIODIC). Each tick increments `s_tick_ms`
 *   and walks the active list. For each event whose deadline is within
 *   `LORAWAN_AGT5_HANDOFF_MS`, AGT4 hands off the remaining countdown
 *   to AGT5 ONE_SHOT (PCLKB/8 = 6.25 MHz, 160 ns/tick). AGT5 fires its
 *   own IRQ at the precise sub-ms moment and posts the callback to the
 *   MicroPython scheduler.
 *
 * Concurrency: `s_agt5_owner` (the TimerEvent_t currently held by AGT5)
 * is the single AGT5-state variable. Only one precision deadline can
 * own AGT5 at a time; subsequent precision deadlines fall back to
 * ms-precision via the AGT4 walk on later ticks.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"

#include "ra/ra_timer.h"
#include "glue/timer_board.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

// ---- State ---------------------------------------------------------------

static volatile uint32_t       s_tick_ms;       // monotonic ms counter
static volatile TimerEvent_t  *s_timer_list;    // head of armed list
static bool                    s_initialized;

// AGT5 state.
static volatile TimerEvent_t  *s_agt5_owner;    // NULL = idle
static uint32_t                s_agt4_period;
static uint32_t                s_agt5_period_per_ms;
static bool                    s_agt5_ready;

// Sidecar table for the renesas-ra extension fields. The upstream
// TimerEvent_t has no room for these and we deliberately do not patch
// it; instead, lookup by pointer key at the few sites that need it.
//
// `arm_subtick_us` captures the AGT4 sub-tick µs offset at TimerStart()
// time. When AGT4 walks down to the handoff threshold and arms AGT5,
// we add this offset to the AGT5 countdown so the total fire time
// (real-µs from TimerStart) matches the requested ReloadValue × 1000
// + fine_us — preserving sub-ms accuracy through the ms-only
// Timestamp = s_tick_ms + ReloadValue compute.
#define LORAWAN_TIMER_EXT_SLOTS  (16)
typedef struct {
    TimerEvent_t *evt;       // NULL → free slot
    int16_t       fine_us;
    bool          precision;
    uint16_t      arm_subtick_us;
} timer_ext_slot_t;
static timer_ext_slot_t s_ext[LORAWAN_TIMER_EXT_SLOTS];

#define LORAWAN_AGT5_HANDOFF_MS  (5u)

// Helper: enter/leave critical section.
static inline mp_uint_t enter_critical(void) {
    return disable_irq();
}
static inline void leave_critical(mp_uint_t state) {
    enable_irq(state);
}

// ---- Sidecar table accessors --------------------------------------------

// Find an existing slot for `evt`, or allocate a free one. Returns NULL
// only if all slots are in use (extremely unlikely — LoRaMac uses ≤8
// timers).
static timer_ext_slot_t *ext_get_or_alloc(TimerEvent_t *evt) {
    timer_ext_slot_t *free_slot = NULL;
    for (size_t i = 0; i < LORAWAN_TIMER_EXT_SLOTS; ++i) {
        if (s_ext[i].evt == evt) {
            return &s_ext[i];
        }
        if (s_ext[i].evt == NULL && free_slot == NULL) {
            free_slot = &s_ext[i];
        }
    }
    if (free_slot != NULL) {
        free_slot->evt = evt;
        free_slot->fine_us = 0;
        free_slot->precision = false;
        free_slot->arm_subtick_us = 0;
    }
    return free_slot;
}

static timer_ext_slot_t *ext_lookup(TimerEvent_t *evt) {
    for (size_t i = 0; i < LORAWAN_TIMER_EXT_SLOTS; ++i) {
        if (s_ext[i].evt == evt) {
            return &s_ext[i];
        }
    }
    return NULL;
}

// ---- Deferred dispatch (scheduler context) ------------------------------

#define LORAWAN_TIMER_POOL_SIZE  (8)

typedef struct {
    mp_sched_node_t node;
    TimerEvent_t *evt;
    bool in_use;
} timer_dispatch_slot_t;

static timer_dispatch_slot_t s_dispatch_pool[LORAWAN_TIMER_POOL_SIZE];

static void timer_dispatch_cb(mp_sched_node_t *node) {
    timer_dispatch_slot_t *slot = (timer_dispatch_slot_t *)node;
    TimerEvent_t *evt = slot->evt;
    slot->in_use = false;
    slot->evt = NULL;
    if (evt != NULL && evt->Callback != NULL) {
        evt->Callback();
    }
}

static bool dispatch_post(TimerEvent_t *evt) {
    for (size_t i = 0; i < LORAWAN_TIMER_POOL_SIZE; ++i) {
        if (!s_dispatch_pool[i].in_use) {
            s_dispatch_pool[i].in_use = true;
            s_dispatch_pool[i].evt = evt;
            return mp_sched_schedule_node(&s_dispatch_pool[i].node,
                timer_dispatch_cb);
        }
    }
    return false;
}

// ---- AGT4 sub-tick read --------------------------------------------------
// Returns µs elapsed in the current ms, [0, 999]. No clock-divisor
// hard-coding — derives ratio from the actual configured period.
static inline uint32_t read_agt4_subtick_us(void) {
    if (s_agt4_period == 0) {
        return 0;
    }
    uint32_t cnt = ra_agt_timer_get_counter(LORAWAN_AGT_TICK_CH);
    uint32_t reload = s_agt4_period - 1u;
    if (cnt > reload) {
        return 0;
    }
    uint32_t elapsed_ticks = reload - cnt;
    return (elapsed_ticks * 1000u) / reload;
}

// ---- AGT5 arming + ISR ---------------------------------------------------

static void agt5_arm(TimerEvent_t *evt, uint32_t remaining_us) {
    if (!s_agt5_ready || s_agt5_owner != NULL || s_agt5_period_per_ms == 0) {
        return;
    }
    uint32_t ticks =
        ((uint64_t)remaining_us * s_agt5_period_per_ms) / 1000u;
    if (ticks == 0) {
        ticks = 1;
    }
    if (ticks > 0xFFFFu) {
        ticks = 0xFFFFu;
    }
    s_agt5_owner = evt;
    ra_agt_timer_set_period(LORAWAN_AGT_TIMEOUT_CH, ticks);
    ra_agt_timer_start(LORAWAN_AGT_TIMEOUT_CH);
}

static void agt5_oneshot_isr(void *param) {
    (void)param;
    TimerEvent_t *e = (TimerEvent_t *)s_agt5_owner;
    s_agt5_owner = NULL;
    if (e != NULL) {
        e->IsRunning = false;
        (void)dispatch_post(e);
    }
}

// ---- AGT4 1 kHz tick ISR -------------------------------------------------

static void agt_tick_isr(void *param) {
    (void)param;
    s_tick_ms++;

    TimerEvent_t **pp = (TimerEvent_t **)&s_timer_list;
    while (*pp != NULL) {
        TimerEvent_t *e = *pp;
        int32_t dt_ms = (int32_t)(e->Timestamp - s_tick_ms);

        if (dt_ms <= 0) {
            *pp = e->Next;
            e->Next = NULL;
            e->IsRunning = false;
            (void)dispatch_post(e);
            continue;
        }

        // Precision hand-off: only when AGT5 is free.
        timer_ext_slot_t *ext = ext_lookup(e);
        if (ext != NULL && ext->precision &&
            (uint32_t)dt_ms <= LORAWAN_AGT5_HANDOFF_MS &&
            s_agt5_owner == NULL && s_agt5_ready) {
            uint32_t subtick_us = read_agt4_subtick_us();
            /* Compensate for the sub-ms offset at TimerStart() time:
               fire target (in absolute µs) was arm_real_µs +
               ReloadValue*1000 + fine_us.  By the time we hand off,
               real µs since arm = (s_tick_ms - arm_s_tick) * 1000 +
               (now_subtick - arm_subtick).  Rearranging, the AGT5
               countdown should be dt_ms*1000 + fine_us - now_subtick
               + arm_subtick. */
            int32_t  rem_us = (int32_t)dt_ms * 1000 + ext->fine_us
                            - (int32_t)subtick_us
                            + (int32_t)ext->arm_subtick_us;
            if (rem_us <= 0) {
                *pp = e->Next;
                e->Next = NULL;
                e->IsRunning = false;
                (void)dispatch_post(e);
                continue;
            }
            *pp = e->Next;
            e->Next = NULL;
            agt5_arm(e, (uint32_t)rem_us);
            continue;
        }

        pp = &e->Next;
    }
}

// ---- Service lifecycle ---------------------------------------------------

bool timer_board_is_initialized(void) {
    return s_initialized;
}

void TimerInternalInit(void) {
    s_tick_ms = 0;
    s_timer_list = NULL;
    s_agt5_owner = NULL;
    memset(s_ext, 0, sizeof(s_ext));
    for (size_t i = 0; i < LORAWAN_TIMER_POOL_SIZE; ++i) {
        s_dispatch_pool[i].in_use = false;
        s_dispatch_pool[i].evt = NULL;
    }
}

void timer_board_init(void) {
    if (s_initialized) {
        return;
    }

    if (!ra_agt_timer_reserve(LORAWAN_AGT_TICK_CH)) {
        mp_raise_OSError(MP_EBUSY);
    }
    s_agt5_ready = ra_agt_timer_reserve(LORAWAN_AGT_TIMEOUT_CH);

    TimerInternalInit();

    ra_agt_timer_init(LORAWAN_AGT_TICK_CH, 1000.0f);
    ra_agt_timer_set_mode(LORAWAN_AGT_TICK_CH, RA_AGT_TIMER_MODE_PERIODIC);
    ra_agt_timer_set_callback(LORAWAN_AGT_TICK_CH, agt_tick_isr, NULL);
    s_agt4_period = ra_agt_timer_get_period(LORAWAN_AGT_TICK_CH);
    ra_agt_timer_start(LORAWAN_AGT_TICK_CH);

    if (s_agt5_ready) {
        /* Init at 100 Hz forces PCLKB/8 clock source per
           ra_agt_timer_set_freq (line 771-777): freq ≥ 1000 picks
           PCLKB/2, freq 77..999 picks PCLKB/8. We need PCLKB/8 so the
           16-bit period_counts can cover the 5 ms handoff threshold:
             PCLKB/2 = 25 MHz → 16-bit max = 2.62 ms (TOO SHORT)
             PCLKB/8 = 6.25 MHz → 16-bit max = 10.49 ms (fits 5 ms × 2 margin)
           ticks_per_ms = init_period_counts / (1000 / init_freq)
                        = period / 10 for init at 100 Hz. */
        ra_agt_timer_init(LORAWAN_AGT_TIMEOUT_CH, 100.0f);
        ra_agt_timer_set_mode(LORAWAN_AGT_TIMEOUT_CH,
            RA_AGT_TIMER_MODE_ONE_SHOT);
        ra_agt_timer_set_callback(LORAWAN_AGT_TIMEOUT_CH,
            agt5_oneshot_isr, NULL);
        uint32_t init_period =
            ra_agt_timer_get_period(LORAWAN_AGT_TIMEOUT_CH);
        s_agt5_period_per_ms = init_period / 10u;
    }

    s_initialized = true;
}

void timer_board_deinit(void) {
    if (!s_initialized) {
        return;
    }
    ra_agt_timer_stop(LORAWAN_AGT_TICK_CH);
    ra_agt_timer_deinit(LORAWAN_AGT_TICK_CH);
    ra_agt_timer_release_reservation(LORAWAN_AGT_TICK_CH);
    if (s_agt5_ready) {
        ra_agt_timer_stop(LORAWAN_AGT_TIMEOUT_CH);
        ra_agt_timer_deinit(LORAWAN_AGT_TIMEOUT_CH);
        ra_agt_timer_release_reservation(LORAWAN_AGT_TIMEOUT_CH);
        s_agt5_ready = false;
    }
    s_initialized = false;
    s_timer_list = NULL;
    s_agt5_owner = NULL;
}

// ---- LoRaMac-node Timer API (canonical upstream signature) --------------

void TimerInit(TimerEvent_t *obj, void (*callback)(void)) {
    if (obj == NULL) {
        return;
    }
    obj->Timestamp = 0;
    obj->ReloadValue = 0;
    obj->IsRunning = false;
    obj->Callback = callback;
    obj->Next = NULL;
    // Reset any sidecar slot for this object.
    timer_ext_slot_t *ext = ext_lookup(obj);
    if (ext != NULL) {
        ext->fine_us = 0;
        ext->precision = false;
    }
}

void TimerSetValue(TimerEvent_t *obj, uint32_t value_ms) {
    if (obj != NULL) {
        obj->ReloadValue = value_ms;
    }
}

void TimerStart(TimerEvent_t *obj) {
    if (obj == NULL || !s_initialized) {
        return;
    }
    mp_uint_t state = enter_critical();
    if (obj->IsRunning) {
        // Re-arm — drop from list before re-inserting.
        TimerEvent_t **pp = (TimerEvent_t **)&s_timer_list;
        while (*pp != NULL && *pp != obj) {
            pp = &(*pp)->Next;
        }
        if (*pp == obj) {
            *pp = obj->Next;
        }
        obj->Next = NULL;
        if (s_agt5_owner == obj) {
            ra_agt_timer_stop(LORAWAN_AGT_TIMEOUT_CH);
            s_agt5_owner = NULL;
        }
    }
    obj->Timestamp = s_tick_ms + obj->ReloadValue;
    /* Capture the AGT4 sub-tick µs offset at arm time so the AGT5
       handoff path can compensate. Without this, the integer-ms
       Timestamp loses precision and the firing is up to 1 ms early. */
    timer_ext_slot_t *ext = ext_lookup(obj);
    if (ext != NULL) {
        ext->arm_subtick_us = (uint16_t)read_agt4_subtick_us();
    }
    obj->Next = (TimerEvent_t *)s_timer_list;
    s_timer_list = obj;
    obj->IsRunning = true;
    leave_critical(state);
}

void TimerStop(TimerEvent_t *obj) {
    if (obj == NULL) {
        return;
    }
    mp_uint_t state = enter_critical();
    if (obj->IsRunning) {
        if (s_agt5_ready && s_agt5_owner == obj) {
            ra_agt_timer_stop(LORAWAN_AGT_TIMEOUT_CH);
            s_agt5_owner = NULL;
        } else {
            TimerEvent_t **pp = (TimerEvent_t **)&s_timer_list;
            while (*pp != NULL && *pp != obj) {
                pp = &(*pp)->Next;
            }
            if (*pp == obj) {
                *pp = obj->Next;
            }
            obj->Next = NULL;
        }
        obj->IsRunning = false;
    }
    leave_critical(state);
}

void TimerReset(TimerEvent_t *obj) {
    TimerStop(obj);
    TimerStart(obj);
}

bool TimerExists(TimerEvent_t *obj) {
    if (obj == NULL || !s_initialized) {
        return false;
    }
    if (s_agt5_owner == obj) {
        return true;
    }
    for (TimerEvent_t *e = (TimerEvent_t *)s_timer_list; e != NULL;
         e = e->Next) {
        if (e == obj) {
            return true;
        }
    }
    return false;
}

TimerTime_t TimerGetCurrentTime(void) {
    return (TimerTime_t)s_tick_ms;
}

TimerTime_t TimerGetElapsedTime(TimerTime_t past) {
    return (TimerTime_t)(s_tick_ms - (uint32_t)past);
}

TimerTime_t TimerGetFutureTime(TimerTime_t eventInFuture) {
    return (TimerTime_t)(s_tick_ms + (uint32_t)eventInFuture);
}

// LoRaMac calls this from the sample app's IRQ entry. We drive the AGT4
// callback path directly, so the public function is a NOP.
void TimerIrqHandler(void) {
    /* not used — AGT4 ISR drives the deadline walk directly */
}

TimerTime_t TimerTempCompensation(TimerTime_t period, float temperature) {
    (void)temperature;
    return period;
}

uint32_t TimerGetClockErrorTime(TimerTime_t elapsedTimeMs, uint8_t clkErrPpm) {
    (void)elapsedTimeMs;
    (void)clkErrPpm;
    return 0;  // Phase 3 v1 — TCXO assumed ideal; tune later if needed.
}

// ---- Renesas-ra extension API -------------------------------------------

void TimerSetPrecision(TimerEvent_t *obj, bool precision, int16_t fine_us) {
    if (obj == NULL) {
        return;
    }
    if (fine_us > 999) {
        fine_us = 999;
    } else if (fine_us < -999) {
        fine_us = -999;
    }
    timer_ext_slot_t *ext = ext_get_or_alloc(obj);
    if (ext != NULL) {
        ext->precision = precision;
        ext->fine_us = fine_us;
    }
}

void DelayMsMcu(uint32_t ms) {
    mp_hal_delay_ms(ms);
}

void DelayMicroseconds(uint32_t us) {
    mp_hal_delay_us(us);
}

uint32_t lorawan_timer_now_ms(void) {
    return s_tick_ms;
}

/* Calendar time accessor used by upstream system/systime.c. We have no
   wall-clock RTC integration, so we return the AGT4-tracked uptime in
   seconds with the sub-second remainder in *milliseconds. LoRaMac uses
   this only as a monotonic reference, not for real calendar dates. */
uint32_t RtcGetCalendarTime(uint16_t *milliseconds) {
    uint32_t now = s_tick_ms;
    if (milliseconds != NULL) {
        *milliseconds = (uint16_t)(now % 1000u);
    }
    return now / 1000u;
}

bool lorawan_timer_oneshot(TimerEvent_t *evt, uint32_t ms,
    lorawan_timer_oneshot_cb_t cb) {
    if (evt == NULL || cb == NULL || !s_initialized) {
        return false;
    }
    TimerInit(evt, cb);
    TimerSetValue(evt, ms);
    TimerStart(evt);
    return true;
}

bool lorawan_timer_oneshot_us(TimerEvent_t *evt, uint32_t us,
    lorawan_timer_oneshot_cb_t cb) {
    if (evt == NULL || cb == NULL || !s_initialized) {
        return false;
    }
    uint32_t ms = us / 1000u;
    int16_t  fine = (int16_t)(us % 1000u);
    TimerInit(evt, cb);
    TimerSetValue(evt, ms);
    TimerSetPrecision(evt, /*precision=*/true, fine);
    TimerStart(evt);
    return true;
}

#endif // MICROPY_HW_LORA_STACK_RENESAS
