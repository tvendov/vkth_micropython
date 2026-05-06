/*
 * lorawan/glue/timer_board.h
 *
 * AGT-backed timer service for the LoRaMac stack on VK_RA4M2.
 *
 * The canonical timer struct (`TimerEvent_t`) and core API
 * (`TimerInit/Start/Stop/SetValue/...`) are declared by the imported
 *   lorawan/system/timer.h
 * and implemented by `glue/timer_board.c`. This header only declares
 * the renesas-ra-specific extensions:
 *   * AGT5 sub-ms hand-off (Phase 3 v1) → `TimerSetPrecision()`
 *   * monotonic ms clock + oneshot test surface (mod_lorawan testing)
 *
 * Phase status:
 *   2 — AGT4 1 kHz tick + dispatch pool (ms accuracy)
 *   3 — + AGT5 ONE_SHOT hand-off in last 5 ms (~50 µs accuracy)
 *
 * Reservations on VK_RA4M2:
 *   AGT4 == LORAWAN_AGT_TICK_CH (== machine.Timer 5 slot)
 *   AGT5 == LORAWAN_AGT_TIMEOUT_CH (== machine.Timer 6 slot)
 */

#ifndef LORAWAN_GLUE_TIMER_BOARD_H
#define LORAWAN_GLUE_TIMER_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Brings in TimerEvent_t and the canonical Timer* API.
#include "system/timer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LORAWAN_AGT_TICK_CH    (4u)   // AGT4 — 1 kHz tick + deadline walk
#define LORAWAN_AGT_TIMEOUT_CH (5u)   // AGT5 — sub-ms ONE_SHOT compare

// ---- Service lifecycle ---------------------------------------------------
//
// Wraps `TimerInternalInit()` plus AGT4/AGT5 reservation + start. Idempotent.
void timer_board_init(void);
void timer_board_deinit(void);
bool timer_board_is_initialized(void);

// ---- Phase 3 v1 — sub-ms precision extension ----------------------------
// Marks `obj` as a precision timer. AGT4's deadline walk hands the
// remaining countdown off to AGT5 ONE_SHOT in the last ~5 ms, achieving
// sub-ms firing accuracy (target ±200 µs). `fine_us` is a signed offset
// in [-999, +999] µs added to the ms-boundary timestamp.
//
// Default for every TimerEvent_t (after `TimerInit`) is precision=false,
// fine_us=0 — i.e., ms-precision firing (Phase 2 behaviour).
void TimerSetPrecision(TimerEvent_t *obj, bool precision, int16_t fine_us);

// ---- Coarse delays ------------------------------------------------------
void DelayMsMcu(uint32_t ms);
void DelayMicroseconds(uint32_t us);

// ---- Testing surface (called from mod_lorawan.c) ------------------------
// Monotonic ms since `timer_board_init()`. Wraps at 32 bits (~49 days).
uint32_t lorawan_timer_now_ms(void);

// One-shot deferred C callback. Caller owns `evt` storage — must
// outlive the firing. Callback signature mirrors upstream
// (no context — closes over static state).
typedef void (*lorawan_timer_oneshot_cb_t)(void);

bool lorawan_timer_oneshot(TimerEvent_t *evt, uint32_t ms,
    lorawan_timer_oneshot_cb_t cb);
bool lorawan_timer_oneshot_us(TimerEvent_t *evt, uint32_t us,
    lorawan_timer_oneshot_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif // LORAWAN_GLUE_TIMER_BOARD_H
