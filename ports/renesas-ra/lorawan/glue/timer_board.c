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
#include "glue/lorawan_stats.h"
#include "glue/lorawan_pump.h"
#include "mac/LoRaMac.h"

/* Direct DWT->CYCCNT access — mirrors lorawan_pump.c convention so we
   don't pull a CMSIS header into the timer TU. DWT init is owned by
   lorawan_stats_dwt_init() in mod_lorawan.c (called from the lorawan.Mac
   constructor before timer_board_init), so the counter is guaranteed
   to be ticking by the time TimerStart() can run. */
#define LORAWAN_DWT_CYCCNT_ADDR  (0xE0001004u)
#define LORAWAN_DWT_CYCCNT       (*(volatile uint32_t *)LORAWAN_DWT_CYCCNT_ADDR)

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

/* Phase 6 — Strategy B (TimerInit-intercept) callback capture.
 *
 * Why Strategy B: OnRxWindow1TimerEvent is declared `static` in
 * mac/LoRaMac.c (L516 / L2182). Strategy A (`extern void
 * OnRxWindow1TimerEvent(void);`) cannot link against a static
 * upstream symbol, and Phase 6 forbids touching mac/. So we capture
 * the callback pointer by ordinal at TimerInit() time.
 *
 * Init ordering contract: LoRaMacInitialization (mac/LoRaMac.c L4417-
 * 4420) issues four TimerInit calls in fixed source order before the
 * radio is brought up:
 *   ordinal 0 : OnTxDelayedTimerEvent
 *   ordinal 1 : OnRxWindow1TimerEvent  <-- our target
 *   ordinal 2 : OnRxWindow2TimerEvent
 *   ordinal 3 : OnAckTimeoutTimerEvent
 * Trade-off: robust to upstream symbol renames; fragile to upstream
 * MAC init reordering. If that order ever changes, the histogram will
 * silently stamp the wrong timer — guarded by Phase 8's t1 consumer
 * landing on the SetRx opcode, which will refuse to record a sample if
 * the captured pointer doesn't actually fire RX1.
 *
 * State reset: s_rx1_cb_capture_count is zeroed in timer_board_init()
 * before the LoRaMac init runs, so a soft-reset / re-init lifecycle
 * picks up a fresh capture each time. */
static void (*s_rx_window1_callback)(void);
static uint8_t s_rx1_cb_capture_count;

/* Phase 6 instrumentation (p6-instrument-001) — TimerInit ordinal log.
   Populated inside TimerInit() in source-order of every non-NULL callback,
   capped at 8 entries. QA compares these against objdump addresses of the
   upstream OnRxWindow{1,2}TimerEvent / OnAckTimeoutTimerEvent /
   OnTxDelayedTimerEvent symbols to falsify H1 (ordinal-1 = RX1 heuristic).
   Cleared in TimerInternalInit() so a soft-reset / re-init picks up a
   fresh capture. */
static void  *s_timer_init_ordinals[8];
static uint8_t s_timer_init_ordinal_idx;

/* Phase 6 P3 probe (p6-instrument-002). Pointers passed to TimerStart()
   in the order TimerStart is called. Wraps at 8. Used to compare against
   the OnRxWindow1TimerEvent runtime address (0x40989 with Thumb bit) to
   decide whether RX1 is ever armed. */
static uint32_t s_timerstart_callback_log[8];
static volatile uint32_t s_timerstart_log_head;

/* Phase 6 P3 probe — pointers invoked inside timer_dispatch_cb's
   matched-callback walk, in order. Wraps at 8. Discriminates H5
   (subscribe-hook compare broken) vs H6 (MAC never reaches RX1
   TimerStart due to LoRaMacProcess starvation). */
static uint32_t s_timer_dispatch_callback_log[8];
static volatile uint32_t s_timer_dispatch_log_head;

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
//
// S-1 (direct ISR-context callback) reverted: OnAckTimeoutTimerEvent calls
// TimerStop() which mutates s_timer_list — UNSAFE from inside agt_tick_isr's
// list walk. The ISR-safety assumption (callbacks are flag-only) doesn't
// hold for the full timer set. Falling back to deferred-dispatch model: ISR
// schedules an mp_sched_node which runs the callback in scheduler context
// (TimerStop reentrancy is then safe — protected by critical sections).

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
        /* Phase 6 P3 (p6-instrument-002) — log the callback pointer
           being dispatched. Pairs with s_timerstart_callback_log so
           QA can verify which timers actually fire vs which got armed. */
        s_timer_dispatch_callback_log[s_timer_dispatch_log_head & 7] =
            (uint32_t)(uintptr_t)evt->Callback;
        s_timer_dispatch_log_head++;
        /* Phase 8 t0 RELOCATE — stamp t0 + arm s_rx_window_active at the
           callback-entry moment, not at TimerStart. This is the moment
           the RX1 timer FIRED, not when it was armed. The dispatch
           responsiveness from this point to SetRx 0x82 is what the
           rx1_arm_to_setrx_us metric measures (HARD gate p99 ≤ 2000 µs).
           Stamping at TimerStart yielded ~RX1_DELAY (~5 s for join) per
           sample — useless for the gate. Helper sets s_rx_window_active=1u
           inside itself, so the NVM-deferral flag also moves to this
           site (single provenance: callback-entry only). */
        if (evt->Callback == s_rx_window1_callback) {
            lorawan_pump_stamp_rx1_arm_t0(LORAWAN_DWT_CYCCNT);
        }
        /* Phase 6 — TIMER reason pump request. The AGT4/AGT5 ISR posts
           this trampoline into mp_sched (see dispatch_post()); we run
           in scheduler-tail context here, NOT hard ISR. The callback
           we are about to invoke is OnRx{Window1,Window2}TimerEvent /
           OnTxDelayedTimerEvent / OnAckTimeoutTimerEvent — each sets a
           MAC-internal flag that LoRaMacProcess() must drain. Option
           (i) — request BEFORE invoking the callback — keeps the pump
           edge anchored to the timer-fire moment regardless of how
           much wall time the callback itself takes. Option (ii) (after
           the callback) was rejected: it conflates timer-fire latency
           with callback-body latency in the dispatch histogram, and
           re-requests on every callback bodily traversing the deadline
           list, which is more invasive. */
        lorawan_driver_request_pump(LORAWAN_PUMP_REASON_TIMER);
        evt->Callback();
    }
}

static bool dispatch_post(TimerEvent_t *evt) {
    for (size_t i = 0; i < LORAWAN_TIMER_POOL_SIZE; ++i) {
        if (!s_dispatch_pool[i].in_use) {
            s_dispatch_pool[i].in_use = true;
            s_dispatch_pool[i].evt = evt;
            STATS_INC(hard_isr_queue_push_count);
            return mp_sched_schedule_node(&s_dispatch_pool[i].node,
                timer_dispatch_cb);
        }
    }
    /* All 8 slots busy — IRQ event is dropped silently per the original
       contract. Count it so QA can detect pool starvation.
       Invariant: hard_isr_queue_push_count + hard_isr_queue_overflow_count
       == total dispatch_post calls. */
    STATS_INC(hard_isr_queue_overflow_count);
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

/* ISR re-entry depth counters — written only by the matching ISR. A
   non-zero reentry count means a higher-priority IRQ pre-empted this ISR
   while it was mid-execution. We still run the body on re-entry (do NOT
   drop the IRQ).

   Save-restore over a uint8_t: each level enters with prev = depth and
   restores to prev on exit, so an inner level completing cannot clear
   the outer level's depth (the bug the bool sentinel had under deep
   nesting). Same-NVIC-line ISRs tail-chain on ARMv8-M and cross-line
   same-priority preemption is impossible, so plain volatile RMW is
   race-free here. */
static volatile uint8_t s_agt5_isr_depth;
static volatile uint8_t s_agt4_isr_depth;

static void agt5_oneshot_isr(void *param) {
    (void)param;
    uint8_t prev = s_agt5_isr_depth;
    if (prev > 0) {
        STATS_INC(hard_isr_agt5_reentry_count);
    }
    s_agt5_isr_depth = (uint8_t)(prev + 1);
    STATS_INC(hard_isr_agt5_count);
    LORAWAN_ISR_CYCLES_BEGIN();

    TimerEvent_t *e = (TimerEvent_t *)s_agt5_owner;
    s_agt5_owner = NULL;
    if (e != NULL) {
        e->IsRunning = false;
        (void)dispatch_post(e);
    }

    LORAWAN_ISR_CYCLES_END(hard_isr_agt5_cycles_max);
    s_agt5_isr_depth = prev;
}

// ---- AGT4 1 kHz tick ISR -------------------------------------------------

static void agt_tick_isr(void *param) {
    (void)param;
    uint8_t prev = s_agt4_isr_depth;
    if (prev > 0) {
        STATS_INC(hard_isr_agt4_reentry_count);
    }
    s_agt4_isr_depth = (uint8_t)(prev + 1);
    STATS_INC(hard_isr_agt4_count);
    LORAWAN_ISR_CYCLES_BEGIN();

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

    LORAWAN_ISR_CYCLES_END(hard_isr_agt4_cycles_max);
    s_agt4_isr_depth = prev;
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
    /* Phase 6 — clear the Strategy B capture so the next
       LoRaMacInitialization pass picks up the fresh RX1 callback
       address at ordinal 1. */
    s_rx_window1_callback = NULL;
    s_rx1_cb_capture_count = 0;
    memset(s_timer_init_ordinals, 0, sizeof(s_timer_init_ordinals));
    s_timer_init_ordinal_idx = 0;
    memset(s_timerstart_callback_log, 0, sizeof(s_timerstart_callback_log));
    s_timerstart_log_head = 0;
    memset(s_timer_dispatch_callback_log, 0, sizeof(s_timer_dispatch_callback_log));
    s_timer_dispatch_log_head = 0;
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
    /* Phase 6 — Strategy B ordinal-1 capture. The first TimerInit call
       in a fresh init pass is TxDelayedTimer; the second is the RX1
       timer (mac/LoRaMac.c L4417-4418). Capture only the first one we
       see at ordinal 1, then stop incrementing so radio.c's later
       TimerInit calls (TxTimeoutTimer, RxTimeoutTimer) cannot displace
       the captured pointer. NULL callbacks are skipped — a no-op
       TimerInit shouldn't shift the ordinal. */
    if (callback != NULL && s_rx_window1_callback == NULL) {
        if (s_rx1_cb_capture_count == 1u) {
            s_rx_window1_callback = callback;
        }
        if (s_rx1_cb_capture_count < 4u) {
            s_rx1_cb_capture_count++;
        }
    }
    /* Phase 6 instrumentation (p6-instrument-001) — log every non-NULL
       TimerInit callback in source-order, capped at 8 slots, for QA
       cross-check against objdump-derived symbol addresses. Independent
       of the ordinal-1 capture above — that one stops once latched; this
       one keeps logging up to the cap so we can see what came after. */
    if (callback != NULL && s_timer_init_ordinal_idx < 8u) {
        s_timer_init_ordinals[s_timer_init_ordinal_idx++] = (void *)callback;
    }
}

uint8_t lorawan_timer_init_log_get(uintptr_t *out, uint8_t cap) {
    if (out == NULL || cap == 0u) {
        return 0u;
    }
    uint8_t n = s_timer_init_ordinal_idx;
    if (n > 8u) {
        n = 8u;
    }
    if (n > cap) {
        n = cap;
    }
    for (uint8_t i = 0; i < n; i++) {
        out[i] = (uintptr_t)s_timer_init_ordinals[i];
    }
    return n;
}

void lorawan_timer_logs_get(uint32_t *out_ts, uint32_t *out_disp) {
    if (out_ts != NULL) {
        for (uint8_t i = 0; i < 8u; i++) {
            out_ts[i] = s_timerstart_callback_log[i];
        }
    }
    if (out_disp != NULL) {
        for (uint8_t i = 0; i < 8u; i++) {
            out_disp[i] = s_timer_dispatch_callback_log[i];
        }
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
    /* Phase 6 P3 (p6-instrument-002) — log every TimerStart callback
       pointer regardless of whether it matches the captured RX1 hook.
       Discriminates H5 vs H6: if RX1 ptr (0x40989 with Thumb bit) ever
       appears here, MAC is reaching TimerStart and the subscribe-hook
       compare below is the bug. */
    s_timerstart_callback_log[s_timerstart_log_head & 7] = (uint32_t)(uintptr_t)obj->Callback;
    s_timerstart_log_head++;
    /* Phase 8 t0 RELOCATE — the rx1_arm_to_setrx_us t0 was previously
       stamped here at TimerStart (timer ARM moment). Master decision A
       (MSG p8-t0-relocate): the HARD gate p99 ≤ 2000 µs measures
       DISPATCH responsiveness from timer EXPIRY to SetRx 0x82, not from
       arm to send — at arm time every sample equals ~RX1_DELAY (~5 s
       for join), unachievable. The stamp now lives at callback-entry
       inside timer_dispatch_cb. The s_rx_window_active=1u side effect
       moves with it (helper sets both atomically). */
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
