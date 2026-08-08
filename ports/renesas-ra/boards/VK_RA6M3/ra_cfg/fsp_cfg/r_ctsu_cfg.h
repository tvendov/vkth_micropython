/*
 * CTSU configuration for VK_RA6M3.
 */

#ifndef R_CTSU_CFG_H
#define R_CTSU_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#define CTSU_CFG_PARAM_CHECKING_ENABLE      (1)
#define CTSU_CFG_NUM_SELF_ELEMENTS          (18)
#define CTSU_CFG_NUM_MUTUAL_ELEMENTS        (0)
#define CTSU_CFG_NUM_CFC                    (0)

#define CTSU_CFG_NUM_SUMULTI                (1)
#define CTSU_CFG_SUMULTI0                   (0x00)
#define CTSU_CFG_SUMULTI1                   (0x00)
#define CTSU_CFG_SUMULTI2                   (0x00)

/* Both boards run CTSU directly from PCLKB; element SDPA sets the drive rate. */
#define CTSU_CFG_PCLK_DIVISION              (0)
#define CTSU_CFG_LOW_VOLTAGE_MODE           (0)
#define CTSU_CFG_VCC_MV                     (3300)

/* P205 is a TSCAP-capable pin on VK_RA6M3 (R7FA6M3AH3CFC). */
#define CTSU_CFG_TSCAP_PORT                 (BSP_IO_PORT_02_PIN_05)

/* DTC support */
#define CTSU_CFG_DTC_SUPPORT_ENABLE         (0)

/* Diagnosis support */
#define CTSU_CFG_DIAG_SUPPORT_ENABLE        (1)

/* Temperature and calibration support */
#define CTSU_CFG_TEMP_CORRECTION_SUPPORT    (0)
#define CTSU_CFG_CALIB_RTRIM_SUPPORT        (0)

/* Interrupt priority */
#define CTSU_CFG_INT_PRIORITY_LEVEL         (12)

/* Diagnosis configuration required by FSP 4.4.0 */
#define CTSU_CFG_DIAG_DAC_TS                (0)
#define CTSU_CFG_DIAG_CCO_HIGH_MAX          (0xFFFF)
#define CTSU_CFG_DIAG_CCO_HIGH_MIN          (0x0000)
#define CTSU_CFG_DIAG_CCO_LOW_MAX           (0xFFFF)
#define CTSU_CFG_DIAG_CCO_LOW_MIN           (0x0000)
#define CTSU_CFG_DIAG_SSCG_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_SSCG_MIN              (0x0000)
#define CTSU_CFG_DIAG_LOAD_REISTER_MAX      (0xFFFF)
#define CTSU_CFG_DIAG_LOAD_REISTER_MIN      (0x0000)
#define CTSU_CFG_DIAG_CURRENT_SOURCE_DIFF_MAX (0x7FFF)
#define CTSU_CFG_DIAG_CURRENT_SOURCE_DIFF_MIN (-0x7FFF)
#define CTSU_CFG_DIAG_CLOCK_RECOV_RANGE     (0xFFFF)
#define CTSU_CFG_DIAG_DAC1_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC1_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC2_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC2_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC3_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC3_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC4_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC4_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC5_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC5_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC6_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC6_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC7_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC7_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC8_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC8_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC9_MAX              (0xFFFF)
#define CTSU_CFG_DIAG_DAC9_MIN              (0x0000)
#define CTSU_CFG_DIAG_DAC10_MAX             (0xFFFF)
#define CTSU_CFG_DIAG_DAC10_MIN             (0x0000)
#define CTSU_CFG_DIAG_DAC11_MAX             (0xFFFF)
#define CTSU_CFG_DIAG_DAC11_MIN             (0x0000)
#define CTSU_CFG_DIAG_DAC12_MAX             (0xFFFF)
#define CTSU_CFG_DIAG_DAC12_MIN             (0x0000)
#define CTSU_CFG_DIAG_DAC1_2_DIFF_MAX       (0x7FFF)
#define CTSU_CFG_DIAG_DAC1_2_DIFF_MIN       (-0x7FFF)
#define CTSU_CFG_DIAG_DAC2_3_DIFF_MAX       (0x7FFF)
#define CTSU_CFG_DIAG_DAC2_3_DIFF_MIN       (-0x7FFF)
#define CTSU_CFG_DIAG_DAC3_4_DIFF_MAX       (0x7FFF)
#define CTSU_CFG_DIAG_DAC3_4_DIFF_MIN       (-0x7FFF)
#define CTSU_CFG_DIAG_DAC4_5_DIFF_MAX       (0x7FFF)
#define CTSU_CFG_DIAG_DAC4_5_DIFF_MIN       (-0x7FFF)
#define CTSU_CFG_DIAG_DAC5_6_DIFF_MAX       (0x7FFF)
#define CTSU_CFG_DIAG_DAC5_6_DIFF_MIN       (-0x7FFF)
#define CTSU_CFG_DIAG_DAC6_7_DIFF_MAX       (0x7FFF)
#define CTSU_CFG_DIAG_DAC6_7_DIFF_MIN       (-0x7FFF)
#define CTSU_CFG_DIAG_DAC7_8_DIFF_MAX       (0x7FFF)
#define CTSU_CFG_DIAG_DAC7_8_DIFF_MIN       (-0x7FFF)
#define CTSU_CFG_DIAG_DAC8_9_DIFF_MAX       (0x7FFF)
#define CTSU_CFG_DIAG_DAC8_9_DIFF_MIN       (-0x7FFF)
#define CTSU_CFG_DIAG_DAC9_10_DIFF_MAX      (0x7FFF)
#define CTSU_CFG_DIAG_DAC9_10_DIFF_MIN      (-0x7FFF)
#define CTSU_CFG_DIAG_DAC10_11_DIFF_MAX     (0x7FFF)
#define CTSU_CFG_DIAG_DAC10_11_DIFF_MIN     (-0x7FFF)
#define CTSU_CFG_DIAG_DAC11_12_DIFF_MAX     (0x7FFF)
#define CTSU_CFG_DIAG_DAC11_12_DIFF_MIN     (-0x7FFF)

#ifdef __cplusplus
}
#endif

#endif /* R_CTSU_CFG_H */
