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

// Singleton pointer used by ISRs to find the active encoder config.
// Only one encoder instance is supported at a time.
static encoder_config_t *enc_irq_cfg = NULL;

// ---- GPT register base pointers (same layout as ra_gpt.c) ----

#if defined(RA4M1)
#define ENC_GPT_CH_SIZE 8
#elif defined(RA4M2)
#define ENC_GPT_CH_SIZE 8
#elif defined(RA4W1)
#define ENC_GPT_CH_SIZE 9
#elif defined(RA6M1)
#define ENC_GPT_CH_SIZE 13
#elif defined(RA6M2) || defined(RA6M3)
#define ENC_GPT_CH_SIZE 14
#elif defined(RA6M5)
#define ENC_GPT_CH_SIZE 10
#else
#error "CMSIS MCU Series is not specified."
#endif

static R_GPT0_Type *enc_gpt_regs[ENC_GPT_CH_SIZE] = {
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
    if (ch_a >= ENC_GPT_CH_SIZE || enc_gpt_regs[ch_a] == NULL) {
        return false;
    }
    *ch = ch_a;
    return true;
}

bool ra_encoder_init(encoder_config_t *cfg) {
    uint32_t ch = cfg->gpt_ch;

    if (ch >= ENC_GPT_CH_SIZE || enc_gpt_regs[ch] == NULL) {
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
    cfg->value = cfg->init_val;
    cfg->reported_val = cfg->init_val;
    cfg->confirmed_dir = 0;
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

    // Clamp to [min_val, max_val]
    // Manual: "Writing to the GTCNT register is prohibited during count operation."
    // GTCR is write-protected — must unlock GTWP before stop/start.
    if (raw < cfg->min_val) {
        raw = cfg->min_val;
        gpt->GTWP = 0xA500U;
        gpt->GTCR = 0;  // CST=0 (stop)
        gpt->GTCNT = (uint32_t)raw;
        gpt->GTCR = 1;  // CST=1 (start)
        gpt->GTWP = 0xA501U;
    } else if (raw > cfg->max_val) {
        raw = cfg->max_val;
        gpt->GTWP = 0xA500U;
        gpt->GTCR = 0;  // CST=0 (stop)
        gpt->GTCNT = (uint32_t)raw;
        gpt->GTCR = 1;  // CST=1 (start)
        gpt->GTWP = 0xA501U;
    }

    // Software debounce: ignore direction reversals shorter than threshold.
    // Requires `debounce` consecutive steps in the new direction to confirm.
    if (cfg->debounce > 0) {
        int32_t delta = raw - cfg->reported_val;
        if (delta == 0) {
            cfg->value = cfg->reported_val;
            return cfg->reported_val;
        }
        int8_t new_dir = (delta > 0) ? 1 : -1;

        if (cfg->confirmed_dir == 0 || new_dir == cfg->confirmed_dir) {
            // Same direction or first move — accept immediately
            cfg->confirmed_dir = new_dir;
            cfg->reported_val = raw;
        } else {
            // Direction reversal — accept only if |delta| > debounce
            int32_t abs_delta = (delta > 0) ? delta : -delta;
            if (abs_delta > (int32_t)cfg->debounce) {
                cfg->confirmed_dir = new_dir;
                cfg->reported_val = raw;
            }
            // else: ignore bounce, keep reported_val unchanged
        }
        cfg->value = cfg->reported_val;
        return cfg->reported_val;
    }

    cfg->value = raw;
    return raw;
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
    cfg->value = value;
    cfg->reported_val = value;
    cfg->confirmed_dir = 0;
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

    // Re-lock write protection
    gpt->GTWP = 0xA501U;

    // Reset counter (not write-protected, counter is stopped)
    gpt->GTCNT = 0;

    // Release pins back to GPIO
    ra_gpio_config(cfg->pin_a, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
    ra_gpio_config(cfg->pin_b, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);

    // Disable module clock
    enc_clock_disable(ch);

    cfg->active = false;
}

// ---- Compare Match IRQ notification ----

// Common ISR logic for both Compare Match A and Compare Match B.
// Reads current GTCNT, updates both compare registers to follow the counter,
// clears the status flags, and invokes the user callback.
static void enc_compare_common_isr(void) {
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    R_BSP_IrqStatusClear(irq);

    encoder_config_t *cfg = enc_irq_cfg;
    if (cfg == NULL || !cfg->active || cfg->irq_cb == NULL) {
        return;
    }

    R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];
    uint32_t cnt = gpt->GTCNT;
    uint32_t mask = enc_is_16bit(cfg->gpt_ch) ? 0xFFFF : 0xFFFFFFFF;

    // Update compare registers to track new position:
    // GTCCRA = CNT+1 (detect forward movement)
    // GTCCRB = CNT-1 (detect backward movement)
    gpt->GTCCR[0] = (cnt + 1) & mask;  // GTCCRA
    gpt->GTCCR[1] = (cnt - 1) & mask;  // GTCCRB

    // Clear both TCFA and TCFB flags (write 0 to clear, other bits preserved)
    gpt->GTST &= ~(R_GPT0_GTST_TCFA_Msk | R_GPT0_GTST_TCFB_Msk);

    // Invoke callback
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

    R_GPT0_Type *gpt = enc_gpt_regs[cfg->gpt_ch];
    uint32_t cnt = gpt->GTCNT;
    uint32_t mask = enc_is_16bit(cfg->gpt_ch) ? 0xFFFF : 0xFFFFFFFF;

    // Set initial compare values to detect first movement
    gpt->GTCCR[0] = (cnt + 1) & mask;  // GTCCRA = CNT+1
    gpt->GTCCR[1] = (cnt - 1) & mask;  // GTCCRB = CNT-1

    // Clear any pending flags
    gpt->GTST &= ~(R_GPT0_GTST_TCFA_Msk | R_GPT0_GTST_TCFB_Msk);

    // Store callback and IRQ info
    cfg->irq_cb = cb;
    cfg->irq_param = param;
    cfg->irq_a = irq_a;
    cfg->irq_b = irq_b;
    enc_irq_cfg = cfg;

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
    // Disable NVIC interrupts
    if (cfg->irq_a >= 0) {
        R_BSP_IrqDisable((IRQn_Type)cfg->irq_a);
        cfg->irq_a = -1;
    }
    if (cfg->irq_b >= 0) {
        R_BSP_IrqDisable((IRQn_Type)cfg->irq_b);
        cfg->irq_b = -1;
    }

    cfg->irq_cb = NULL;
    cfg->irq_param = NULL;

    if (enc_irq_cfg == cfg) {
        enc_irq_cfg = NULL;
    }
}