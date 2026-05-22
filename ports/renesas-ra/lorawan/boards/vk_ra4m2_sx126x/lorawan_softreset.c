/*
 * LoRaWAN AGT release + Python-facing timer adapter shims (VK_RA4M2,
 * Commit 2 of timer-board refactor).
 *
 * Two integration shims that bridge mod_lorawan.c (the Python binding)
 * to the pristine vendor timer-board.c (lorawan/boards/ra2l1ek_sx126x/):
 *
 *   timer_board_init / timer_board_deinit  — referenced by mod_lorawan.c.
 *                                            Delegate to BoardTimerInit
 *                                            (vendor) and re-release AGT
 *                                            on de-init.
 *   lorawan_softreset_agt_release           — called from main.c soft-reset
 *                                            path before mp_init, to flush
 *                                            a half-open AGT state from
 *                                            the previous VM cycle.
 *
 * Vendor timer-board.c has no de-init path of its own. After a Python
 * `machine.soft_reset()` the AGT control blocks still claim `.open != 0`,
 * so the next R_AGT_Open inside BoardTimerInit returns
 * FSP_ERR_ALREADY_OPEN and the LoRaWAN timer chain silently never runs.
 * R_AGT_Close also disables the cycle-end IRQ at the NVIC and stops the
 * counter, undoing RpMcuResourceTimerStart's side effects.
 *
 * The two AGT channels are reserved at compile time via
 * MICROPY_HW_AGT_RESERVED_MASK (mpconfigboard.h) — that gate runs in
 * ra_agt_timer_reserve() and keeps machine.Timer() callers out. No
 * runtime port-side reserve call is needed from this shim.
 */

#include "bsp_api.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "r_agt.h"

#include "lorawan_hal_data.h"

void BoardTimerInit(void);

void lorawan_softreset_agt_release(void) {
    if (g_timer1_ctrl.open != 0U) {
        R_AGT_Close((timer_ctrl_t *)&g_timer1_ctrl);
    }
    if (g_timer0_ctrl.open != 0U) {
        R_AGT_Close((timer_ctrl_t *)&g_timer0_ctrl);
    }
    /* R_AGT_Close clears the `open` flag, but be explicit in case vendor
     * code is mid-flight when soft-reset fires. */
    g_timer0_ctrl.open = 0U;
    g_timer1_ctrl.open = 0U;
}

void timer_board_init(mp_obj_t timer0, mp_obj_t timer1) {
    /* The Python-side machine.Timer objects exist only to (a) keep
     * pin/channel reservation visible at the API surface and (b) anchor
     * GC roots; the actual hardware open is done by the vendor stack
     * against AGT4/AGT5 (fixed by lorawan_hal_data.c). Accept any object
     * here and pass it through as a root pointer in mod_lorawan.c. */
    (void)timer0;
    (void)timer1;
    lorawan_softreset_agt_release();
    BoardTimerInit();
}

void timer_board_deinit(void) {
    lorawan_softreset_agt_release();
}
