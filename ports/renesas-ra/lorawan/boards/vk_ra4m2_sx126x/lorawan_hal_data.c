/*
 * LoRaWAN AGT FSP HAL data for VK_RA4M2 (Commit 2 of timer-board refactor).
 *
 * Provides g_timer0_ctrl/cfg + g_timer1_ctrl/cfg out-of-tree so the
 * pristine vendor timer-board.c (lorawan/boards/ra2l1ek_sx126x/) can
 * link without touching the port's RASC-generated ra_gen/hal_data.c.
 *
 * Channel mapping (per timer_1to1_vendor_reuse_design_audit.md and
 * the Commit-1 vector_data.c split):
 *
 *   g_timer0 → AGT4 — 16 s free-running underflow ("seconds tick").
 *              IRQ slot 50 (VECTOR_NUMBER_AGT4_INT) → FSP agt_int_isr
 *              dispatches cycle_end_irq → RpMcuFreeRunTimerIntHandler.
 *
 *   g_timer1 → AGT5 — sub-second alarm via Compare-A.
 *              IRQ slot 51 (VECTOR_NUMBER_AGT5_INT, cycle_end) →
 *                  FSP agt_int_isr → RpMcuCompareTimerIntHandler.
 *              IRQ slot 62 (VECTOR_NUMBER_AGT5_COMPARE_A) →
 *                  vendor agt_comp_int_isr (defined in timer-board.c).
 *
 * Clock: SUBCLOCK (SOSC 32.768 kHz crystal) ÷ 8 = 4096 Hz tick,
 * matching the vendor RA2L1EK rate (R_RTC_CONST_THETA / 8). Requires
 * BSP_CLOCK_CFG_SUBCLOCK_POPULATED = 1 (set in
 * boards/VK_RA4M2/ra_cfg/fsp_cfg/bsp/bsp_cfg.h, Commit 1).
 *
 * Period_counts:
 *   timer0 = 0x10000 → 16-bit AGT register = 0xFFFF, full 16 s cycle.
 *   timer1 = 0x0F000 → 16-bit AGT register = 0xEFFF, 15 s cycle.
 *            The 0xEFFF wrap is required by vendor RpMcuCompareTimerSet
 *            math (temp = 0xEFFF marker, see lorawan/boards/ra2l1ek_sx126x/
 *            timer-board.c:438). A larger period would leave a 0x1000-count
 *            dead band where compare targets fall outside the live count
 *            range and never fire.
 *
 * IRQ priorities: 4 — matches RA_PRI_EXTINT (LoRaWAN DIO1 path uses 4).
 */

#include "bsp_api.h"
#include "r_agt.h"

#include "lorawan_vector_aliases.h"  /* AGT0/AGT1 vector names → AGT4/AGT5 */
#include "lorawan_hal_data.h"

/* Forward decls — defined in timer-board.c. */
void RpMcuFreeRunTimerIntHandler(timer_callback_args_t *p_args);
void RpMcuCompareTimerIntHandler(timer_callback_args_t *p_args);

/* ---- Timer 0 (AGT4) — free-running seconds tick ---- */

agt_instance_ctrl_t g_timer0_ctrl;

static const agt_extended_cfg_t g_timer0_extend = {
    .count_source        = AGT_CLOCK_SUBCLOCK,
    .agtoab_settings_b   = { .agtoa = AGT_PIN_CFG_DISABLED,
                             .agtob = AGT_PIN_CFG_DISABLED },
    .agto                = AGT_PIN_CFG_DISABLED,
    .measurement_mode    = AGT_MEASURE_DISABLED,
    .agtio_filter        = AGT_AGTIO_FILTER_NONE,
    .enable_pin          = AGT_ENABLE_PIN_NOT_USED,
    .trigger_edge        = AGT_TRIGGER_EDGE_RISING,
};

const timer_cfg_t g_timer0_cfg = {
    .mode              = TIMER_MODE_PERIODIC,
    .period_counts     = 0x10000U,              /* AGT=0xFFFF, 16 s @4096 Hz */
    .source_div        = TIMER_SOURCE_DIV_8,
    .duty_cycle_counts = 0,
    .channel           = 4U,                    /* AGT4 */
    .cycle_end_ipl     = 4U,
    .cycle_end_irq     = VECTOR_NUMBER_AGT0_INT,/* aliased → AGT4_INT */
    .p_callback        = RpMcuFreeRunTimerIntHandler,
    .p_context         = NULL,
    .p_extend          = &g_timer0_extend,
};

/* ---- Timer 1 (AGT5) — sub-second compare-A alarm ---- */

agt_instance_ctrl_t g_timer1_ctrl;

static const agt_extended_cfg_t g_timer1_extend = {
    .count_source        = AGT_CLOCK_SUBCLOCK,
    .agtoab_settings_b   = { .agtoa = AGT_PIN_CFG_DISABLED,
                             .agtob = AGT_PIN_CFG_DISABLED },
    .agto                = AGT_PIN_CFG_DISABLED,
    .measurement_mode    = AGT_MEASURE_DISABLED,
    .agtio_filter        = AGT_AGTIO_FILTER_NONE,
    .enable_pin          = AGT_ENABLE_PIN_NOT_USED,
    .trigger_edge        = AGT_TRIGGER_EDGE_RISING,
};

const timer_cfg_t g_timer1_cfg = {
    .mode              = TIMER_MODE_PERIODIC,
    .period_counts     = 0x0F000U,              /* AGT=0xEFFF, 15 s @4096 Hz */
    .source_div        = TIMER_SOURCE_DIV_8,
    .duty_cycle_counts = 0,
    .channel           = 5U,                    /* AGT5 */
    .cycle_end_ipl     = 4U,
    .cycle_end_irq     = VECTOR_NUMBER_AGT1_INT,/* aliased → AGT5_INT */
    .p_callback        = RpMcuCompareTimerIntHandler,
    .p_context         = NULL,
    .p_extend          = &g_timer1_extend,
};
