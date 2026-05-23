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

void timer_board_init(void) {
    /* AGT4/AGT5 are boot-reserved via MICROPY_HW_AGT_RESERVED_MASK so the
     * vendor stack owns the hardware directly. Release any half-open state
     * from a prior VM cycle, then let BoardTimerInit re-open both channels. */
    lorawan_softreset_agt_release();
    BoardTimerInit();

    /* HIL diagnostic + workaround for TSTART=0 issue (2026-05-23, master
     * authorized). BoardTimerInit returns with AGTCR=0x00 despite both
     * R_AGT_Open succeeding (.open == AGT_OPEN). R_AGT_Start in
     * RpMcuResourceTimerStart either never runs or its TSTART write is
     * reverted. Manual R_AGT_Start fallback unblocks AGT counter. */
    uint8_t cr0_pre = g_timer0_ctrl.p_reg->AGT16.CTRL.AGTCR;
    uint8_t cr1_pre = g_timer1_ctrl.p_reg->AGT16.CTRL.AGTCR;
    if ((cr0_pre & 0x01U) == 0U) {
        R_AGT_Start((timer_ctrl_t *)&g_timer0_ctrl);
    }
    if ((cr1_pre & 0x01U) == 0U) {
        R_AGT_Start((timer_ctrl_t *)&g_timer1_ctrl);
    }
    uint8_t cr0_post = g_timer0_ctrl.p_reg->AGT16.CTRL.AGTCR;
    uint8_t cr1_post = g_timer1_ctrl.p_reg->AGT16.CTRL.AGTCR;
    mp_printf(&mp_plat_print,
              "timer_board_init: AGTCR t0 pre=0x%02x post=0x%02x  t1 pre=0x%02x post=0x%02x\n",
              cr0_pre, cr0_post, cr1_pre, cr1_post);
}

void timer_board_deinit(void) {
    lorawan_softreset_agt_release();
}
