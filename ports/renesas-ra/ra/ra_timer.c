/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021-2023 Renesas Electronics Corporation
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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "hal_data.h"
#include "ra_config.h"
#include "mpconfigboard.h"  // board-specific: MICROPY_HW_RTC_SOURCE etc. (-I$(BOARD_DIR) in CFLAGS)

// AGTMR2 bit 7 (LPM): selects AGTSCLK source for AGT sub-clock path.
// 0 = SOSC — external 32.768 kHz sub-clock crystal
// 1 = LOCO — internal 32.768 kHz oscillator
// LPM=1 prohibits access to AGT/AGTCR registers (RA4M2 §22.2.6), which would
// break counter()/ISR. This port therefore keeps LPM=0 unconditionally.
// Low-frequency timer branches default to AGTKCLK (TCK=100b) = LOCO direct,
// which does not need LPM. SOSC is reached via AGTSCLK (TCK=110b, LPM=0) and
// is selected at runtime through ra_agt_timer_set_freq_ex() — see ra_timer.h
// ra_agt_clock_source_t.
#define AGT_AGTMR2_LPM_BIT (0x00U)
#include "ra_gpio.h"
#include "ra_int.h"
#include "ra_utils.h"
#include "ra_timer.h"

#if defined(RA4M2)
#define AGT_CH_SIZE 6
#else
#define AGT_CH_SIZE 2
#endif

#define AGT_MAX_PERIOD_16BIT          (UINT16_MAX + 1U)
#define AGT_SOURCE_CLOCK_PCLKB_BITS   (0x3U)
#define AGT_OUTPUT_CHANNELS           (2U)
#define AGT_COMPARE_MATCH_A_ENABLE    (R_AGTX0_AGT16_CTRL_AGTCMSR_TCMEA_Msk)
#define AGT_COMPARE_MATCH_A_OUTPUT    (R_AGTX0_AGT16_CTRL_AGTCMSR_TOEA_Msk)
#define AGT_COMPARE_MATCH_B_ENABLE    (R_AGTX0_AGT16_CTRL_AGTCMSR_TCMEB_Msk)
#define AGT_COMPARE_MATCH_B_OUTPUT    (R_AGTX0_AGT16_CTRL_AGTCMSR_TOEB_Msk)
#define AGT_MODE_TIMER                (0x0U)
#define AGT_MODE_EVENT_COUNTER        (0x2U)
#define AGT_MODE_PULSE_WIDTH          (0x3U)
#define AGT_MODE_PULSE_PERIOD         (0x4U)

enum AGT_SOURCE {
    AGT_PCLKB = 0,
    AGT_PCLKB8,
    AGT_PCLKB2 = 3,
    AGT_AGTKCLK,
    AGT_AGT0UNDER,
    AGT_AGTSCLK
};

static R_AGTX0_AGT16_Type *const agt_regs[AGT_CH_SIZE] = {
    (R_AGTX0_AGT16_Type *)R_AGTX0,
    (R_AGTX0_AGT16_Type *)R_AGTX1,
    #if defined(RA4M2)
    (R_AGTX0_AGT16_Type *)R_AGTX2,
    (R_AGTX0_AGT16_Type *)R_AGTX3,
    (R_AGTX0_AGT16_Type *)R_AGTX4,
    (R_AGTX0_AGT16_Type *)R_AGTX5,
    #endif
};

static const IRQn_Type ch_to_irq[AGT_CH_SIZE] = {
    #if defined(VECTOR_NUMBER_AGT0_INT)
    VECTOR_NUMBER_AGT0_INT,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT1_INT)
    VECTOR_NUMBER_AGT1_INT,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(RA4M2)
    #if defined(VECTOR_NUMBER_AGT2_INT)
    VECTOR_NUMBER_AGT2_INT,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT3_INT)
    VECTOR_NUMBER_AGT3_INT,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT4_INT)
    VECTOR_NUMBER_AGT4_INT,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT5_INT)
    VECTOR_NUMBER_AGT5_INT,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #endif
};

static const IRQn_Type ch_to_compare_a_irq[AGT_CH_SIZE] = {
    #if defined(VECTOR_NUMBER_AGT0_COMPARE_A)
    VECTOR_NUMBER_AGT0_COMPARE_A,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT1_COMPARE_A)
    VECTOR_NUMBER_AGT1_COMPARE_A,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(RA4M2)
    #if defined(VECTOR_NUMBER_AGT2_COMPARE_A)
    VECTOR_NUMBER_AGT2_COMPARE_A,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT3_COMPARE_A)
    VECTOR_NUMBER_AGT3_COMPARE_A,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT4_COMPARE_A)
    VECTOR_NUMBER_AGT4_COMPARE_A,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT5_COMPARE_A)
    VECTOR_NUMBER_AGT5_COMPARE_A,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #endif
};

static const IRQn_Type ch_to_compare_b_irq[AGT_CH_SIZE] = {
    #if defined(VECTOR_NUMBER_AGT0_COMPARE_B)
    VECTOR_NUMBER_AGT0_COMPARE_B,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT1_COMPARE_B)
    VECTOR_NUMBER_AGT1_COMPARE_B,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(RA4M2)
    #if defined(VECTOR_NUMBER_AGT2_COMPARE_B)
    VECTOR_NUMBER_AGT2_COMPARE_B,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT3_COMPARE_B)
    VECTOR_NUMBER_AGT3_COMPARE_B,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT4_COMPARE_B)
    VECTOR_NUMBER_AGT4_COMPARE_B,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #if defined(VECTOR_NUMBER_AGT5_COMPARE_B)
    VECTOR_NUMBER_AGT5_COMPARE_B,
    #else
    FSP_INVALID_VECTOR,
    #endif
    #endif
};

typedef uintptr_t (*agt_fast_asm_fun_t)(uintptr_t);

typedef struct _ra_agt_timer_state_t {
    float freq;
    uint32_t period_counts;
    volatile uint32_t irq_count;
    AGT_TIMER_CB callback;
    void *callback_param;
    ra_agt_timer_mode_t mode;
    ra_agt_timer_irq_event_t last_event;
    uint32_t compare[AGT_OUTPUT_CHANNELS];
    uint32_t output_pin[AGT_OUTPUT_CHANNELS];
    uint32_t input_pin;
    uint32_t last_capture;
    bool fast_irq;
    bool input_enabled;
    bool input_saved_clock_valid;
    bool compare_set[AGT_OUTPUT_CHANNELS];
    bool compare_irq_enabled[AGT_OUTPUT_CHANNELS];
    bool output_enabled[AGT_OUTPUT_CHANNELS];
    uint8_t input_saved_agtmr1;
    uint8_t input_saved_agtmr2;
    uint8_t input_selector;
    ra_agt_timer_capture_edge_t input_edge;
    ra_agt_timer_capture_measure_t input_measure;
    void *fast_entry;
    uintptr_t fast_param;
} ra_agt_timer_state_t;

static ra_agt_timer_state_t ra_agt_timer_state[AGT_CH_SIZE];
static bool agt_reserved[AGT_CH_SIZE];

typedef struct _ra_agt_output_pin_t {
    uint8_t timer;
    uint8_t output;
    uint32_t pin;
} ra_agt_output_pin_t;

typedef struct _ra_agt_input_pin_t {
    uint8_t timer;
    uint8_t selector;
    uint32_t pin;
} ra_agt_input_pin_t;

#if defined(RA4M2)
static const ra_agt_output_pin_t ra_agt_output_pins[] = {
    { 0, 1, P500 },
    { 0, 2, P501 },
    { 1, 1, P411 },
    { 1, 2, P410 },
    { 2, 1, P409 },
    { 2, 2, P408 },
    { 2, 1, P306 },
    { 2, 2, P305 },
    { 2, 1, P502 },
    { 2, 2, P503 },
    { 3, 1, P108 },
    { 3, 2, P109 },
    { 3, 1, P504 },
    { 3, 2, P505 },
    { 5, 1, P211 },
    { 5, 2, P210 },
    { 5, 1, P111 },
    { 5, 2, P112 },
};

static const ra_agt_input_pin_t ra_agt_input_pins[] = {
    { 0, 0, P100 },
    { 0, 0, P301 },
    { 0, 0, P407 },
    { 0, 1, P404 },
    { 0, 2, P402 },
    { 0, 3, P403 },
    { 1, 0, P400 },
    { 1, 1, P404 },
    { 1, 2, P402 },
    { 1, 3, P403 },
    { 2, 0, P103 },
    { 2, 1, P404 },
    { 2, 2, P402 },
    { 2, 3, P403 },
    { 3, 0, P600 },
    { 3, 1, P404 },
    { 3, 2, P402 },
    { 3, 3, P403 },
    { 4, 0, P415 },
    { 4, 0, P603 },
    { 5, 0, P414 },
    { 5, 0, P114 },
};
#else
static const ra_agt_output_pin_t ra_agt_output_pins[] = {
};
static const ra_agt_input_pin_t ra_agt_input_pins[] = {
};
#endif

bool ra_agt_timer_is_valid(uint32_t ch) {
    return ch < AGT_CH_SIZE && agt_regs[ch] != NULL && ch_to_irq[ch] != FSP_INVALID_VECTOR;
}

bool ra_agt_timer_reserve(uint32_t ch) {
    #ifdef MICROPY_HW_AGT_RESERVED_MASK
    /* Board-level reservation: channels permanently owned by another
     * driver (e.g. LoRaWAN renesas stack on VK_RA4M2: AGT4/AGT5). Reject
     * before any state inspection so a stuck/half-open hardware state on
     * the LoRaWAN channels cannot accidentally be surfaced to Python. */
    if ((MICROPY_HW_AGT_RESERVED_MASK >> ch) & 1U) {
        return false;
    }
    #endif
    if (ch >= AGT_CH_SIZE || agt_reserved[ch]) {
        return false;
    }
    // A live SOFTWARE owner means the channel is genuinely in use -> reject. These
    // checks come FIRST so a channel with no owner can be reclaimed below.
    ra_agt_timer_state_t *st = &ra_agt_timer_state[ch];
    if (st->callback != NULL || st->freq != 0.0f || st->period_counts != 0U) {
        return false;
    }
    if (st->input_enabled) {
        return false;
    }
    for (size_t i = 0; i < AGT_OUTPUT_CHANNELS; ++i) {
        if (st->output_enabled[i] || st->compare_irq_enabled[i]) {
            return false;
        }
    }
    // No software owner. If the hardware timer is still RUNNING it is an ORPHAN left
    // by a session killed without deinit -- its running state (AGTCR.TCSTF) survives a
    // warm reset, so reserve would otherwise reject it forever until a cold reset.
    // Reclaim it: stop the count, then take the channel (init reconfigures it fully).
    if (agt_regs[ch] != NULL && agt_regs[ch]->CTRL.AGTCR_b.TCSTF != 0U) {
        agt_regs[ch]->CTRL.AGTCR_b.TSTART = 0U;         // request stop
        for (uint32_t spin = 0U; spin < 10000U; ++spin) {
            if (agt_regs[ch]->CTRL.AGTCR_b.TCSTF == 0U) {
                break;                                   // counter has stopped
            }
        }
    }
    agt_reserved[ch] = true;
    return true;
}

void ra_agt_timer_release_reservation(uint32_t ch) {
    if (ch < AGT_CH_SIZE) {
        agt_reserved[ch] = false;
    }
}

void ra_agt_timer_clear_all_reservations(void) {
    for (size_t i = 0; i < AGT_CH_SIZE; ++i) {
        agt_reserved[i] = false;
    }
}

bool ra_agt_timer_is_reserved(uint32_t ch) {
    #ifdef MICROPY_HW_AGT_RESERVED_MASK
    if ((MICROPY_HW_AGT_RESERVED_MASK >> ch) & 1U) {
        return true;
    }
    #endif
    return ch < AGT_CH_SIZE && agt_reserved[ch];
}

static uint32_t ra_agt_timer_clock_frequency_get(uint32_t ch) {
    uint32_t clock_freq_hz = 0;
    uint8_t count_source_int = agt_regs[ch]->CTRL.AGTMR1_b.TCK;
    uint8_t divider = 0;

    if (0U == (count_source_int & (~AGT_SOURCE_CLOCK_PCLKB_BITS))) {
        clock_freq_hz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB);   // real PCLKB, not PCLK
        divider = count_source_int;
        if (divider != 0U) {
            // Map PCLKB / 8 from 1 to 3 and PCLKB / 2 from 3 to 1.
            divider ^= 2U;
        }
    } else {
        clock_freq_hz = 32768U;
        divider = agt_regs[ch]->CTRL.AGTMR2_b.CKS;
    }

    clock_freq_hz >>= divider;
    return clock_freq_hz;
}

static uint32_t ra_agt_timer_reg_get_counter(uint32_t ch) {
    return agt_regs[ch]->AGT;
}

static void ra_agt_timer_reg_set_counter(uint32_t ch, uint32_t counter) {
    agt_regs[ch]->AGT = (uint16_t)counter;
}

static void ra_agt_timer_sync_freq(uint32_t ch) {
    if (ra_agt_timer_state[ch].period_counts == 0U) {
        ra_agt_timer_state[ch].freq = 0.0f;
        return;
    }
    ra_agt_timer_state[ch].freq = (float)ra_agt_timer_clock_frequency_get(ch) / (float)ra_agt_timer_state[ch].period_counts;
}

static int ra_agt_timer_output_index(uint32_t output) {
    if (output < 1U || output > AGT_OUTPUT_CHANNELS) {
        return -1;
    }
    return (int)output - 1;
}

static IRQn_Type ra_agt_timer_output_irq_get(uint32_t ch, uint32_t output) {
    int output_idx = ra_agt_timer_output_index(output);

    if (!ra_agt_timer_is_valid(ch) || output_idx < 0) {
        return FSP_INVALID_VECTOR;
    }
    return output_idx == 0 ? ch_to_compare_a_irq[ch] : ch_to_compare_b_irq[ch];
}

static void ra_agt_timer_update_output_hw(uint32_t ch);

typedef enum {
    AGT_IRQ_SOURCE_INT = 0,
    AGT_IRQ_SOURCE_COMPARE_A = 1,
    AGT_IRQ_SOURCE_COMPARE_B = 2,
} ra_agt_irq_source_t;

static bool ra_agt_timer_is_output_pin(uint32_t ch, uint32_t output, uint32_t pin) {
    for (size_t i = 0; i < sizeof(ra_agt_output_pins) / sizeof(ra_agt_output_pins[0]); ++i) {
        if (ra_agt_output_pins[i].timer == ch && ra_agt_output_pins[i].output == output && ra_agt_output_pins[i].pin == pin) {
            return true;
        }
    }
    return false;
}

static const ra_agt_input_pin_t *ra_agt_timer_find_input_pin(uint32_t ch, uint32_t pin) {
    for (size_t i = 0; i < sizeof(ra_agt_input_pins) / sizeof(ra_agt_input_pins[0]); ++i) {
        if (ra_agt_input_pins[i].timer == ch && ra_agt_input_pins[i].pin == pin) {
            return &ra_agt_input_pins[i];
        }
    }
    return NULL;
}

static bool ra_agt_timer_has_input_support(uint32_t ch) {
    for (size_t i = 0; i < sizeof(ra_agt_input_pins) / sizeof(ra_agt_input_pins[0]); ++i) {
        if (ra_agt_input_pins[i].timer == ch) {
            return true;
        }
    }
    return false;
}

static void ra_agt_timer_apply_input_mode(uint32_t ch) {
    uint8_t agtmr1 = agt_regs[ch]->CTRL.AGTMR1;
    uint8_t agtioc = 0U;

    agtmr1 &= (uint8_t)~(R_AGTX0_AGT16_CTRL_AGTMR1_TMOD_Msk | R_AGTX0_AGT16_CTRL_AGTMR1_TEDGPL_Msk | R_AGTX0_AGT16_CTRL_AGTMR1_TCK_Msk);
    if (ra_agt_timer_state[ch].input_measure == RA_AGT_TIMER_CAPTURE_MEASURE_PULSE_WIDTH_LOW) {
        agtmr1 |= AGT_MODE_PULSE_WIDTH;
        agtioc = 0U;
    } else if (ra_agt_timer_state[ch].input_measure == RA_AGT_TIMER_CAPTURE_MEASURE_PULSE_WIDTH_HIGH) {
        agtmr1 |= AGT_MODE_PULSE_WIDTH;
        agtioc = 1U;
    } else if (ra_agt_timer_state[ch].input_measure == RA_AGT_TIMER_CAPTURE_MEASURE_EVENT_COUNT) {
        agtmr1 |= AGT_MODE_EVENT_COUNTER;
        if (ra_agt_timer_state[ch].input_edge == RA_AGT_TIMER_CAPTURE_EDGE_BOTH) {
            agtmr1 |= R_AGTX0_AGT16_CTRL_AGTMR1_TEDGPL_Msk;
            agtioc = 0U;
        } else {
            agtioc = (uint8_t)ra_agt_timer_state[ch].input_edge;
        }
    } else {
        agtmr1 |= (ra_agt_timer_state[ch].input_saved_agtmr1 & R_AGTX0_AGT16_CTRL_AGTMR1_TCK_Msk);
        agtmr1 |= AGT_MODE_PULSE_PERIOD;
        agtioc = (uint8_t)ra_agt_timer_state[ch].input_edge;
    }
    agt_regs[ch]->CTRL.AGTMR1 = agtmr1;
    agt_regs[ch]->CTRL.AGTIOC = agtioc;
    agt_regs[ch]->CTRL.AGTIOSEL = (uint8_t)ra_agt_timer_state[ch].input_selector;
    agt_regs[ch]->CTRL.AGTCMSR = 0U;
}

static void ra_agt_timer_apply_timer_mode(uint32_t ch) {
    if (ra_agt_timer_state[ch].input_saved_clock_valid) {
        agt_regs[ch]->CTRL.AGTMR2 = ra_agt_timer_state[ch].input_saved_agtmr2;
        agt_regs[ch]->CTRL.AGTMR1 = ra_agt_timer_state[ch].input_saved_agtmr1;
    } else {
        agt_regs[ch]->CTRL.AGTMR1 &= (uint8_t)~(R_AGTX0_AGT16_CTRL_AGTMR1_TMOD_Msk | R_AGTX0_AGT16_CTRL_AGTMR1_TEDGPL_Msk);
    }
    agt_regs[ch]->CTRL.AGTIOC = 0U;
    agt_regs[ch]->CTRL.AGTIOSEL = 0U;
    ra_agt_timer_update_output_hw(ch);
}

static void ra_agt_timer_update_output_hw(uint32_t ch) {
    uint8_t agtcmsr = 0;

    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }
    if (ra_agt_timer_state[ch].input_enabled) {
        agt_regs[ch]->CTRL.AGTCMSR = 0U;
        return;
    }

    if (ra_agt_timer_state[ch].output_enabled[0] || ra_agt_timer_state[ch].compare_irq_enabled[0]) {
        agtcmsr |= AGT_COMPARE_MATCH_A_ENABLE;
    }
    if (ra_agt_timer_state[ch].output_enabled[0]) {
        agtcmsr |= AGT_COMPARE_MATCH_A_OUTPUT;
    }
    if (ra_agt_timer_state[ch].output_enabled[1] || ra_agt_timer_state[ch].compare_irq_enabled[1]) {
        agtcmsr |= AGT_COMPARE_MATCH_B_ENABLE;
    }
    if (ra_agt_timer_state[ch].output_enabled[1]) {
        agtcmsr |= AGT_COMPARE_MATCH_B_OUTPUT;
    }
    agt_regs[ch]->CTRL.AGTCMSR = agtcmsr;
}

static void ra_agt_timer_write_compare(uint32_t ch, uint32_t output, uint32_t compare) {
    if (output == 1U) {
        agt_regs[ch]->AGTCMA = (uint16_t)compare;
    } else {
        agt_regs[ch]->AGTCMB = (uint16_t)compare;
    }
}

static void ra_agt_timer_release_all_output_pins(uint32_t ch) {
    for (size_t i = 0; i < AGT_OUTPUT_CHANNELS; ++i) {
        if (ra_agt_timer_state[ch].output_enabled[i]) {
            ra_gpio_config(ra_agt_timer_state[ch].output_pin[i], GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
            ra_agt_timer_state[ch].output_enabled[i] = false;
        }
    }
    ra_agt_timer_update_output_hw(ch);
}

static void ra_agt_timer_release_input_pin(uint32_t ch) {
    if (ra_agt_timer_state[ch].input_enabled) {
        ra_gpio_config(ra_agt_timer_state[ch].input_pin, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
        ra_agt_timer_state[ch].input_enabled = false;
        ra_agt_timer_state[ch].input_pin = PIN_END;
        ra_agt_timer_state[ch].input_selector = 0U;
        ra_agt_timer_state[ch].last_capture = 0U;
        ra_agt_timer_state[ch].last_event = RA_AGT_TIMER_IRQ_EVENT_NONE;
        ra_agt_timer_state[ch].input_measure = RA_AGT_TIMER_CAPTURE_MEASURE_PERIOD;
        ra_agt_timer_state[ch].input_saved_clock_valid = false;
    }
}

static uint8_t ra_agt_timer_irq_flag_mask(ra_agt_timer_irq_event_t event) {
    switch (event) {
        case RA_AGT_TIMER_IRQ_EVENT_CYCLE_END:
            return R_AGTX0_AGT16_CTRL_AGTCR_TUNDF_Msk;
        case RA_AGT_TIMER_IRQ_EVENT_CAPTURE:
            return R_AGTX0_AGT16_CTRL_AGTCR_TEDGF_Msk;
        case RA_AGT_TIMER_IRQ_EVENT_COMPARE_A:
            return R_AGTX0_AGT16_CTRL_AGTCR_TCMAF_Msk;
        case RA_AGT_TIMER_IRQ_EVENT_COMPARE_B:
            return R_AGTX0_AGT16_CTRL_AGTCR_TCMBF_Msk;
        default:
            return 0U;
    }
}

static bool ra_agt_timer_irq_decode(IRQn_Type irq, uint32_t *ch_out, ra_agt_irq_source_t *source_out) {
    for (uint32_t ch = 0; ch < AGT_CH_SIZE; ++ch) {
        if (ch_to_irq[ch] == irq) {
            *ch_out = ch;
            *source_out = AGT_IRQ_SOURCE_INT;
            return true;
        }
        if (ch_to_compare_a_irq[ch] == irq) {
            *ch_out = ch;
            *source_out = AGT_IRQ_SOURCE_COMPARE_A;
            return true;
        }
        if (ch_to_compare_b_irq[ch] == irq) {
            *ch_out = ch;
            *source_out = AGT_IRQ_SOURCE_COMPARE_B;
            return true;
        }
    }
    return false;
}

static bool ra_agt_timer_set_period_internal(uint32_t ch, uint32_t period_counts) {
    if (!ra_agt_timer_is_valid(ch) || period_counts == 0U || period_counts > AGT_MAX_PERIOD_16BIT) {
        return false;
    }

    ra_agt_timer_state[ch].period_counts = period_counts;
    ra_agt_timer_reg_set_counter(ch, period_counts - 1U);
    for (size_t i = 0; i < AGT_OUTPUT_CHANNELS; ++i) {
        if (!ra_agt_timer_state[ch].compare_set[i] || ra_agt_timer_state[ch].compare[i] >= period_counts) {
            ra_agt_timer_state[ch].compare[i] = period_counts >> 1;
        }
        ra_agt_timer_write_compare(ch, i + 1U, ra_agt_timer_state[ch].compare[i]);
    }
    ra_agt_timer_sync_freq(ch);
    return true;
}

static void ra_agt_timer_module_start(uint32_t ch) {
    #if defined(RA4M2)
    if (ch < 4) {
        static const uint32_t mstpcrd_masks[4] = {
            R_MSTP_MSTPCRD_MSTPD3_Msk,
            R_MSTP_MSTPCRD_MSTPD2_Msk,
            R_MSTP_MSTPCRD_MSTPD1_Msk,
            R_MSTP_MSTPCRD_MSTPD0_Msk,
        };
        ra_mstpcrd_start(mstpcrd_masks[ch]);
    } else {
        static const uint32_t mstpcre_masks[2] = {
            R_MSTP_MSTPCRE_MSTPE15_Msk,
            R_MSTP_MSTPCRE_MSTPE14_Msk,
        };
        ra_mstpcre_start(mstpcre_masks[ch - 4]);
    }
    #else
    if (ch == 0) {
        ra_mstpcrd_start(R_MSTP_MSTPCRD_MSTPD3_Msk);
    } else {
        ra_mstpcrd_start(R_MSTP_MSTPCRD_MSTPD2_Msk);
    }
    #endif
}

static void ra_agt_timer_module_stop(uint32_t ch) {
    #if defined(RA4M2)
    if (ch < 4) {
        static const uint32_t mstpcrd_masks[4] = {
            R_MSTP_MSTPCRD_MSTPD3_Msk,
            R_MSTP_MSTPCRD_MSTPD2_Msk,
            R_MSTP_MSTPCRD_MSTPD1_Msk,
            R_MSTP_MSTPCRD_MSTPD0_Msk,
        };
        ra_mstpcrd_stop(mstpcrd_masks[ch]);
    } else {
        static const uint32_t mstpcre_masks[2] = {
            R_MSTP_MSTPCRE_MSTPE15_Msk,
            R_MSTP_MSTPCRE_MSTPE14_Msk,
        };
        ra_mstpcre_stop(mstpcre_masks[ch - 4]);
    }
    #else
    if (ch == 0) {
        ra_mstpcrd_stop(R_MSTP_MSTPCRD_MSTPD3_Msk);
    } else {
        ra_mstpcrd_stop(R_MSTP_MSTPCRD_MSTPD2_Msk);
    }
    #endif
}

void ra_agt_timer_set_callback(uint32_t ch, AGT_TIMER_CB cb, void *param) {
    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }
    ra_agt_timer_state[ch].callback = cb;
    ra_agt_timer_state[ch].callback_param = param;
}

void ra_agt_timer_set_fast_irq(uint32_t ch, bool fast_irq, void *fast_entry, uintptr_t fast_param) {
    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }
    ra_agt_timer_state[ch].fast_irq = fast_irq && fast_entry != NULL;
    ra_agt_timer_state[ch].fast_entry = fast_entry;
    ra_agt_timer_state[ch].fast_param = fast_param;
}

bool ra_agt_timer_get_fast_irq(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return false;
    }
    return ra_agt_timer_state[ch].fast_irq;
}

void *ra_agt_timer_get_fast_entry(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return NULL;
    }
    return ra_agt_timer_state[ch].fast_entry;
}

uintptr_t ra_agt_timer_get_fast_param(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return 0;
    }
    return ra_agt_timer_state[ch].fast_param;
}

void ra_agt_timer_set_mode(uint32_t ch, ra_agt_timer_mode_t mode) {
    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }
    ra_agt_timer_state[ch].mode = mode;
}

ra_agt_timer_mode_t ra_agt_timer_get_mode(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return RA_AGT_TIMER_MODE_PERIODIC;
    }
    return ra_agt_timer_state[ch].mode;
}

static void ra_agt_timer_chk_callback(uint32_t ch, ra_agt_irq_source_t source) {
    uint8_t agtcr;
    ra_agt_timer_irq_event_t event = RA_AGT_TIMER_IRQ_EVENT_NONE;
    uint8_t clear_mask;

    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }

    agtcr = agt_regs[ch]->CTRL.AGTCR;
    ra_agt_timer_state[ch].irq_count += 1;
    ra_agt_timer_state[ch].last_event = RA_AGT_TIMER_IRQ_EVENT_NONE;

    if ((source == AGT_IRQ_SOURCE_INT) &&
        (agtcr & R_AGTX0_AGT16_CTRL_AGTCR_TUNDF_Msk) &&
        ra_agt_timer_state[ch].mode == RA_AGT_TIMER_MODE_ONE_SHOT) {
        ra_agt_timer_stop(ch);
    }

    if (source == AGT_IRQ_SOURCE_COMPARE_A && (agtcr & R_AGTX0_AGT16_CTRL_AGTCR_TCMAF_Msk)) {
        event = RA_AGT_TIMER_IRQ_EVENT_COMPARE_A;
    } else if (source == AGT_IRQ_SOURCE_COMPARE_B && (agtcr & R_AGTX0_AGT16_CTRL_AGTCR_TCMBF_Msk)) {
        event = RA_AGT_TIMER_IRQ_EVENT_COMPARE_B;
    } else if (agtcr & R_AGTX0_AGT16_CTRL_AGTCR_TUNDF_Msk) {
        event = RA_AGT_TIMER_IRQ_EVENT_CYCLE_END;
    } else if (ra_agt_timer_state[ch].input_enabled && (agtcr & R_AGTX0_AGT16_CTRL_AGTCR_TEDGF_Msk)) {
        uint32_t period_counts = ra_agt_timer_get_period(ch);
        if (period_counts == 0U) {
            return;
        }
        uint32_t reload_value = period_counts - 1U;
        ra_agt_timer_state[ch].last_capture = reload_value - ra_agt_timer_reg_get_counter(ch);
        if (ra_agt_timer_state[ch].input_measure == RA_AGT_TIMER_CAPTURE_MEASURE_PERIOD) {
            ra_agt_timer_state[ch].last_capture += 1U;
        } else {
            ra_agt_timer_reg_set_counter(ch, reload_value);
        }
        event = RA_AGT_TIMER_IRQ_EVENT_CAPTURE;
    }

    ra_agt_timer_state[ch].last_event = event;
    if (event == RA_AGT_TIMER_IRQ_EVENT_NONE) {
        return;
    }

    clear_mask = ra_agt_timer_irq_flag_mask(event);
    if (clear_mask != 0U) {
        agtcr = agt_regs[ch]->CTRL.AGTCR;
        agt_regs[ch]->CTRL.AGTCR = (uint8_t)(agtcr & ~clear_mask);
    }

    if (ra_agt_timer_state[ch].fast_irq && ra_agt_timer_state[ch].fast_entry != NULL) {
        agt_fast_asm_fun_t fast_fn = (agt_fast_asm_fun_t)ra_agt_timer_state[ch].fast_entry;
        fast_fn(ra_agt_timer_state[ch].fast_param);
        return;
    }

    if (ra_agt_timer_state[ch].callback) {
        (*ra_agt_timer_state[ch].callback)(ra_agt_timer_state[ch].callback_param);
    }
}

void ra_agt_timer_start(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }
    if ((ra_agt_timer_state[ch].mode == RA_AGT_TIMER_MODE_ONE_SHOT || ra_agt_timer_state[ch].input_enabled) &&
        ra_agt_timer_state[ch].period_counts != 0U) {
        ra_agt_timer_reg_set_counter(ch, ra_agt_timer_state[ch].period_counts - 1U);
    }
    agt_regs[ch]->CTRL.AGTCR_b.TSTART = 1; /* start counter */
}

void ra_agt_timer_stop(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }
    agt_regs[ch]->CTRL.AGTCR_b.TSTART = 0; /* stop counter */
}

bool ra_agt_timer_set_freq_ex(uint32_t ch, float freq, ra_agt_clock_source_t clk_src) {
    if (!ra_agt_timer_is_valid(ch)) {
        return false;
    }
    /* SOSC selection requires the board to actually have the sub-clock crystal
     * populated AND the BSP to have started it. Reject at compile time on
     * boards that don't (caller in the Python layer should also gate this so
     * the user gets a clean error instead of a silent no-op). */
    if (clk_src == RA_AGT_CLOCK_SOSC) {
        #if !defined(MICROPY_HW_SUBCLK_POPULATED) || (MICROPY_HW_SUBCLK_POPULATED == 0)
        return false;
        #endif
    }
    R_AGTX0_AGT16_Type *agt_reg = agt_regs[ch];
    uint8_t source = 0;
    uint32_t period_counts = 0;
    uint8_t cks = 0;
    ra_agt_timer_state[ch].irq_count = 0;
    /* The AGT counts from PCLKB, NOT the CPU/ICLK "PCLK" macro. On RA6M3 PCLKB is
     * PLL/4 = 60 MHz while PCLK is 120 MHz, so the old (PCLK/2)/freq period was 2x too
     * long and every requested rate came out at HALF (48 kHz -> 24 kHz physically).
     * Query the real PCLKB like the FSP AGT driver; where PCLK == PCLKB this is a no-op. */
    uint32_t pclkb = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB);
    if (freq > (float)(pclkb / 2)) {
        return false;
    } else if (freq >= 1000.0) {
        /* PCLKB/2: crystal-accurate, for freq >= 1000 Hz. clk_src ignored. */
        source = AGT_PCLKB2;
        period_counts = (uint32_t)((float)(pclkb / 2) / freq);
    } else if (freq >= 77.0) {
        /* PCLKB/8: crystal-accurate, for 77–999 Hz (min ~= (PCLKB/8)/65536).
         * Preferred over LOCO (±15% RC) for accuracy. clk_src ignored. */
        source = AGT_PCLKB8;
        period_counts = (uint32_t)((float)(pclkb / 8) / freq);
    } else if (freq > 1.0) {
        /* Low-frequency branch. AGTSCLK (TCK=110b, LPM=0) feeds from SOSC
         * crystal; AGTKCLK (TCK=100b) feeds from LOCO directly. Both deliver
         * 32.768 kHz nominal so the divider/period math is identical — only
         * the TCK selector differs. */
        source = (clk_src == RA_AGT_CLOCK_SOSC) ? AGT_AGTSCLK : AGT_AGTKCLK;
        cks = 2;
        period_counts = (uint32_t)((float)(32768 / 4) / freq);
    } else if (freq > 0.01) {
        source = (clk_src == RA_AGT_CLOCK_SOSC) ? AGT_AGTSCLK : AGT_AGTKCLK;
        period_counts = (uint32_t)((float)(32768 / 128) / freq);
        cks = 7;
    } else {
        return false;
    }
    if (period_counts == 0U) {
        period_counts = 1U;
    }
    if (period_counts > AGT_MAX_PERIOD_16BIT) {
        return false;
    }
    agt_reg->CTRL.AGTCR_b.TSTART = 0;                // stop counter
    /* Write order matters (RA4M2 manual §22.2.5 Note 7):
     * "Do not change TCK[2:0] when CKS[2:0] is not 000b."
     * So: clear CKS first, then set TCK, then set CKS.
     * LPM stays 0 always — see AGT_AGTMR2_LPM_BIT comment at top of file. */
    agt_reg->CTRL.AGTMR2 = 0;                        // CKS=000b first
    agt_reg->CTRL.AGTMR1 = (uint8_t)(source << 4);   // set TCK
    agt_reg->CTRL.AGTMR2 = cks;                      // now set CKS
    (void)ra_agt_timer_set_period_internal(ch, period_counts);
    if (ra_agt_timer_state[ch].input_enabled) {
        ra_agt_timer_state[ch].input_saved_agtmr1 = agt_reg->CTRL.AGTMR1;
        ra_agt_timer_state[ch].input_saved_agtmr2 = agt_reg->CTRL.AGTMR2;
        ra_agt_timer_state[ch].input_saved_clock_valid = true;
        ra_agt_timer_apply_input_mode(ch);
    }
    return true;
}

void ra_agt_timer_set_freq(uint32_t ch, float freq) {
    /* Default low-freq AGT source = SOSC (crystal-accurate ±20-50 ppm) on
     * boards that populate the sub-clock; falls back to LOCO transparently
     * elsewhere via the _ex check at L765-769. */
    #if defined(MICROPY_HW_SUBCLK_POPULATED) && (MICROPY_HW_SUBCLK_POPULATED == 1)
    (void)ra_agt_timer_set_freq_ex(ch, freq, RA_AGT_CLOCK_SOSC);
    #else
    (void)ra_agt_timer_set_freq_ex(ch, freq, RA_AGT_CLOCK_DEFAULT);
    #endif
}

float ra_agt_timer_get_freq(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return 0;
    }
    return ra_agt_timer_state[ch].freq;
}

bool ra_agt_timer_set_period(uint32_t ch, uint32_t period_counts) {
    return ra_agt_timer_set_period_internal(ch, period_counts);
}

uint32_t ra_agt_timer_get_period(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return 0;
    }
    if (ra_agt_timer_state[ch].period_counts == 0U) {
        return ra_agt_timer_reg_get_counter(ch) + 1U;
    }
    return ra_agt_timer_state[ch].period_counts;
}

bool ra_agt_timer_set_counter(uint32_t ch, uint32_t counter) {
    uint32_t period_counts = 0;

    if (!ra_agt_timer_is_valid(ch)) {
        return false;
    }

    period_counts = ra_agt_timer_get_period(ch);
    if (period_counts == 0U) {
        return false;
    }
    if (counter >= period_counts) {
        counter = period_counts - 1U;
    }
    ra_agt_timer_reg_set_counter(ch, counter);
    return true;
}

uint32_t ra_agt_timer_get_counter(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return 0;
    }
    return ra_agt_timer_reg_get_counter(ch);
}

bool ra_agt_timer_is_output_channel(uint32_t ch, uint32_t output) {
    if (!ra_agt_timer_is_valid(ch) || ra_agt_timer_output_index(output) < 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(ra_agt_output_pins) / sizeof(ra_agt_output_pins[0]); ++i) {
        if (ra_agt_output_pins[i].timer == ch && ra_agt_output_pins[i].output == output) {
            return true;
        }
    }
    return false;
}

bool ra_agt_timer_is_input_capture_supported(uint32_t ch) {
    return ra_agt_timer_is_valid(ch) && ra_agt_timer_has_input_support(ch);
}

bool ra_agt_timer_channel_pin_assign(uint32_t ch, uint32_t output, uint32_t pin) {
    int output_idx = ra_agt_timer_output_index(output);

    if (!ra_agt_timer_is_output_pin(ch, output, pin) || output_idx < 0 || ra_agt_timer_state[ch].input_enabled) {
        return false;
    }

    if (ra_agt_timer_state[ch].output_enabled[output_idx] && ra_agt_timer_state[ch].output_pin[output_idx] != pin) {
        ra_gpio_config(ra_agt_timer_state[ch].output_pin[output_idx], GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
    }

    ra_gpio_config(pin, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_LOW_POWER, AF_AGT);
    ra_agt_timer_state[ch].output_pin[output_idx] = pin;
    ra_agt_timer_state[ch].output_enabled[output_idx] = true;
    ra_agt_timer_update_output_hw(ch);
    return true;
}

void ra_agt_timer_channel_pin_release(uint32_t ch, uint32_t output) {
    int output_idx = ra_agt_timer_output_index(output);

    if (!ra_agt_timer_is_valid(ch) || output_idx < 0) {
        return;
    }

    if (ra_agt_timer_state[ch].output_enabled[output_idx]) {
        ra_gpio_config(ra_agt_timer_state[ch].output_pin[output_idx], GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
        ra_agt_timer_state[ch].output_enabled[output_idx] = false;
    }
    ra_agt_timer_update_output_hw(ch);
}

bool ra_agt_timer_input_pin_assign(uint32_t ch, uint32_t pin, ra_agt_timer_capture_measure_t measure, ra_agt_timer_capture_edge_t edge) {
    const ra_agt_input_pin_t *input_pin = NULL;
    bool was_running;

    if (!ra_agt_timer_is_input_capture_supported(ch)) {
        return false;
    }
    if (measure != RA_AGT_TIMER_CAPTURE_MEASURE_PERIOD &&
        measure != RA_AGT_TIMER_CAPTURE_MEASURE_PULSE_WIDTH_LOW &&
        measure != RA_AGT_TIMER_CAPTURE_MEASURE_PULSE_WIDTH_HIGH &&
        measure != RA_AGT_TIMER_CAPTURE_MEASURE_EVENT_COUNT) {
        return false;
    }
    if (edge != RA_AGT_TIMER_CAPTURE_EDGE_RISING &&
        edge != RA_AGT_TIMER_CAPTURE_EDGE_FALLING &&
        edge != RA_AGT_TIMER_CAPTURE_EDGE_BOTH) {
        return false;
    }
    if (measure != RA_AGT_TIMER_CAPTURE_MEASURE_EVENT_COUNT && edge == RA_AGT_TIMER_CAPTURE_EDGE_BOTH) {
        return false;
    }
    for (size_t i = 0; i < AGT_OUTPUT_CHANNELS; ++i) {
        if (ra_agt_timer_state[ch].output_enabled[i]) {
            return false;
        }
    }

    input_pin = ra_agt_timer_find_input_pin(ch, pin);
    if (input_pin == NULL) {
        return false;
    }

    was_running = agt_regs[ch]->CTRL.AGTCR_b.TCSTF != 0U;
    ra_agt_timer_stop(ch);
    ra_agt_timer_release_input_pin(ch);
    ra_agt_timer_state[ch].input_saved_agtmr1 = agt_regs[ch]->CTRL.AGTMR1;
    ra_agt_timer_state[ch].input_saved_agtmr2 = agt_regs[ch]->CTRL.AGTMR2;
    ra_agt_timer_state[ch].input_saved_clock_valid = true;

    ra_gpio_config(pin, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_AGT);
    ra_agt_timer_state[ch].input_enabled = true;
    ra_agt_timer_state[ch].input_pin = pin;
    ra_agt_timer_state[ch].input_selector = input_pin->selector;
    ra_agt_timer_state[ch].input_edge = edge;
    ra_agt_timer_state[ch].input_measure = measure;
    ra_agt_timer_state[ch].last_capture = 0U;
    ra_agt_timer_state[ch].last_event = RA_AGT_TIMER_IRQ_EVENT_NONE;
    ra_agt_timer_apply_input_mode(ch);

    if (was_running) {
        ra_agt_timer_start(ch);
    }
    return true;
}

void ra_agt_timer_input_pin_release(uint32_t ch) {
    bool was_running;

    if (!ra_agt_timer_is_valid(ch) || !ra_agt_timer_state[ch].input_enabled) {
        return;
    }

    was_running = agt_regs[ch]->CTRL.AGTCR_b.TCSTF != 0U;
    ra_agt_timer_stop(ch);
    ra_agt_timer_release_input_pin(ch);
    ra_agt_timer_apply_timer_mode(ch);

    if (was_running) {
        if (ra_agt_timer_state[ch].period_counts != 0U) {
            ra_agt_timer_reg_set_counter(ch, ra_agt_timer_state[ch].period_counts - 1U);
        }
        ra_agt_timer_start(ch);
    }
}

bool ra_agt_timer_input_is_configured(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return false;
    }
    return ra_agt_timer_state[ch].input_enabled;
}

bool ra_agt_timer_has_compare(uint32_t ch, uint32_t output) {
    int output_idx = ra_agt_timer_output_index(output);

    if (!ra_agt_timer_is_valid(ch) || output_idx < 0) {
        return false;
    }
    return ra_agt_timer_state[ch].compare_set[output_idx];
}

bool ra_agt_timer_set_compare(uint32_t ch, uint32_t output, uint32_t compare) {
    int output_idx = ra_agt_timer_output_index(output);
    uint32_t period_counts = ra_agt_timer_get_period(ch);

    if (!ra_agt_timer_is_valid(ch) || output_idx < 0 || period_counts == 0U || compare >= period_counts || ra_agt_timer_state[ch].input_enabled) {
        return false;
    }

    ra_agt_timer_state[ch].compare[output_idx] = compare;
    ra_agt_timer_state[ch].compare_set[output_idx] = true;
    ra_agt_timer_write_compare(ch, output, compare);
    return true;
}

uint32_t ra_agt_timer_get_compare(uint32_t ch, uint32_t output) {
    int output_idx = ra_agt_timer_output_index(output);

    if (!ra_agt_timer_is_valid(ch) || output_idx < 0) {
        return 0;
    }
    return ra_agt_timer_state[ch].compare[output_idx];
}

uint32_t ra_agt_timer_get_capture(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return 0;
    }
    if (ra_agt_timer_state[ch].input_enabled &&
        ra_agt_timer_state[ch].input_measure == RA_AGT_TIMER_CAPTURE_MEASURE_EVENT_COUNT) {
        uint32_t period_counts = ra_agt_timer_get_period(ch);
        if (period_counts == 0U) {
            return 0U;
        }
        return (period_counts - 1U) - ra_agt_timer_reg_get_counter(ch);
    }
    return ra_agt_timer_state[ch].last_capture;
}

ra_agt_timer_irq_event_t ra_agt_timer_get_irq_event(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return RA_AGT_TIMER_IRQ_EVENT_NONE;
    }
    return ra_agt_timer_state[ch].last_event;
}

void ra_agt_timer_set_compare_irq(uint32_t ch, uint32_t output, bool enable) {
    int output_idx = ra_agt_timer_output_index(output);
    IRQn_Type irq;

    if (!ra_agt_timer_is_valid(ch) || output_idx < 0) {
        return;
    }

    irq = ra_agt_timer_output_irq_get(ch, output);
    if (irq == FSP_INVALID_VECTOR) {
        return;
    }

    if (enable) {
        if (!ra_agt_timer_state[ch].compare_irq_enabled[output_idx]) {
            ra_agt_timer_state[ch].compare_irq_enabled[output_idx] = true;
            ra_agt_timer_update_output_hw(ch);
            R_BSP_IrqCfgEnable(irq, RA_PRI_TIM5, NULL);
        }
    } else if (ra_agt_timer_state[ch].compare_irq_enabled[output_idx]) {
        ra_agt_timer_state[ch].compare_irq_enabled[output_idx] = false;
        R_BSP_IrqDisable(irq);
        R_BSP_IrqStatusClear(irq);
        ra_agt_timer_update_output_hw(ch);
    }
}

void ra_agt_timer_init(uint32_t ch, float freq) {
    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }
    R_AGTX0_AGT16_Type *agt_reg = agt_regs[ch];
    ra_agt_timer_module_start(ch);
    if (ra_agt_timer_state[ch].mode != RA_AGT_TIMER_MODE_ONE_SHOT) {
        ra_agt_timer_state[ch].mode = RA_AGT_TIMER_MODE_PERIODIC;
    }
    ra_agt_timer_set_fast_irq(ch, false, NULL, 0);
    ra_agt_timer_set_freq(ch, freq);
    if (ra_agt_timer_state[ch].input_enabled) {
        ra_agt_timer_apply_input_mode(ch);
    } else {
        ra_agt_timer_update_output_hw(ch);
    }
    agt_reg->CTRL.AGTCR_b.TUNDF = 1;                 // underflow interrupt
    R_BSP_IrqCfgEnable(ch_to_irq[ch], RA_PRI_TIM5, NULL);
}

void ra_agt_timer_deinit(uint32_t ch) {
    if (!ra_agt_timer_is_valid(ch)) {
        return;
    }
    for (uint32_t output = 1; output <= AGT_OUTPUT_CHANNELS; ++output) {
        ra_agt_timer_set_compare_irq(ch, output, false);
    }
    R_BSP_IrqDisable(ch_to_irq[ch]);
    R_BSP_IrqStatusClear(ch_to_irq[ch]);
    ra_agt_timer_stop(ch);
    ra_agt_timer_release_input_pin(ch);
    ra_agt_timer_release_all_output_pins(ch);
    ra_agt_timer_release_reservation(ch);
    memset(&ra_agt_timer_state[ch], 0, sizeof(ra_agt_timer_state[ch]));
    ra_agt_timer_module_stop(ch);
}

void ra_port_agt_int_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    uint32_t ch;
    ra_agt_irq_source_t source;
    if ((uint32_t)irq >= IRQ_MAX) {
        return;
    }
    R_BSP_IrqStatusClear(irq);
    if (!ra_agt_timer_irq_decode(irq, &ch, &source)) {
        return;
    }
    ra_agt_timer_chk_callback(ch, source);
}

/* FSP owns agt_int_isr when AGT is used by LoRaWAN or BLE. Their board vector
 * tables route port-owned channels directly to ra_port_agt_int_isr. */
#if (!defined(MICROPY_HW_LORA_STACK_RENESAS) || (MICROPY_HW_LORA_STACK_RENESAS == 0)) && \
    (!defined(MICROPY_HW_ENABLE_BLE) || (MICROPY_HW_ENABLE_BLE == 0))
void agt_int_isr(void) {
    ra_port_agt_int_isr();
}
#endif

extern uint32_t uwTick;

uint32_t mtick() {
    return uwTick;
}
