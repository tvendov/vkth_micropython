/*
 * RA6-family capability tables for the SDR I/Q capture path.  See ra_sdr_caps.h
 * for the rationale and the verification reference (SDR-RA6M5-VERIFY-20260822).
 *
 * The whole body is gated on MICROPY_HW_ENABLE_IQ_ADC so this translation unit is
 * empty unless the SDR feature is compiled in.
 */

#include "py/mpconfig.h"

#if MICROPY_HW_ENABLE_IQ_ADC

#include "hal_data.h"       /* BSP_FEATURE_AGT_MAX_CHANNEL_NUM, IRQn_Type, FSP_INVALID_VECTOR */
#include "ra_gpio.h"        /* P003 / P007 / P014 pin enums               */
#include "ra_adc.h"         /* AN000 / AN100 channel enums                */
#include "ra_iq_adc.h"      /* RA_IQ_ADC_MAX_BLOCK_SAMPLES                */
#include "ra_sdr_caps.h"
#include "vector_data.h"    /* VECTOR_NUMBER_* generated macros           */

/* ------------------------------------------------------------------ */
/* MCU facts - selected by CMSIS_MCU at compile time                    */
/* ------------------------------------------------------------------ */

#if defined(RA6M3)

static const ra_sdr_mcu_facts_t s_mcu_facts = {
    .mcu_name = "RA6M3",

    .has_adc0 = true,
    .has_adc1 = true,
    .has_elc = true,
    .has_dtc = true,
    .has_dmac = true,
    .has_agt = true,
    .has_dac = true,

    .adc_channels_per_unit = 32U,
    .adc0_first_ch = AN000,             /* 0  */
    .adc1_first_ch = AN100,             /* 32 */

    .pga_layout_valid = true,
    .pga_channels_per_unit = 3U,
    .pga_adc0_first_ch = AN000,         /* 0  */
    .pga_adc1_first_ch = AN100,         /* 32 */
    .pgavss0_pin = P003,                /* PGAVSS000 */
    .pgavss1_pin = P007,                /* PGAVSS100 */

    .elc_adc0_slot = 8U,                /* ELC_AD00, r_elc_api.h ELC_PERIPHERAL_ADC0 */
    .elc_adc1_slot = 10U,               /* ELC_AD10, r_elc_api.h ELC_PERIPHERAL_ADC1 */

    .adc_adsstr = 0x0BU,
    .adc_sstsh = 0x18U,

    .max_block_samples = RA_IQ_ADC_MAX_BLOCK_SAMPLES,
};

#elif defined(RA6M5)

/* RA6M5 shares the ADC12 / S&H / PGA / ELC register model with RA6M3.  The ADC
 * unit layout, ELC slots and sampling-time reset values are identical (verified,
 * SDR-RA6M5-VERIFY-20260822).  The PGA channel/pin table has NOT yet been
 * verified against the RA6M5 pinout, so pga_layout_valid is left false until it
 * is; callers must treat the PGA facts as provisional while that flag is false. */
static const ra_sdr_mcu_facts_t s_mcu_facts = {
    .mcu_name = "RA6M5",

    .has_adc0 = true,
    .has_adc1 = true,
    .has_elc = true,
    .has_dtc = true,
    .has_dmac = true,
    .has_agt = true,
    .has_dac = true,

    .adc_channels_per_unit = 32U,
    .adc0_first_ch = AN000,             /* 0  */
    .adc1_first_ch = AN100,             /* 32 */

    .pga_layout_valid = false,          /* RA6M5 PGA table not yet verified */
    .pga_channels_per_unit = 3U,
    .pga_adc0_first_ch = AN000,         /* 0  */
    .pga_adc1_first_ch = AN100,         /* 32 */
    .pgavss0_pin = P003,
    .pgavss1_pin = P007,

    .elc_adc0_slot = 8U,
    .elc_adc1_slot = 10U,

    .adc_adsstr = 0x0BU,
    .adc_sstsh = 0x18U,

    .max_block_samples = RA_IQ_ADC_MAX_BLOCK_SAMPLES,
};

#else

#error "ra_sdr_caps: unsupported MCU for the SDR I/Q path (expected RA6M3 or RA6M5)"

#endif

/* ------------------------------------------------------------------ */
/* Board facts - built from the generated vector table                  */
/* ------------------------------------------------------------------ */

static const ra_sdr_board_facts_t s_board_facts = {
    #if defined(VECTOR_NUMBER_ADC0_SCAN_END)
    .adc0_scan_end_irq = VECTOR_NUMBER_ADC0_SCAN_END,
    #else
    .adc0_scan_end_irq = FSP_INVALID_VECTOR,
    #endif

    #if defined(VECTOR_NUMBER_ADC1_SCAN_END)
    .adc1_scan_end_irq = VECTOR_NUMBER_ADC1_SCAN_END,
    .has_adc1_scan_end_vector = true,
    #else
    .adc1_scan_end_irq = FSP_INVALID_VECTOR,
    .has_adc1_scan_end_vector = false,
    #endif

    #if defined(VECTOR_NUMBER_DMAC0_INT)
    .has_dmac_dac_vector = true,
    .dmac_dac_irq = VECTOR_NUMBER_DMAC0_INT,
    #else
    .has_dmac_dac_vector = false,
    .dmac_dac_irq = FSP_INVALID_VECTOR,
    #endif

    .default_dac_pin = P014,            /* DA0 on VK_RA6M3 */
    .default_dac_ch = 0U,

    .agt_channel_count = (uint8_t)(BSP_FEATURE_AGT_MAX_CHANNEL_NUM + 1U),
};

/* ------------------------------------------------------------------ */

static const ra_sdr_caps_t s_caps = {
    .mcu = &s_mcu_facts,
    .board = &s_board_facts,
};

const ra_sdr_caps_t *ra_sdr_caps_get(void) {
    return &s_caps;
}

#endif /* MICROPY_HW_ENABLE_IQ_ADC */
