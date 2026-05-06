/*
 * lorawan/glue/timer-board.h
 *
 * Shim header. The imported `radio/radio.c` and `system/systime.c`
 * include "timer-board.h" expecting upstream Renesas/Semtech symbols.
 * On this port the actual implementation lives in `glue/timer_board.c`
 * (built on the AGT4/AGT5 service); this shim re-exports the upstream-
 * convention surface so imported sources compile unmodified.
 *
 * Only the symbols actually called from the imported tree are wired
 * here. RTC-style helpers (`RtcGetTimerValue`, `RtcSetTimeout`, etc.)
 * map onto our `Timer*()` API via macros — same semantics, same units.
 */

#ifndef LORAWAN_GLUE_TIMER_BOARD_SHIM_H
#define LORAWAN_GLUE_TIMER_BOARD_SHIM_H

#include <stdint.h>
#include <stdbool.h>

/* Brings in TimerEvent_t / TimerTime_t / Timer* prototypes. */
#include "system/timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RTC-style aliases used by upstream `radio.c`'s timeout arming.
   Behaviour matches our AGT4 ms tick. */
#define RtcGetTimerValue          TimerGetCurrentTime
#define RtcSetTimeout             TimerSetValue
#define RtcStopTimer              TimerStop
#define RtcGetElapsedAlarmTime    TimerGetElapsedTime

/* Calendar time in seconds since boot. The optional `*milliseconds`
   out-parameter receives the [0..999] sub-second remainder. */
uint32_t RtcGetCalendarTime(uint16_t *milliseconds);

#ifdef __cplusplus
}
#endif

#endif /* LORAWAN_GLUE_TIMER_BOARD_SHIM_H */
