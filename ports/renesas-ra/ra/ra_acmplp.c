/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 MicroPython contributors
 *
 * ACMPLP (Low-Power Analog Comparator) driver for RA4M1
 * Reference: RA4M1 Hardware Manual, Chapter 35 - Low-Power Analog Comparator
 * Reference: R7FA4M1AB.h - R_ACMPLP Structure @ 0x40085E00
 */

#include "ra_acmplp.h"

#if defined(RA4M1)

// Use FSP register definitions
#include "hal_data.h"

// ACMPLP register access via R_ACMPLP from FSP
#define ACMPLP  R_ACMPLP

// MSTP bit for ACMPLP - MSTPCRC bit 28 (from datasheet)
#define ACMPLP_MSTP_BIT     (1UL << 28)

// Callback storage
static ra_acmplp_callback_t acmplp_callbacks[RA_ACMPLP_NUM_CHANNELS] = {NULL, NULL};

// Check if channel is valid
static inline bool is_valid_channel(uint8_t channel) {
    return channel < RA_ACMPLP_NUM_CHANNELS;
}

// Enable module clock
static void acmplp_module_start(void) {
    R_MSTP->MSTPCRC &= ~ACMPLP_MSTP_BIT;
    // Small delay for module to stabilize
    for (volatile int i = 0; i < 10; i++) {}
}

// Disable module clock
static void acmplp_module_stop(void) {
    R_MSTP->MSTPCRC |= ACMPLP_MSTP_BIT;
}

bool ra_acmplp_init(uint8_t channel, const ra_acmplp_config_t *config) {
    if (!is_valid_channel(channel) || config == NULL) {
        return false;
    }

    // Enable module clock
    acmplp_module_start();

    // Disable comparator before configuration
    ra_acmplp_disable(channel);

    if (channel == RA_ACMPLP_CH0) {
        // Configure input selection (COMPSEL0)
        uint8_t sel0 = ACMPLP->COMPSEL0;
        sel0 = (sel0 & 0xF8) | (config->input & 0x07);
        ACMPLP->COMPSEL0 = sel0;

        // Configure reference selection (COMPSEL1)
        uint8_t sel1 = ACMPLP->COMPSEL1;
        sel1 = (sel1 & 0xF8) | (config->reference & 0x07);
        ACMPLP->COMPSEL1 = sel1;

        // Configure filter and edge (COMPFIR)
        uint8_t fir = ACMPLP->COMPFIR;
        fir = (fir & 0xF0);  // Clear CH0 bits
        fir |= (config->filter & 0x03);  // C0FCK
        if (config->edge == RA_ACMPLP_EDGE_FALLING) {
            fir |= (1 << 2);  // C0EPO = 1
        }
        if (config->edge == RA_ACMPLP_EDGE_BOTH) {
            fir |= (1 << 3);  // C0EDG = 1
        }
        ACMPLP->COMPFIR = fir;

        // Configure output control (COMPOCR)
        uint8_t ocr = ACMPLP->COMPOCR;
        if (config->output_pin) {
            ocr |= (1 << 1);  // C0OE
        } else {
            ocr &= ~(1 << 1);
        }
        if (config->invert) {
            ocr |= (1 << 2);  // C0OP
        } else {
            ocr &= ~(1 << 2);
        }
        ACMPLP->COMPOCR = ocr;

        // Configure mode (COMPMDR)
        uint8_t mdr = ACMPLP->COMPMDR;
        if (config->window_mode) {
            mdr |= (1 << 1);  // C0WDE
        } else {
            mdr &= ~(1 << 1);
        }
        ACMPLP->COMPMDR = mdr;

    } else { // RA_ACMPLP_CH1
        // Configure input selection (COMPSEL0)
        uint8_t sel0 = ACMPLP->COMPSEL0;
        sel0 = (sel0 & 0x8F) | ((config->input & 0x07) << 4);
        ACMPLP->COMPSEL0 = sel0;

        // Configure reference selection (COMPSEL1)
        uint8_t sel1 = ACMPLP->COMPSEL1;
        sel1 = (sel1 & 0x0F) | ((config->reference & 0x07) << 4);
        ACMPLP->COMPSEL1 = sel1;

        // Configure filter and edge (COMPFIR)
        uint8_t fir = ACMPLP->COMPFIR;
        fir = (fir & 0x0F);  // Clear CH1 bits
        fir |= ((config->filter & 0x03) << 4);  // C1FCK
        if (config->edge == RA_ACMPLP_EDGE_FALLING) {
            fir |= (1 << 6);  // C1EPO
        }
        if (config->edge == RA_ACMPLP_EDGE_BOTH) {
            fir |= (1 << 7);  // C1EDG
        }
        ACMPLP->COMPFIR = fir;

        // Configure output control (COMPOCR)
        uint8_t ocr = ACMPLP->COMPOCR;
        if (config->output_pin) {
            ocr |= (1 << 5);  // C1OE
        } else {
            ocr &= ~(1 << 5);
        }
        if (config->invert) {
            ocr |= (1 << 6);  // C1OP
        } else {
            ocr &= ~(1 << 6);
        }
        ACMPLP->COMPOCR = ocr;

        // Configure mode (COMPMDR)
        uint8_t mdr = ACMPLP->COMPMDR;
        if (config->window_mode) {
            mdr |= (1 << 5);  // C1WDE
        } else {
            mdr &= ~(1 << 5);
        }
        ACMPLP->COMPMDR = mdr;
    }

    // Set speed mode (shared for both channels)
    ra_acmplp_set_speed(config->speed);

    return true;
}

void ra_acmplp_deinit(uint8_t channel) {
    if (!is_valid_channel(channel)) {
        return;
    }
    ra_acmplp_irq_disable(channel);
    ra_acmplp_disable(channel);

    // Check if both channels are disabled, then stop module
    uint8_t mdr = ACMPLP->COMPMDR;
    if ((mdr & 0x11) == 0) {  // C0ENB and C1ENB both 0
        acmplp_module_stop();
    }
}

void ra_acmplp_enable(uint8_t channel) {
    if (!is_valid_channel(channel)) {
        return;
    }
    uint8_t mdr = ACMPLP->COMPMDR;
    if (channel == RA_ACMPLP_CH0) {
        mdr |= (1 << 0);  // C0ENB
    } else {
        mdr |= (1 << 4);  // C1ENB
    }
    ACMPLP->COMPMDR = mdr;

    // Wait for stabilization (~300ns at high speed, ~3us at low power)
    for (volatile int i = 0; i < 100; i++) {}
}

void ra_acmplp_disable(uint8_t channel) {
    if (!is_valid_channel(channel)) {
        return;
    }
    uint8_t mdr = ACMPLP->COMPMDR;
    if (channel == RA_ACMPLP_CH0) {
        mdr &= ~(1 << 0);  // C0ENB = 0
    } else {
        mdr &= ~(1 << 4);  // C1ENB = 0
    }
    ACMPLP->COMPMDR = mdr;
}

bool ra_acmplp_get_output(uint8_t channel) {
    if (!is_valid_channel(channel)) {
        return false;
    }
    uint8_t mdr = ACMPLP->COMPMDR;
    if (channel == RA_ACMPLP_CH0) {
        return (mdr & (1 << 3)) != 0;  // C0MON
    } else {
        return (mdr & (1 << 7)) != 0;  // C1MON
    }
}

void ra_acmplp_set_speed(ra_acmplp_speed_t speed) {
    uint8_t ocr = ACMPLP->COMPOCR;
    if (speed == RA_ACMPLP_SPEED_HIGH) {
        ocr |= (1 << 7);  // SPDMD = 1
    } else {
        ocr &= ~(1 << 7);  // SPDMD = 0
    }
    ACMPLP->COMPOCR = ocr;
}

	/* IRQ handlers - to be connected via vector_data.c
	 * Only built for boards that allocate comparator interrupts in vector_data.h.
	 */
	#if defined(VECTOR_NUMBER_ACMPLP0_INT)
	void acmplp0_int_isr(void) {
	    /* Clear the ICU interrupt flag first, then dispatch the callback. */
	    R_BSP_IrqStatusClear(VECTOR_NUMBER_ACMPLP0_INT);
	    if (acmplp_callbacks[0] != NULL) {
	        acmplp_callbacks[0](0);
	    }
	}
	#endif

	#if defined(VECTOR_NUMBER_ACMPLP1_INT)
	void acmplp1_int_isr(void) {
	    R_BSP_IrqStatusClear(VECTOR_NUMBER_ACMPLP1_INT);
	    if (acmplp_callbacks[1] != NULL) {
	        acmplp_callbacks[1](1);
	    }
	}
	#endif

	bool ra_acmplp_irq_enable(uint8_t channel, ra_acmplp_callback_t callback) {
	    if (!is_valid_channel(channel)) {
	        return false;
	    }

	    acmplp_callbacks[channel] = callback;

	#if defined(VECTOR_NUMBER_ACMPLP0_INT) || defined(VECTOR_NUMBER_ACMPLP1_INT)
	    IRQn_Type irq;

	    if (channel == 0) {
	    #ifdef VECTOR_NUMBER_ACMPLP0_INT
	        irq = VECTOR_NUMBER_ACMPLP0_INT;
	    #else
	        return false;
	    #endif
	    } else {
	    #ifdef VECTOR_NUMBER_ACMPLP1_INT
	        irq = VECTOR_NUMBER_ACMPLP1_INT;
	    #else
	        return false;
	    #endif
	    }

	    /* Configure and enable the interrupt with a medium-low priority (12). */
	    R_BSP_IrqCfg(irq, 12, NULL);
	    R_BSP_IrqEnable(irq);

	    return true;
	#else
	    (void) callback;
	    return false;
	#endif
	}

	void ra_acmplp_irq_disable(uint8_t channel) {
	    if (!is_valid_channel(channel)) {
	        return;
	    }

	    acmplp_callbacks[channel] = NULL;

	#if defined(VECTOR_NUMBER_ACMPLP0_INT) || defined(VECTOR_NUMBER_ACMPLP1_INT)
	    IRQn_Type irq;

	    if (channel == 0) {
	    #ifdef VECTOR_NUMBER_ACMPLP0_INT
	        irq = VECTOR_NUMBER_ACMPLP0_INT;
	    #else
	        return;
	    #endif
	    } else {
	    #ifdef VECTOR_NUMBER_ACMPLP1_INT
	        irq = VECTOR_NUMBER_ACMPLP1_INT;
	    #else
	        return;
	    #endif
	    }

	    R_BSP_IrqDisable(irq);
	#else
	    (void) channel;
	#endif
	}

#endif // RA4M1

