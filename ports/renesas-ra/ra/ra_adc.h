/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Renesas Electronics Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef RA_ADC_H_
#define RA_ADC_H_

#include <stdint.h>
#include <stdbool.h>

#if defined(RA4M1) | defined(RA4M2) | defined(RA4W1)
#define ADC_RESOLUTION (14)
#else
#define ADC_RESOLUTION (12)
#endif

enum ADC14_PIN
{
    #if defined(RA4M1)

    AN000 = 0,
    AN001 = 1,
    AN002 = 2,
    AN003 = 3,
    AN004 = 4,
    AN005 = 5,
    AN006 = 6,
    AN007 = 7,
    AN008 = 8,
    AN009 = 9,
    AN010 = 10,
    AN011 = 11,
    AN012 = 12,
    AN013 = 13,
    AN014 = 14,
    AN016 = 16,
    AN017 = 17,
    AN018 = 18,
    AN019 = 19,
    AN020 = 20,
    AN021 = 21,
    AN022 = 22,
    AN023 = 23,
    AN024 = 24,
    AN025 = 25,

    #elif defined(RA4M2)

    AN000 = 0,
    AN001 = 1,
    AN002 = 2,
    AN003 = 3,
    AN004 = 4,
    AN005 = 5,
    AN006 = 6,
    AN007 = 7,
    AN008 = 8,
    AN011 = 11,
    AN012 = 12,
    AN013 = 13,
    AN016 = 16,

    #elif defined(RA4W1)

    AN004 = 4,
    AN005 = 5,
    AN006 = 6,
    AN009 = 9,
    AN010 = 10,
    AN017 = 17,
    AN019 = 19,
    AN020 = 20,

    #elif defined(RA6M1)

    AN000 = 0,
    AN001 = 1,
    AN002 = 2,
    AN003 = 3,
    AN004 = 4,
    AN005 = 5,
    AN006 = 6,
    AN007 = 7,
    AN008 = 8,
    AN009 = 9,
    AN010 = 10,
    AN011 = 11,
    AN012 = 12,
    AN013 = 13,
    AN014 = 14,
    AN015 = 15,
    AN016 = 16,
    AN017 = 17,
    AN018 = 18,
    AN019 = 19,
    AN020 = 20,
    AN021 = 21,
    AN022 = 22,
    AN100 = 32,
    AN101 = 33,
    AN102 = 34,
    AN103 = 35,
    AN104 = 36,
    AN105 = 37,
    AN106 = 38,
    AN107 = 39,
    AN108 = 40,
    AN109 = 41,
    AN110 = 42,
    AN111 = 43,
    AN112 = 44,
    AN113 = 45,
    AN114 = 46,
    AN115 = 47,
    AN116 = 48,
    AN117 = 49,
    AN118 = 50,
    AN119 = 51,
    AN120 = 52,

    #elif defined(RA6M3)
    // Unit 0
    AN000 = 0,
    AN001 = 1,
    AN002 = 2,
    AN003 = 3,
    AN004 = 4,
    AN005 = 5,
    AN006 = 6,
    AN007 = 7,
    AN016 = 16,
    AN017 = 17,
    AN018 = 18,
    AN019 = 19,
    AN020 = 20,
    // Unit 1
    AN100 = 32,
    AN101 = 33,
    AN102 = 34,
    AN103 = 35,
    AN105 = 37,
    AN106 = 38,
    AN107 = 39,
    AN116 = 48,
    AN117 = 49,
    AN118 = 50,
    AN119 = 51,

    #elif defined(RA6M2)
    // Unit 0
    AN000 = 0,
    AN001 = 1,
    AN002 = 2,
    AN003 = 3,
    AN004 = 4,
    AN005 = 5,
    AN006 = 6,
    AN007 = 7,
    AN016 = 16,
    AN017 = 17,
    AN018 = 18,
    AN019 = 19,
    AN020 = 20,
    // Unit 1
    AN100 = 32,
    AN101 = 33,
    AN102 = 34,
    AN105 = 37,
    AN106 = 38,
    AN107 = 39,
    AN116 = 48,
    AN117 = 49,
    AN118 = 50,

    #elif defined(RA6M5)
    // Unit 0
    AN000 = 0,
    AN001 = 1,
    AN002 = 2,
    AN003 = 3,
    AN004 = 4,
    AN005 = 5,
    AN006 = 6,
    AN007 = 7,
    AN008 = 8,
    AN009 = 9,
    AN010 = 10,
    AN012 = 12,
    AN013 = 13,
    // Unit 1
    AN100 = 32,
    AN101 = 33,
    AN102 = 34,
    AN116 = 48,
    AN117 = 49,
    AN118 = 50,
    AN119 = 51,
    AN120 = 52,
    AN121 = 53,
    AN122 = 54,
    AN123 = 55,
    AN124 = 56,
    AN125 = 57,
    AN126 = 58,
    AN127 = 59,
    AN128 = 60,

    #else
    #error "CMSIS MCU Series is not specified."
    #endif
    ADC_TEMP = 29,
    ADC_REF = 30,
    ADC_NON = 255,
};

#if defined(RA4M2)
#define RA_ADC_DEF_RESOLUTION 12
#elif defined(RA4M1) | defined(RA4W1)
#define RA_ADC_DEF_RESOLUTION 14
#else
#define RA_ADC_DEF_RESOLUTION 12
#endif

typedef enum {
    RA_ADC_VREF_AVCC = 0,
    RA_ADC_VREF_EXTERNAL = 1,
    RA_ADC_VREF_INTERNAL = 2,
} ra_adc_vref_t;

bool ra_adc_pin_to_ch(uint32_t pin, uint8_t *ch);
bool ra_adc_ch_to_pin(uint8_t ch, uint32_t *pin);
uint8_t ra_adc_get_channel(uint32_t pin);
// static void ra_adc_module_start(void);
// static void ra_adc_module_stop(void);
void ra_adc_set_pin(uint32_t pin, bool adc_enable);
void ra_adc_enable(uint32_t pin);
void ra_adc_disable(uint32_t pin);
void ra_adc_set_resolution(uint8_t res);
uint8_t ra_adc_get_resolution(void);
bool ra_adc_set_vref(ra_adc_vref_t vref);
ra_adc_vref_t ra_adc_get_vref(void);
uint16_t ra_adc_read_ch(uint8_t ch);
uint16_t ra_adc_read(uint32_t pin);
int16_t ra_adc_read_itemp(void);
float ra_adc_read_ftemp(void);
float ra_adc_read_fref(void);
void ra_adc_all(uint32_t resolution, uint32_t mask);
uint16_t ra_adc_all_read_ch(uint32_t ch);
bool ra_adc_init(void);
bool ra_adc_deinit(void);
__attribute__((weak)) void adc_scan_end_isr(void);

/* ---------------------------------------------------------------------------
 * Programmable Gain Amplifier (PGA)
 *
 * RA6M3 only.  Verified against R01UH0886EJ0120 Rev.1.20, sections 47.2.33 to
 * 47.2.35 and 47.3.12, Table 47.11.  Other RA parts are deliberately not
 * enabled here: their PGA presence has not been checked.
 *
 * The PGA exists on AN000..AN002 (unit 0) and AN100..AN102 (unit 1) - exactly
 * the six channels that also carry the dedicated sample-and-hold circuits.
 *
 * Table 47.11 lists only four valid pin configurations for these six channels.
 * ADC12 conversion is available in three of them, and every one of those three
 * requires PmnPFS.ASEL = 1 (already done by ra_adc_set_pin) TOGETHER WITH a
 * non-initial ADPGACR nibble:
 *
 *   ADPGACR nibble 9h -> amplifier bypassed, ADC12 reads the pin directly
 *   ADPGACR nibble Eh -> the signal passes through the amplifier
 *
 * ASEL = 1 with ADPGACR left at its initial value is not one of the documented
 * configurations, so RA_ADC_PGA_BYPASS has to be selected for these channels
 * even when no gain is wanted.
 *
 * Differential mode uses the real PGAVSS pin as the negative PGA input.  The
 * driver enables analog mode on P003/PGAVSS000 for AN000..AN002 and on
 * P007/PGAVSS100 for AN100..AN102, but it does not create an internal ground.
 * The board must wire that pin to the intended reference externally.
 * ------------------------------------------------------------------------- */

#if defined(RA6M3)

typedef enum {
    RA_ADC_PGA_OFF = 0,       /* ADPGACR nibble 0: pin belongs to the port, ADC12 unavailable */
    RA_ADC_PGA_BYPASS,        /* nibble 9h: ADC12 reads the pin, amplifier powered down       */
    RA_ADC_PGA_SINGLE,        /* nibble Eh: single-ended input through the amplifier          */
    RA_ADC_PGA_DIFFERENTIAL,  /* nibble Eh + ADPGADCR0: differential against PGAVSSn00        */
} ra_adc_pga_mode_t;

/* Single-ended gain codes, ADPGAGS0.PnGAIN[3:0] (47.2.34). */
typedef enum {
    RA_ADC_PGA_GAIN_2_000 = 0x0,
    RA_ADC_PGA_GAIN_2_500 = 0x1,
    RA_ADC_PGA_GAIN_2_667 = 0x2,
    RA_ADC_PGA_GAIN_2_857 = 0x3,
    RA_ADC_PGA_GAIN_3_077 = 0x4,
    RA_ADC_PGA_GAIN_3_333 = 0x5,
    RA_ADC_PGA_GAIN_3_636 = 0x6,
    RA_ADC_PGA_GAIN_4_000 = 0x7,
    RA_ADC_PGA_GAIN_4_444 = 0x8,
    RA_ADC_PGA_GAIN_5_000 = 0x9,
    RA_ADC_PGA_GAIN_5_714 = 0xA,
    RA_ADC_PGA_GAIN_6_667 = 0xB,
    RA_ADC_PGA_GAIN_8_000 = 0xC,
    RA_ADC_PGA_GAIN_10_000 = 0xD,
    RA_ADC_PGA_GAIN_13_333 = 0xE,
} ra_adc_pga_gain_t;

/* Differential gain codes.  The hardware needs a matched pair of
 * ADPGAGS0.PnGAIN and ADPGADCR0.PnDG values; the driver applies both. */
typedef enum {
    RA_ADC_PGA_DIFF_GAIN_1_500 = 0,
    RA_ADC_PGA_DIFF_GAIN_2_333 = 1,
    RA_ADC_PGA_DIFF_GAIN_4_000 = 2,
    RA_ADC_PGA_DIFF_GAIN_5_667 = 3,
} ra_adc_pga_diff_gain_t;

/* True for AN000..AN002 and AN100..AN102 only. */
bool ra_adc_pga_supported_ch(uint8_t ch);
bool ra_adc_pga_supported(uint32_t pin);

/* Configure path and gain in one step.  gain_code is a ra_adc_pga_gain_t for
 * RA_ADC_PGA_SINGLE, a ra_adc_pga_diff_gain_t for RA_ADC_PGA_DIFFERENTIAL and
 * ignored otherwise.  Fails if the channel has no PGA or a scan is running. */
bool ra_adc_pga_config_ch(uint8_t ch, ra_adc_pga_mode_t mode, uint8_t gain_code);
bool ra_adc_pga_config(uint32_t pin, ra_adc_pga_mode_t mode, uint8_t gain_code);

/* Change gain only, keeping the current path.  Fails in OFF or BYPASS. */
bool ra_adc_pga_set_gain_ch(uint8_t ch, uint8_t gain_code);
bool ra_adc_pga_set_gain(uint32_t pin, uint8_t gain_code);

bool ra_adc_pga_get_ch(uint8_t ch, ra_adc_pga_mode_t *mode, uint8_t *gain_code);
bool ra_adc_pga_get(uint32_t pin, ra_adc_pga_mode_t *mode, uint8_t *gain_code);

/* Nominal gain x1000, for scaling and for stats().  1000 in OFF and BYPASS.
 * Nominal only: 60.13 gives a gain error of 1.0 to 2.0 % depending on the
 * setting, so a measured correction is still required for I/Q balance. */
uint32_t ra_adc_pga_gain_milli(ra_adc_pga_mode_t mode, uint8_t gain_code);

#endif /* RA6M3 */

#endif /* RA_ADC_H_ */
