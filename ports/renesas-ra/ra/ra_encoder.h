/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Vekatech Ltd.
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

#ifndef RA_RA_ENCODER_H_
#define RA_RA_ENCODER_H_

#include <stdint.h>
#include <stdbool.h>

// Maximum number of GPT channels available for encoder use (MCU-dependent).
// Used to size per-channel lookup arrays in both ra_encoder.c and machine_encoder.c.
#if defined(RA4M1)
#define RA_ENCODER_MAX_CH 8
#elif defined(RA4M2)
#define RA_ENCODER_MAX_CH 8
#elif defined(RA4W1)
#define RA_ENCODER_MAX_CH 9
#elif defined(RA6M1)
#define RA_ENCODER_MAX_CH 13
#elif defined(RA6M2) || defined(RA6M3)
#define RA_ENCODER_MAX_CH 14
#elif defined(RA6M5)
#define RA_ENCODER_MAX_CH 10
#else
#error "CMSIS MCU Series is not specified."
#endif

// Callback type for encoder movement notification (called from ISR context)
typedef void (*encoder_irq_cb_t)(void *param);

// Quadrature counting modes
typedef enum {
    ENCODER_MODE_X1 = 1,   // Count on A rising edge only (1x resolution)
    ENCODER_MODE_X2 = 2,   // Count on A rising + falling edges (2x resolution)
    ENCODER_MODE_X4 = 4,   // Count on all edges of A and B (4x resolution)
} encoder_mode_t;

// Noise filter sampling clock divider
typedef enum {
    ENCODER_FILTER_NONE   = 0,  // No noise filter
    ENCODER_FILTER_PCLKD  = 1,  // PCLKD / 1
    ENCODER_FILTER_PCLKD4 = 2,  // PCLKD / 4
    ENCODER_FILTER_PCLKD16 = 3, // PCLKD / 16
    ENCODER_FILTER_PCLKD64 = 4, // PCLKD / 64
} encoder_filter_t;

// Phase counting GTUPSR/GTDNSR bit patterns (bits 8-15)
// Standard quadrature: A leads B = forward (count up), B leads A = reverse (count down)

// X1: Count on GTIOCA rising edge only
#define ENCODER_X1_UP   (1U << 8)   // A↑ while B=Low
#define ENCODER_X1_DN   (1U << 9)   // A↑ while B=High

// X2: Count on GTIOCA rising and falling edges
#define ENCODER_X2_UP   ((1U << 8) | (1U << 11))  // A↑B=L | A↓B=H
#define ENCODER_X2_DN   ((1U << 9) | (1U << 10))  // A↑B=H | A↓B=L

// X4: Count on all edges of both A and B
#define ENCODER_X4_UP   ((1U << 8) | (1U << 11) | (1U << 13) | (1U << 14))  // A↑B=L|A↓B=H|B↑A=H|B↓A=L
#define ENCODER_X4_DN   ((1U << 9) | (1U << 10) | (1U << 12) | (1U << 15))  // A↑B=H|A↓B=L|B↑A=L|B↓A=H

// Encoder instance configuration
typedef struct {
    uint32_t pin_a;             // GTIOCA pin (encoder channel A)
    uint32_t pin_b;             // GTIOCB pin (encoder channel B)
    uint32_t gpt_ch;            // GPT channel number (auto-detected from pins)
    encoder_mode_t mode;        // Counting mode: X1, X2, X4
    encoder_filter_t filter;    // Noise filter setting
    int32_t min_val;            // Minimum counter value (software clamp)
    int32_t max_val;            // Maximum counter value (software clamp)
    int32_t value;              // Current scaled position value
    int32_t init_val;           // Initial counter value
    uint8_t debounce;           // Consecutive reverse counts required to confirm reversal (0=off)
    int8_t  confirmed_dir;      // Confirmed direction: +1, -1, 0(none)
    int8_t  pending_dir;        // Provisional reversal direction: +1, -1, 0(none)
    uint8_t pending_count;      // Consecutive raw counts seen in pending_dir
    int32_t reported_val;       // Last debounced value reported to the user
    int32_t last_raw;           // Last clamped raw sample used for debounce step counting
    bool active;                // Instance is initialized and running
    // IRQ notification fields
    encoder_irq_cb_t irq_cb;   // User callback (NULL = disabled)
    void *irq_param;            // Parameter passed to callback
    int8_t irq_a;               // NVIC IRQ number for Compare Match A (-1 = none)
    int8_t irq_b;               // NVIC IRQ number for Compare Match B (-1 = none)
    // Compare-window IRQ redesign:
    // Notification fires when GTCNT leaves [irq_anchor - irq_step, irq_anchor + irq_step].
    uint32_t irq_step;          // Compare quantum in counts (>= 1)
    int32_t  irq_anchor;        // Current window center
    uint32_t irq_mask;          // 0xFFFF for 16-bit GPT, else 0xFFFFFFFF
} encoder_config_t;

// Diagnostic snapshot of raw GPT registers
typedef struct {
    uint32_t gtcnt;    // Raw counter value
    uint32_t gtst;     // Status register (bit15=TUCF direction flag)
    uint32_t gtupsr;   // Up count source register
    uint32_t gtdnsr;   // Down count source register
    uint32_t gtcr;     // Control register (CST, MD, TPCS)
    uint32_t gtior;    // I/O control register (noise filter bits)
    uint32_t gtccra;   // Compare Match A register
    uint32_t gtccrb;   // Compare Match B register
    uint32_t irq_step; // Software compare-window step size
    int32_t irq_anchor;// Software compare-window center
} encoder_status_t;

// ──── API ────

// Find GPT channel from a pair of pins. Returns true if valid pair found.
bool ra_encoder_find_channel(uint32_t pin_a, uint32_t pin_b, uint32_t *ch);

// Initialize encoder on given GPT channel with specified pins and mode.
// Returns true on success.
bool ra_encoder_init(encoder_config_t *cfg);

// Read current counter value (GTCNT), apply scale and clamp to [min, max].
int32_t ra_encoder_read(encoder_config_t *cfg);

// Read the current hardware counter value from GTCNT.
// Returns the signed raw count (sign-extended for 16-bit GPT), with no
// debounce or software clamp applied.
int32_t ra_encoder_read_hw_count(encoder_config_t *cfg);

// Reset counter to given value.
void ra_encoder_reset(encoder_config_t *cfg, int32_t value);

// Reconfigure min/max limits.
void ra_encoder_config(encoder_config_t *cfg, int32_t min_val, int32_t max_val);

// Deinitialize encoder, release GPT channel and pins.
void ra_encoder_deinit(encoder_config_t *cfg);

// Enable compare-match IRQ notification for encoder movement.
// irq_a/irq_b: NVIC vector indices assigned to GPT Compare Match A/B events.
// cb: callback invoked from ISR context on any encoder movement.
// param: opaque parameter passed to callback.
void ra_encoder_irq_enable(encoder_config_t *cfg, int8_t irq_a, int8_t irq_b,
    encoder_irq_cb_t cb, void *param);

// Disable compare-match IRQ notification.
void ra_encoder_irq_disable(encoder_config_t *cfg);

// Read raw GPT register snapshot for diagnostics.
void ra_encoder_status(encoder_config_t *cfg, encoder_status_t *status);

// ISR handlers — called from vector table entries for GPT Compare Match A/B.
void encoder_compare_a_isr(void);
void encoder_compare_b_isr(void);

#endif /* RA_RA_ENCODER_H_ */
