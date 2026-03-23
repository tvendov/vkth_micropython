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

#include <string.h>
#include "hal_data.h"
#include "ra_config.h"
#include "ra_gpio.h"
#include "ra_utils.h"
#include "ra_gpt.h"
#include "ra_encoder.h"
#include "bsp_api.h"

// Direct IRQ→cfg lookup table for O(1) ISR dispatch.
// Indexed by NVIC IRQ number. Populated by irq_enable, cleared by irq_disable.
// Size = BSP_VECTOR_TABLE_MAX_ENTRIES (112 for RA4M2) — 448 bytes RAM.
// This replaces the previous O(N) scan in enc_irq_find_by_irq().
static encoder_config_t *enc_irq_lut[BSP_VECTOR_TABLE_MAX_ENTRIES] = {0};

// ---- GPT register base pointers (same layout as ra_gpt.c) ----

static R_GPT0_Type *enc_gpt_regs[RA_ENCODER_MAX_CH] = {
#if defined(RA4M1)
    R_GPT0, R_GPT1, R_GPT2, R_GPT3, R_GPT4, R_GPT5, R_GPT6, R_GPT7,
#elif defined(RA4M2)
    R_GPT0, R_GPT1, R_GPT2, R_GPT3, R_GPT4, R_GPT5, R_GPT6, R_GPT7,
#elif defined(RA4W1)
    R_GPT0, R_GPT1, R_GPT2, R_GPT3, R_GPT4, R_GPT5, NULL, NULL, R_GPT8,
#elif defined(RA6M1)
    R_GPT0, R_GPT1, R_GPT2, R_GPT3, R_GPT4, R_GPT5, R_GPT6, R_GPT7,
    R_GPT8, R_GPT9, R_GPT10, R_GPT11, R_GPT12,
#elif defined(RA6M2) || defined(RA6M3)
    R_GPT0, R_GPT1, R_GPT2, R_GPT3, R_GPT4, R_GPT5, R_GPT6, R_GPT7,
    R_GPT8, R_GPT9, R_GPT10, R_GPT11, R_GPT12, R_GPT13,
#elif defined(RA6M5)
    R_GPT0, R_GPT1, R_GPT2, R_GPT3, R_GPT4, R_GPT5, R_GPT6, R_GPT7,
    R_GPT8, R_GPT9,
#endif
};

// ---- 16-bit vs 32-bit GPT channel detection ----
// RA4M2: GPT320 ch0-3 = 32-bit, GPT164 ch4-7 = 16-bit
// Other MCUs: adjust as needed
static inline bool enc_is_16bit(uint32_t ch) {
    #if defined(RA4M2) || defined(RA4M1)
    return (ch >= 4);
    #elif defined(RA4W1)
    return (ch >= 4);  // GPT164: ch4,5,8
    #elif defined(RA6M1) || defined(RA6M2) || defined(RA6M3)
    return (ch >= 8);
    #elif defined(RA6M5)
    return (ch >= 4);
    #else
    return false;
    #endif
}

// Sign-extend 16-bit counter value to int32_t
static inline int32_t enc_sign_extend_16(uint32_t raw) {
    if (raw & 0x8000) {
        return (int32_t)(raw | 0xFFFF0000);
    }
    return (int32_t)(raw & 0xFFFF);
}

static inline void enc_reset_debounce_state(encoder_config_t *cfg, int32_t value) {
    cfg->value = value;
    cfg->reported_val = value;
    cfg->last_raw = value;
    cfg->confirmed_dir = 0;
    cfg->pending_dir = 0;
    cfg->pending_count = 0;
}

// ---- Clock enable/disable helpers ----

static void enc_clock_enable(uint32_t ch) {
    #ifdef RA4M1
    if (ch <= 1) {
    #elif defined(RA4M2)
    if (ch <= 7) {
    #elif defined(RA4W1)
    if (ch <= 3) {
    #elif defined(RA6M1) || defined(RA6M2) || defined(RA6M3)
    if (ch <= 7) {
    #elif defined(RA6M5)
    if (ch <= 9) {
    #else
    #error Choose proper clock enable BIT!
    #endif
        #if defined(RA4M2) || defined(RA6M5)
        ra_mstpcre_start(1UL << (31 - ch));
        #else
        ra_mstpcrd_start(R_MSTP_MSTPCRD_MSTPD5_Msk);
        #endif
    } else {
        ra_mstpcrd_start(R_MSTP_MSTPCRD_MSTPD6_Msk);
    }
}

static void enc_clock_disable(uint32_t ch) {
    #ifdef RA4M1
    if (ch <= 1) {
    #elif defined(RA4M2)
    if (ch <= 7) {
    #elif defined(RA4W1)
    if (ch <= 3) {
    #elif defined(RA6M1) || defined(RA6M2) || defined(RA6M3)
    if (ch <= 7) {
    #elif defined(RA6M5)
    if (ch <= 9) {
    #else
    #error Choose proper clock enable BIT!
    #endif
        #if defined(RA4M2) || defined(RA6M5)
        ra_mstpcre_stop(1UL << (31 - ch));
        #else
        ra_mstpcrd_stop(R_MSTP_MSTPCRD_MSTPD5_Msk);
        #endif
    } else {
        ra_mstpcrd_stop(R_MSTP_MSTPCRD_MSTPD6_Msk);
    }
}

// ---- API Implementation ----

bool ra_encoder_find_channel(uint32_t pin_a, uint32_t pin_b, uint32_t *ch) {
    uint32_t ch_a, af_a, ch_b, af_b;

    if (!ra_gpt_find_pin_channel(pin_a, &ch_a, &af_a)) {
        return false;
    }
    if (!ra_gpt_find_pin_channel(pin_b, &ch_b, &af_b)) {
        return false;
    }
    if (ch_a != ch_b) {
        return false;  // Both pins must be on the same GPT channel
    }
    if (ch_a >= RA_ENCODER_MAX_CH || enc_gpt_regs[ch_a] == NULL) {
        return false;
    }
    *ch = ch_a;
    return true;
}

bool ra_encoder_init(encoder_config_t *cfg) {
    uint32_t ch = cfg->gpt_ch;

    if (ch >= RA_ENCODER_MAX_CH || enc_gpt_regs[ch] == NULL) {
        return false;
    }

    R_GPT0_Type *gpt = enc_gpt_regs[ch];
    uint32_t af_a, af_b, dummy_ch;

    // Get AF values for pin configuration
    if (!ra_gpt_find_pin_channel(cfg->pin_a, &dummy_ch, &af_a)) {
        return false;
    }
    if (!ra_gpt_find_pin_channel(cfg->pin_b, &dummy_ch, &af_b)) {
        return false;
    }

    // Enable module clock
    enc_clock_enable(ch);

    // Configure GTUPSR/GTDNSR for phase counting mode
    uint32_t upsr, dnsr;
    switch (cfg->mode) {
        case ENCODER_MODE_X1:
            upsr = ENCODER_X1_UP;
            dnsr = ENCODER_X1_DN;
            break;
        case ENCODER_MODE_X2:
            upsr = ENCODER_X2_UP;
            dnsr = ENCODER_X2_DN;
            break;
        case ENCODER_MODE_X4:
        default:
            upsr = ENCODER_X4_UP;
            dnsr = ENCODER_X4_DN;
            break;
    }

    // Build GTIOR as a single 32-bit value to avoid spurious edges.
    // Manual warns: "changing the value of the [NFAEN] bit might lead to the
    // internal generation of an unexpected edge" — so we must NOT do
    // individual RMW bitfield writes (each one triggers a separate edge risk).
    // OAE=0, OBE=0 (inputs, not outputs).
    uint32_t gtior = 0;
    if (cfg->filter != ENCODER_FILTER_NONE) {
        uint32_t nf_clk = (uint32_t)(cfg->filter - 1);  // 0=PCLKD/1, 1=PCLKD/4, 2=PCLKD/16, 3=PCLKD/64
        // NFAEN=bit13, NFCSA=bits[15:14], NFBEN=bit29, NFCSB=bits[31:30]
        gtior |= (1U << 13) | (nf_clk << 14);   // Filter A
        gtior |= (1U << 29) | (nf_clk << 30);   // Filter B
    }

    // Unlock write protection (GTWP) before writing ANY protected register.
    // GTCR, GTUPSR, GTDNSR, GTPR, GTPBR, GTIOR, GTSSR, GTPSR, GTCSR,
    // GTUDDTYC are ALL write-protected by GTWP.
    gpt->GTWP = 0xA500U;  // Unlock

    // Stop counter and set saw-wave mode with TPCS=0 in one write.
    // GTCR is write-protected — must be inside GTWP unlock region!
    gpt->GTCR = 0;  // CST=0, MD=0 (saw-wave), TPCS=0 (PCLKD/1)

    // Clear status register — remove stale TUCF (direction flag),
    // overflow/underflow flags from previous use (FSP does this too).
    gpt->GTST = 0;

    // Clear counter before configuring (FSP-compatible order)
    gpt->GTCNT = 0;

    // Set start/stop/clear source registers: bit31 = software control enable.
    // FSP sets 0x80000000 to allow GTSTR/GTSTP/GTCLR software control.
    // Lower bits = 0 to disable all hardware triggers on encoder pins.
    gpt->GTSSR = 0x80000000U;
    gpt->GTPSR = 0x80000000U;
    gpt->GTCSR = 0x80000000U;

    // Disable input capture sources (prevent stale triggers)
    gpt->GTICASR = 0;
    gpt->GTICBSR = 0;

    // Configure up/down count sources for phase counting mode
    gpt->GTUPSR = upsr;
    gpt->GTDNSR = dnsr;

    // Set period to max (free-running counter)
    uint32_t max_count = enc_is_16bit(ch) ? 0xFFFF : 0xFFFFFFFF;
    gpt->GTPR = max_count;
    gpt->GTPBR = max_count;

    // Clear ADC trigger and dead time control (prevent interference)
    gpt->GTINTAD = 0;
    gpt->GTDTCR = 0;

    // Set I/O control (noise filters) — single atomic write to avoid
    // spurious edges from individual bitfield modifications.
    gpt->GTIOR = gtior;

    // Set GTUDDTYC: for phase counting this is "invalid" per manual,
    // but FSP still forces count direction with UDF+UD sequence.
    // We clear it to neutral state.
    gpt->GTUDDTYC = 0x00000003U;  // UDF=1, UD=1 (force up)
    gpt->GTUDDTYC = 0x00000001U;  // UDF=0, UD=1 (release force)

    // Set initial counter value (counter is stopped — CST=0 above)
    gpt->GTCNT = (uint32_t)cfg->init_val;

    // Configure pins as peripheral INPUT for GPT phase counting with pull-up.
    ra_gpio_config(cfg->pin_a, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_LOW_POWER, af_a);
    ra_gpio_config(cfg->pin_b, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_LOW_POWER, af_b);

    // Set initial state
    enc_reset_debounce_state(cfg, cfg->init_val);
    cfg->active = true;
    cfg->irq_cb = NULL;
    cfg->irq_param = NULL;
    cfg->irq_a = -1;
    cfg->irq_b = -1;

    // Start counter — GTCR is write-protected, must be before GTWP lock!
    gpt->GTCR = 1;  // CST=1, MD=0, TPCS=0 — full write, no RMW

    // Re-lock write protection
    gpt->GTWP = 0xA501U;  // Lock

    return true;
}


int32_t ra_encoder_read(encoder_config_t *cfg) {
    if (!cfg->active) {
        return cfg->value;
    }

    R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];
    int32_t raw;

    // Sign-extend for 16-bit GPT channels
    if (enc_is_16bit(cfg->gpt_ch)) {
        raw = enc_sign_extend_16(gpt->GTCNT);
    } else {
        raw = (int32_t)gpt->GTCNT;
    }

    // Software-only clamp to [min_val, max_val].
    // read() is a pure getter — it does NOT write back to GTCNT.
    // Use ra_encoder_reset() if you need to explicitly set the HW counter.
    if (raw < cfg->min_val) {
        raw = cfg->min_val;
    } else if (raw > cfg->max_val) {
        raw = cfg->max_val;
    }

    // Software debounce: on reversal, keep the previous reported position
    // until we confirm `debounce` consecutive raw counts in the new direction.
    // NOTE: when debounce > 0, the returned value is the "debounced reported
    // position" (reported_val), not the raw hardware count. This is the
    // intended API behavior for UI knobs — enc.value() returns a stable,
    // debounced position suitable for menu navigation etc.
    if (cfg->debounce > 0) {
        int32_t delta = raw - cfg->last_raw;
        if (delta == 0) {
            cfg->value = cfg->reported_val;
            return cfg->reported_val;
        }
        cfg->last_raw = raw;

        int8_t new_dir = (delta > 0) ? 1 : -1;
        uint32_t step_count = (uint32_t)((delta > 0) ? delta : -delta);

        if (cfg->confirmed_dir == 0 || new_dir == cfg->confirmed_dir) {
            // Same direction or first move — accept immediately
            cfg->confirmed_dir = new_dir;
            cfg->pending_dir = 0;
            cfg->pending_count = 0;
            cfg->reported_val = raw;
        } else {
            // Reversal: accumulate consecutive raw counts in the new direction.
            if (cfg->pending_dir != new_dir) {
                cfg->pending_dir = new_dir;
                cfg->pending_count = 0;
            }

            uint32_t pending = (uint32_t)cfg->pending_count + step_count;
            if (pending > UINT8_MAX) {
                pending = UINT8_MAX;
            }
            cfg->pending_count = (uint8_t)pending;

            if (cfg->pending_count >= cfg->debounce) {
                cfg->confirmed_dir = new_dir;
                cfg->reported_val = raw;
                cfg->pending_dir = 0;
                cfg->pending_count = 0;
            }
            // else: ignore bounce, keep reported_val unchanged
        }
        cfg->value = cfg->reported_val;
        return cfg->reported_val;
    }

    cfg->value = raw;
    cfg->reported_val = raw;
    cfg->last_raw = raw;
    cfg->confirmed_dir = 0;
    cfg->pending_dir = 0;
    cfg->pending_count = 0;
    return raw;
}

int32_t ra_encoder_read_hw_count(encoder_config_t *cfg) {
    if (cfg == NULL || !cfg->active) {
        return (cfg != NULL) ? cfg->value : 0;
    }
    R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];
    uint32_t raw = gpt->GTCNT;
    if (enc_is_16bit(cfg->gpt_ch)) {
        return (int32_t)(int16_t)(raw & 0xFFFFu);
    }
    return (int32_t)raw;
}

void ra_encoder_reset(encoder_config_t *cfg, int32_t value) {
    if (!cfg->active) {
        return;
    }

    R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];

    // Clamp reset value to bounds
    if (value < cfg->min_val) {
        value = cfg->min_val;
    } else if (value > cfg->max_val) {
        value = cfg->max_val;
    }

    // GTCR is write-protected — must unlock GTWP before stop/start.
    gpt->GTWP = 0xA500U;
    gpt->GTCR = 0;  // CST=0 (stop counter)
    gpt->GTCNT = (uint32_t)value;
    enc_reset_debounce_state(cfg, value);
    gpt->GTCR = 1;  // CST=1 (restart counter)
    gpt->GTWP = 0xA501U;
}

void ra_encoder_config(encoder_config_t *cfg, int32_t min_val, int32_t max_val) {
    cfg->min_val = min_val;
    cfg->max_val = max_val;
}

void ra_encoder_status(encoder_config_t *cfg, encoder_status_t *status) {
    if (!cfg->active) {
        memset(status, 0, sizeof(*status));
        return;
    }
    R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];
    status->gtcnt = gpt->GTCNT;
    status->gtst  = gpt->GTST;
    status->gtupsr = gpt->GTUPSR;
    status->gtdnsr = gpt->GTDNSR;
    status->gtcr  = gpt->GTCR;
    status->gtior = gpt->GTIOR;
    status->gtccra = gpt->GTCCR[0];
    status->gtccrb = gpt->GTCCR[1];
    status->irq_step = cfg->irq_step;
    status->irq_anchor = cfg->irq_anchor;
}

void ra_encoder_deinit(encoder_config_t *cfg) {
    if (!cfg->active) {
        return;
    }

    // Disable IRQ notification before tearing down hardware
    ra_encoder_irq_disable(cfg);

    uint32_t ch = cfg->gpt_ch;
    R_GPT0_Type *gpt = enc_gpt_regs[ch];

    // Unlock write protection FIRST, then stop counter.
    // GTCR is write-protected — must unlock before CST=0!
    gpt->GTWP = 0xA500U;

    // Stop counter
    gpt->GTCR = 0;  // CST=0, clear all GTCR fields

    // Clear status register
    gpt->GTST = 0;

    // Clear phase counting sources
    gpt->GTUPSR = 0;
    gpt->GTDNSR = 0;

    // Clear start/stop/clear source registers
    gpt->GTSSR = 0;
    gpt->GTPSR = 0;
    gpt->GTCSR = 0;

    // Clear input capture sources
    gpt->GTICASR = 0;
    gpt->GTICBSR = 0;

    // Disable noise filters (single write, no RMW)
    gpt->GTIOR = 0;

    // Clear ADC trigger and dead time control
    gpt->GTINTAD = 0;
    gpt->GTDTCR = 0;

    // Clear direction setting
    gpt->GTUDDTYC = 0x00000001U;  // UD=1 (up), UDF=0

    // Reset period
    gpt->GTPR = 0;
    gpt->GTPBR = 0;

    // Reset counter (counter is stopped, GTCNT is write-protected by GTWP)
    gpt->GTCNT = 0;

    // Re-lock write protection
    gpt->GTWP = 0xA501U;

    // Release pins back to GPIO
    ra_gpio_config(cfg->pin_a, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
    ra_gpio_config(cfg->pin_b, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);

    // Disable module clock
    enc_clock_disable(ch);

    cfg->active = false;
}

// ---- Compare-window IRQ helpers ----

static inline uint32_t enc_irq_mask_for_cfg(encoder_config_t *cfg) {
    return enc_is_16bit(cfg->gpt_ch) ? 0xFFFFu : 0xFFFFFFFFu;
}

static inline int32_t enc_irq_read_count(encoder_config_t *cfg) {
    R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];
    uint32_t raw = gpt->GTCNT;
    if (enc_is_16bit(cfg->gpt_ch)) {
        return (int32_t)(int16_t)(raw & 0xFFFFu);
    }
    return (int32_t)raw;
}

// Clear compare-match A/B flags in GTST.
// IMPORTANT: caller must hold GTWP unlocked (0xA500) — this function
// does NOT manage GTWP itself to avoid redundant unlock/lock pairs.
//
// GTST W0C semantics (RA4M2 manual §21.2.16, Note 1):
//   "Only 0 can be written to [flag] bits. Do not write 1."
//   Writing 0 clears the flag; writing 1 is a no-op but discouraged.
//   Reserved bits (14:8, 23:16, 28:25) "write value should be 0".
//   Read-only bits (TUCF, ODF, OABHF, OABLF) ignore writes.
//
// We write 0 to the entire register. This:
//   - Clears TCFA, TCFB (and all other W0C flags: TCFC-F, TCFPO/PU, PCF)
//   - Writes 0 to reserved bits (as required)
//   - Writes to R-only bits are ignored by hardware
// For encoder use we don't need TCFC-F/overflow/underflow flags,
// so clearing them all is safe and avoids RMW races in ISR context.
static inline void enc_irq_clear_flags(R_GPT0_Type *gpt) {
    gpt->GTST = 0;
}

// Rearm the compare window around a new anchor position.
// Performs a single GTWP unlock/lock region covering GTCCRA, GTCCRB,
// and GTST flag clear — all three are write-protected by GTWP.WP.
static inline void enc_irq_rearm_window(encoder_config_t *cfg, int32_t anchor) {
    R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];
    uint32_t mask = cfg->irq_mask;
    uint32_t step = cfg->irq_step;

    cfg->irq_anchor = anchor;

    // --- Begin GTWP-protected region ---
    gpt->GTWP = 0xA500U;  // Unlock (WP=0)

    // Window edges: A = anchor + step, B = anchor - step
    gpt->GTCCR[0] = ((uint32_t)anchor + step) & mask;  // GTCCRA
    gpt->GTCCR[1] = ((uint32_t)anchor - step) & mask;  // GTCCRB

    // Clear compare flags so next match triggers a fresh interrupt
    enc_irq_clear_flags(gpt);

    gpt->GTWP = 0xA501U;  // Re-lock (WP=1)
    // --- End GTWP-protected region ---
}

// ---- Compare Match IRQ notification ----

// Window-based ISR: instead of CNT±1 (which races), we maintain
// a window [anchor - step, anchor + step]. When GTCNT exits this
// window, the ISR catches anchor up in step-sized chunks and rearms.
//
// Sequence:
//   1. Clear NVIC pending (R_BSP_IrqStatusClear)
//   2. Read GTCNT, compute new anchor
//   3. Rearm compare window — enc_irq_rearm_window() handles:
//      GTWP unlock → GTCCRA/B update → GTST flag clear → GTWP lock
//   4. Deliver coarse movement notification to upper layer
//
// All GTWP-protected register writes happen inside enc_irq_rearm_window().
// Compare flags are NOT cleared separately in this ISR — rearm does it.
// O(1) IRQ→cfg lookup via enc_irq_lut[]. Populated by irq_enable/disable.
static inline encoder_config_t *enc_irq_find_by_irq(IRQn_Type irq) {
    if ((uint32_t)irq < BSP_VECTOR_TABLE_MAX_ENTRIES) {
        return enc_irq_lut[(uint32_t)irq];
    }
    return NULL;
}

static void enc_compare_common_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    R_BSP_IrqStatusClear(irq);

    encoder_config_t *cfg = enc_irq_find_by_irq(irq);
    if (cfg == NULL) {
        return;
    }

    int32_t cnt = enc_irq_read_count(cfg);
    int32_t anchor = cfg->irq_anchor;
    int32_t step = (int32_t)(cfg->irq_step ? cfg->irq_step : 1);

    // Signed delta from current window center
    int32_t delta = cnt - anchor;

    // Catch up anchor in chunks of irq_step
    if (delta >= step) {
        int32_t n = delta / step;
        anchor += n * step;
    } else if (delta <= -step) {
        int32_t n = (-delta) / step;
        anchor -= n * step;
    } else {
        // Near boundary — nudge anchor one step in the direction of movement
        if (delta > 0) {
            anchor += step;
        } else if (delta < 0) {
            anchor -= step;
        }
        // delta == 0: leave anchor unchanged (spurious match)
    }

    // Rearm window (GTWP unlock/lock + GTCCR update + GTST clear all inside)
    enc_irq_rearm_window(cfg, anchor);

    // Deliver one coarse movement notification to upper layer
    cfg->irq_cb(cfg->irq_param);
}

void encoder_compare_a_isr(void) {
    enc_compare_common_isr();
}

void encoder_compare_b_isr(void) {
    enc_compare_common_isr();
}

void ra_encoder_irq_enable(encoder_config_t *cfg, int8_t irq_a, int8_t irq_b,
    encoder_irq_cb_t cb, void *param) {

    if (!cfg->active || cb == NULL) {
        return;
    }

    // Disable previous IRQ if any
    ra_encoder_irq_disable(cfg);

    // Store callback and IRQ info
    cfg->irq_cb = cb;
    cfg->irq_param = param;
    cfg->irq_a = irq_a;
    cfg->irq_b = irq_b;
    cfg->irq_mask = enc_irq_mask_for_cfg(cfg);

    // Default compare quantum: notify on every raw count for all modes.
    // This keeps slow movement responsive; callers that want coarser events
    // can coalesce them in software at a higher layer.
    cfg->irq_step = 1;

    // Initialize window around current count
    cfg->irq_anchor = enc_irq_read_count(cfg);

    // Populate direct IRQ→cfg lookup table for O(1) ISR dispatch
    if (irq_a >= 0 && (uint32_t)irq_a < BSP_VECTOR_TABLE_MAX_ENTRIES) {
        enc_irq_lut[(uint32_t)irq_a] = cfg;
    }
    if (irq_b >= 0 && (uint32_t)irq_b < BSP_VECTOR_TABLE_MAX_ENTRIES) {
        enc_irq_lut[(uint32_t)irq_b] = cfg;
    }

    // Program initial compare window
    enc_irq_rearm_window(cfg, cfg->irq_anchor);

    // Configure and enable NVIC interrupts
    // Priority 12 (lower than SysTick=0, same area as other peripherals)
    const uint32_t priority = 12;

    if (irq_a >= 0) {
        R_BSP_IrqCfg((IRQn_Type)irq_a, priority, NULL);
        R_BSP_IrqEnable((IRQn_Type)irq_a);
    }
    if (irq_b >= 0) {
        R_BSP_IrqCfg((IRQn_Type)irq_b, priority, NULL);
        R_BSP_IrqEnable((IRQn_Type)irq_b);
    }
}

void ra_encoder_irq_disable(encoder_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }

    // Disable NVIC interrupts and clear IRQ→cfg LUT entries
    if (cfg->irq_a >= 0) {
        R_BSP_IrqDisable((IRQn_Type)cfg->irq_a);
        if ((uint32_t)cfg->irq_a < BSP_VECTOR_TABLE_MAX_ENTRIES) {
            enc_irq_lut[(uint32_t)cfg->irq_a] = NULL;
        }
        cfg->irq_a = -1;
    }
    if (cfg->irq_b >= 0) {
        R_BSP_IrqDisable((IRQn_Type)cfg->irq_b);
        if ((uint32_t)cfg->irq_b < BSP_VECTOR_TABLE_MAX_ENTRIES) {
            enc_irq_lut[(uint32_t)cfg->irq_b] = NULL;
        }
        cfg->irq_b = -1;
    }

    // Clear compare flags if hardware is still active.
    // enc_irq_clear_flags requires GTWP unlocked — do it here.
    if (cfg->active) {
        R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];
        gpt->GTWP = 0xA500U;  // Unlock
        enc_irq_clear_flags(gpt);
        gpt->GTWP = 0xA501U;  // Re-lock
    }

    cfg->irq_cb = NULL;
    cfg->irq_param = NULL;
    cfg->irq_step = 0;
    cfg->irq_anchor = 0;
    cfg->irq_mask = 0;
}
