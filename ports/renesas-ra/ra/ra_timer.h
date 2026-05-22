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

#ifndef RA_RA_TIMER_H_
#define RA_RA_TIMER_H_

#include <stdbool.h>
#include <stdint.h>

void SysTick_Handler(void);
uint32_t HAL_GetTick(void);

#define DEF_CLKDEV    2
#define TENUSEC_COUNT (PCLK / DEF_CLKDEV / 100000)
#define MSEC_COUNT    (PCLK / DEF_CLKDEV / 100)

typedef void (*AGT_TIMER_CB)(void *);
typedef enum {
    RA_AGT_TIMER_MODE_PERIODIC = 0,
    RA_AGT_TIMER_MODE_ONE_SHOT = 1,
} ra_agt_timer_mode_t;
typedef enum {
    RA_AGT_TIMER_IRQ_EVENT_NONE = 0,
    RA_AGT_TIMER_IRQ_EVENT_CYCLE_END = 1,
    RA_AGT_TIMER_IRQ_EVENT_CAPTURE = 2,
    RA_AGT_TIMER_IRQ_EVENT_COMPARE_A = 3,
    RA_AGT_TIMER_IRQ_EVENT_COMPARE_B = 4,
} ra_agt_timer_irq_event_t;
typedef enum {
    RA_AGT_TIMER_CAPTURE_EDGE_RISING = 0,
    RA_AGT_TIMER_CAPTURE_EDGE_FALLING = 1,
    RA_AGT_TIMER_CAPTURE_EDGE_BOTH = 8,
} ra_agt_timer_capture_edge_t;
typedef enum {
    RA_AGT_TIMER_CAPTURE_MEASURE_PERIOD = 0,
    RA_AGT_TIMER_CAPTURE_MEASURE_PULSE_WIDTH_LOW = 1,
    RA_AGT_TIMER_CAPTURE_MEASURE_PULSE_WIDTH_HIGH = 2,
    RA_AGT_TIMER_CAPTURE_MEASURE_EVENT_COUNT = 3,
} ra_agt_timer_capture_measure_t;

/* Low-frequency (<1000 Hz) clock source preference for ra_agt_timer_set_freq_ex().
 * Branches running on PCLKB/2 or PCLKB/8 ignore this — they are always
 * crystal-accurate.
 * DEFAULT = legacy AGTKCLK path (LOCO direct, ±15%).
 * LOCO    = explicit AGTKCLK (TCK=100b). Same as DEFAULT.
 * SOSC    = AGTSCLK (TCK=110b) with LPM=0 — uses external 32.768 kHz crystal
 *           (±20–50 ppm). Requires MICROPY_HW_SUBCLK_POPULATED == 1 at compile
 *           time AND BSP_CLOCK_CFG_SUBCLOCK_POPULATED == 1 so FSP starts SOSC.
 * Note: LPM=1 (which would let AGTSCLK select LOCO on boards without SOSC) is
 *       NOT supported here — it prohibits AGT/AGTCR register access per
 *       RA4M2 §22.2.6 and breaks counter()/ISR. */
typedef enum {
    RA_AGT_CLOCK_DEFAULT = 0,
    RA_AGT_CLOCK_LOCO = 1,
    RA_AGT_CLOCK_SOSC = 2,
} ra_agt_clock_source_t;

bool ra_agt_timer_is_valid(uint32_t ch);
bool ra_agt_timer_reserve(uint32_t ch);
void ra_agt_timer_release_reservation(uint32_t ch);
void ra_agt_timer_clear_all_reservations(void);
bool ra_agt_timer_is_reserved(uint32_t ch);
void ra_agt_timer_set_callback(uint32_t ch, AGT_TIMER_CB cb, void *param);
void ra_agt_int_isr0(void);
void ra_agt_int_isr1(void);
void ra_agt_timer_set_mode(uint32_t ch, ra_agt_timer_mode_t mode);
ra_agt_timer_mode_t ra_agt_timer_get_mode(uint32_t ch);
void ra_agt_timer_start(uint32_t ch);
void ra_agt_timer_stop(uint32_t ch);
void ra_agt_timer_set_freq(uint32_t ch, float freq);
bool ra_agt_timer_set_freq_ex(uint32_t ch, float freq, ra_agt_clock_source_t source);
float ra_agt_timer_get_freq(uint32_t ch);
bool ra_agt_timer_set_period(uint32_t ch, uint32_t period_counts);
uint32_t ra_agt_timer_get_period(uint32_t ch);
bool ra_agt_timer_set_counter(uint32_t ch, uint32_t counter);
uint32_t ra_agt_timer_get_counter(uint32_t ch);
bool ra_agt_timer_is_output_channel(uint32_t ch, uint32_t output);
bool ra_agt_timer_is_input_capture_supported(uint32_t ch);
bool ra_agt_timer_channel_pin_assign(uint32_t ch, uint32_t output, uint32_t pin);
void ra_agt_timer_channel_pin_release(uint32_t ch, uint32_t output);
bool ra_agt_timer_input_pin_assign(uint32_t ch, uint32_t pin, ra_agt_timer_capture_measure_t measure, ra_agt_timer_capture_edge_t edge);
void ra_agt_timer_input_pin_release(uint32_t ch);
bool ra_agt_timer_input_is_configured(uint32_t ch);
bool ra_agt_timer_has_compare(uint32_t ch, uint32_t output);
bool ra_agt_timer_set_compare(uint32_t ch, uint32_t output, uint32_t compare);
uint32_t ra_agt_timer_get_compare(uint32_t ch, uint32_t output);
uint32_t ra_agt_timer_get_capture(uint32_t ch);
ra_agt_timer_irq_event_t ra_agt_timer_get_irq_event(uint32_t ch);
void ra_agt_timer_set_compare_irq(uint32_t ch, uint32_t output, bool enable);
void ra_agt_timer_set_fast_irq(uint32_t ch, bool fast_irq, void *fast_entry, uintptr_t fast_param);
bool ra_agt_timer_get_fast_irq(uint32_t ch);
void *ra_agt_timer_get_fast_entry(uint32_t ch);
uintptr_t ra_agt_timer_get_fast_param(uint32_t ch);
void ra_agt_timer_init(uint32_t ch, float freq);
void ra_agt_timer_deinit(uint32_t ch);
void ra_port_agt_int_isr(void);
uint32_t mtick();

#endif /* RA_RA_TIMER_H_ */
