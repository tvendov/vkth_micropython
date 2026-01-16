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
#define CTSU_CFG_DIAG_SUPPORT_ENABLE         (0)
#define CTSU_CFG_TEMP_CORRECTION_SUPPORT     (0)
#define CTSU_CFG_CALIB_RTRIM_SUPPORT         (0)

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
