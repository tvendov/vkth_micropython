#ifndef R_CTSU_CFG_H_
#define R_CTSU_CFG_H_
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal CTSU (Capacitive Touch Sensing Unit) driver configuration.
 *
 * The renesas-ra port currently ships CTSU support as a scaffold; most boards
 * do not open/use the CTSU driver yet.  However the FSP r_ctsu module is built
 * as part of the port, and requires this configuration header to compile.
 *
 * Boards that actually use CTSU should tune these values (especially
 * CTSU_CFG_TSCAP_PORT and element counts) based on their hardware.
 */

/* Parameter checking follows the BSP setting. */
#define CTSU_CFG_PARAM_CHECKING_ENABLE       (BSP_CFG_PARAM_CHECKING_ENABLE)

/* Interrupt priority (0 = highest). Keep relatively low priority by default. */
#define CTSU_CFG_INT_PRIORITY_LEVEL          (3)

/* Disable optional features not currently used by this port. */
#define CTSU_CFG_DTC_SUPPORT_ENABLE          (0)
#define CTSU_CFG_DIAG_SUPPORT_ENABLE         (1)
#define CTSU_CFG_TEMP_CORRECTION_SUPPORT     (0)
#define CTSU_CFG_CALIB_RTRIM_SUPPORT         (0)

/*
 * Diagnosis configuration thresholds.
 *
 * When CTSU_CFG_DIAG_SUPPORT_ENABLE is enabled, the FSP CTSU driver expects a
 * set of CTSU_CFG_DIAG_* macros to be defined at compile time. Some boards
 * provide their own r_ctsu_cfg.h; however this port-level config must also be
 * self-contained to keep builds working across all boards.
 *
 * Boards that actively use CTSU diagnosis should override these values.
 */
#ifndef CTSU_CFG_DIAG_DAC_TS
#define CTSU_CFG_DIAG_DAC_TS                 (0)
#endif

#ifndef CTSU_CFG_DIAG_CCO_HIGH_MAX
#define CTSU_CFG_DIAG_CCO_HIGH_MAX           (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_CCO_HIGH_MIN
#define CTSU_CFG_DIAG_CCO_HIGH_MIN           (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_CCO_LOW_MAX
#define CTSU_CFG_DIAG_CCO_LOW_MAX            (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_CCO_LOW_MIN
#define CTSU_CFG_DIAG_CCO_LOW_MIN            (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_SSCG_MAX
#define CTSU_CFG_DIAG_SSCG_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_SSCG_MIN
#define CTSU_CFG_DIAG_SSCG_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_LOAD_REISTER_MAX
#define CTSU_CFG_DIAG_LOAD_REISTER_MAX       (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_LOAD_REISTER_MIN
#define CTSU_CFG_DIAG_LOAD_REISTER_MIN       (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_CURRENT_SOURCE_DIFF_MAX
#define CTSU_CFG_DIAG_CURRENT_SOURCE_DIFF_MAX (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_CURRENT_SOURCE_DIFF_MIN
#define CTSU_CFG_DIAG_CURRENT_SOURCE_DIFF_MIN (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_CLOCK_RECOV_RANGE
#define CTSU_CFG_DIAG_CLOCK_RECOV_RANGE      (0xFFFF)
#endif

/* DAC diagnosis thresholds (safe defaults). */
#ifndef CTSU_CFG_DIAG_DAC1_MAX
#define CTSU_CFG_DIAG_DAC1_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC1_MIN
#define CTSU_CFG_DIAG_DAC1_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC2_MAX
#define CTSU_CFG_DIAG_DAC2_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC2_MIN
#define CTSU_CFG_DIAG_DAC2_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC3_MAX
#define CTSU_CFG_DIAG_DAC3_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC3_MIN
#define CTSU_CFG_DIAG_DAC3_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC4_MAX
#define CTSU_CFG_DIAG_DAC4_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC4_MIN
#define CTSU_CFG_DIAG_DAC4_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC5_MAX
#define CTSU_CFG_DIAG_DAC5_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC5_MIN
#define CTSU_CFG_DIAG_DAC5_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC6_MAX
#define CTSU_CFG_DIAG_DAC6_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC6_MIN
#define CTSU_CFG_DIAG_DAC6_MIN               (0x0000)
#endif

/* CTSU2 uses additional DAC entries and DAC diff thresholds; provide safe defaults too. */
#ifndef CTSU_CFG_DIAG_DAC7_MAX
#define CTSU_CFG_DIAG_DAC7_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC7_MIN
#define CTSU_CFG_DIAG_DAC7_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC8_MAX
#define CTSU_CFG_DIAG_DAC8_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC8_MIN
#define CTSU_CFG_DIAG_DAC8_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC9_MAX
#define CTSU_CFG_DIAG_DAC9_MAX               (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC9_MIN
#define CTSU_CFG_DIAG_DAC9_MIN               (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC10_MAX
#define CTSU_CFG_DIAG_DAC10_MAX              (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC10_MIN
#define CTSU_CFG_DIAG_DAC10_MIN              (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC11_MAX
#define CTSU_CFG_DIAG_DAC11_MAX              (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC11_MIN
#define CTSU_CFG_DIAG_DAC11_MIN              (0x0000)
#endif
#ifndef CTSU_CFG_DIAG_DAC12_MAX
#define CTSU_CFG_DIAG_DAC12_MAX              (0xFFFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC12_MIN
#define CTSU_CFG_DIAG_DAC12_MIN              (0x0000)
#endif

#ifndef CTSU_CFG_DIAG_DAC1_2_DIFF_MAX
#define CTSU_CFG_DIAG_DAC1_2_DIFF_MAX        (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC1_2_DIFF_MIN
#define CTSU_CFG_DIAG_DAC1_2_DIFF_MIN        (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC2_3_DIFF_MAX
#define CTSU_CFG_DIAG_DAC2_3_DIFF_MAX        (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC2_3_DIFF_MIN
#define CTSU_CFG_DIAG_DAC2_3_DIFF_MIN        (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC3_4_DIFF_MAX
#define CTSU_CFG_DIAG_DAC3_4_DIFF_MAX        (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC3_4_DIFF_MIN
#define CTSU_CFG_DIAG_DAC3_4_DIFF_MIN        (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC4_5_DIFF_MAX
#define CTSU_CFG_DIAG_DAC4_5_DIFF_MAX        (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC4_5_DIFF_MIN
#define CTSU_CFG_DIAG_DAC4_5_DIFF_MIN        (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC5_6_DIFF_MAX
#define CTSU_CFG_DIAG_DAC5_6_DIFF_MAX        (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC5_6_DIFF_MIN
#define CTSU_CFG_DIAG_DAC5_6_DIFF_MIN        (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC6_7_DIFF_MAX
#define CTSU_CFG_DIAG_DAC6_7_DIFF_MAX        (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC6_7_DIFF_MIN
#define CTSU_CFG_DIAG_DAC6_7_DIFF_MIN        (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC7_8_DIFF_MAX
#define CTSU_CFG_DIAG_DAC7_8_DIFF_MAX        (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC7_8_DIFF_MIN
#define CTSU_CFG_DIAG_DAC7_8_DIFF_MIN        (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC8_9_DIFF_MAX
#define CTSU_CFG_DIAG_DAC8_9_DIFF_MAX        (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC8_9_DIFF_MIN
#define CTSU_CFG_DIAG_DAC8_9_DIFF_MIN        (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC9_10_DIFF_MAX
#define CTSU_CFG_DIAG_DAC9_10_DIFF_MAX       (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC9_10_DIFF_MIN
#define CTSU_CFG_DIAG_DAC9_10_DIFF_MIN       (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC10_11_DIFF_MAX
#define CTSU_CFG_DIAG_DAC10_11_DIFF_MAX      (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC10_11_DIFF_MIN
#define CTSU_CFG_DIAG_DAC10_11_DIFF_MIN      (-0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC11_12_DIFF_MAX
#define CTSU_CFG_DIAG_DAC11_12_DIFF_MAX      (0x7FFF)
#endif
#ifndef CTSU_CFG_DIAG_DAC11_12_DIFF_MIN
#define CTSU_CFG_DIAG_DAC11_12_DIFF_MIN      (-0x7FFF)
#endif

/* Board/system electrical configuration. */
#define CTSU_CFG_LOW_VOLTAGE_MODE            (0)
#define CTSU_CFG_VCC_MV                      (BSP_CFG_MCU_VCC_MV)

/* CTSU peripheral clock division setting (see RA FSP CTSU user manual). */
#define CTSU_CFG_PCLK_DIVISION               (0)

/*
 * TSCAP pin used for the external low-pass filter capacitor.
 *
 * For RA4M1/RA4M2 evaluation kits this is commonly P205.
 * If your board routes TSCAP elsewhere, override this.
 */
#define CTSU_CFG_TSCAP_PORT                  (BSP_IO_PORT_02_PIN_05)

/* Element counts compiled into the driver (used for static buffer sizing). */
#define CTSU_CFG_NUM_SELF_ELEMENTS           (12)
#define CTSU_CFG_NUM_MUTUAL_ELEMENTS         (0)

/* Multi-frequency support (driver expects macros for 0/1/2). */
#define CTSU_CFG_NUM_SUMULTI                 (1)
#define CTSU_CFG_SUMULTI0                    (0)
#define CTSU_CFG_SUMULTI1                    (0)
#define CTSU_CFG_SUMULTI2                    (0)

/* Correction for CFC scan is not used by default. */
#define CTSU_CFG_NUM_CFC                     (0)

#ifdef __cplusplus
}
#endif
#endif /* R_CTSU_CFG_H_ */
