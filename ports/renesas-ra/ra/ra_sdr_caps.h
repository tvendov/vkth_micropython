/*
 * RA6-family capability layer for the SDR I/Q capture path.
 *
 * The coherent I/Q driver (ra_iq_adc.c) and its Python wrapper (machine_iq_adc.c)
 * were first written RA6M3-only, with the portable facts baked in as literals.
 * This layer hoists those facts into two const tables so the same driver can run
 * on register-compatible members of the RA6 family (RA6M3, RA6M5) without
 * touching the register-level S&H / PGA / DTC / ELC logic.
 *
 *   ra_sdr_mcu_facts_t   - facts that follow the MCU (ADC unit layout, PGA layout,
 *                          ELC output slots, sampling-time reset values).  Selected
 *                          at compile time by the CMSIS_MCU define.
 *   ra_sdr_board_facts_t - facts that follow the board / generated vector table
 *                          (which SCAN_END / DMAC vectors were generated, the
 *                          default DAC pin).  Built from VECTOR_NUMBER_* macros.
 *
 * Values verified against the FSP headers; see SDR-RA6M5-VERIFY-20260822.  ELC
 * event NUMBERS differ between MCUs, so this layer never stores event numbers:
 * the driver keeps using the ELC_EVENT_* enums, which the FSP header resolves per
 * MCU.  Only the ELC output SLOT indices (ELSR8 / ELSR10) live here, and those are
 * identical across RA6M3 and RA6M5 (r_elc_api.h: ELC_PERIPHERAL_ADC0 = 8,
 * ELC_PERIPHERAL_ADC1 = 10).
 */

#ifndef MICROPY_INCLUDED_RENESAS_RA_SDR_CAPS_H
#define MICROPY_INCLUDED_RENESAS_RA_SDR_CAPS_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_api.h"    /* IRQn_Type */

#if MICROPY_HW_ENABLE_IQ_ADC

typedef struct {
    const char *mcu_name;

    bool has_adc0;
    bool has_adc1;
    bool has_elc;
    bool has_dtc;
    bool has_dmac;
    bool has_agt;
    bool has_dac;

    uint8_t adc_channels_per_unit;  /* channels each ADC unit exposes (32)         */
    uint8_t adc0_first_ch;          /* channel enum of unit-0 first channel (0)    */
    uint8_t adc1_first_ch;          /* channel enum of unit-1 first channel (32)   */

    bool pga_layout_valid;          /* PGA channel/pin table verified for this MCU */
    uint8_t pga_channels_per_unit;  /* channels per unit that carry a PGA (3)       */
    uint8_t pga_adc0_first_ch;      /* first PGA channel on unit 0 (AN000 = 0)      */
    uint8_t pga_adc1_first_ch;      /* first PGA channel on unit 1 (AN100 = 32)     */
    uint32_t pgavss0_pin;           /* PGAVSS000 pin enum (P003) for unit 0         */
    uint32_t pgavss1_pin;           /* PGAVSS100 pin enum (P007) for unit 1         */

    uint8_t elc_adc0_slot;          /* ELC ELSR index feeding unit 0 (ELC_AD00 = 8) */
    uint8_t elc_adc1_slot;          /* ELC ELSR index feeding unit 1 (ELC_AD10 = 10)*/

    uint8_t adc_adsstr;             /* per-channel sampling-time reset value (0x0B) */
    uint8_t adc_sstsh;              /* S&H sampling-time reset value (0x18)         */

    uint16_t max_block_samples;     /* RA_IQ_ADC_MAX_BLOCK_SAMPLES                  */
} ra_sdr_mcu_facts_t;

typedef struct {
    IRQn_Type adc0_scan_end_irq;    /* VECTOR_NUMBER_ADC0_SCAN_END: DTC activation  */
    IRQn_Type adc1_scan_end_irq;    /* VECTOR_NUMBER_ADC1_SCAN_END: unit-1 liveness */
    bool has_adc1_scan_end_vector;  /* diagnostic slot was generated                */

    bool has_dmac_dac_vector;       /* a DMAC INT vector was generated for the DAC  */
    IRQn_Type dmac_dac_irq;         /* VECTOR_NUMBER_DMAC0_INT                       */

    uint32_t default_dac_pin;       /* DA0 pin enum (P014 on VK_RA6M3)              */
    uint8_t default_dac_ch;         /* DAC channel behind default_dac_pin           */

    uint8_t agt_channel_count;      /* number of AGT channels available             */
} ra_sdr_board_facts_t;

typedef struct {
    const ra_sdr_mcu_facts_t *mcu;
    const ra_sdr_board_facts_t *board;
} ra_sdr_caps_t;

const ra_sdr_caps_t *ra_sdr_caps_get(void);

#endif /* MICROPY_HW_ENABLE_IQ_ADC */

#endif /* MICROPY_INCLUDED_RENESAS_RA_SDR_CAPS_H */
