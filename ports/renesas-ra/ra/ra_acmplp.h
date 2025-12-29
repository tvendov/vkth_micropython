/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 MicroPython contributors
 *
 * ACMPLP (Low-Power Analog Comparator) driver for RA4M1
 * Reference: RA4M1 Hardware Manual, Chapter 35 - Low-Power Analog Comparator
 */

#ifndef RA_ACMPLP_H
#define RA_ACMPLP_H

#include <stdint.h>
#include <stdbool.h>

// Number of ACMPLP channels
#define RA_ACMPLP_NUM_CHANNELS  2

// Channel definitions
#define RA_ACMPLP_CH0           0
#define RA_ACMPLP_CH1           1

// Filter settings (COMPFIR CxFCK bits)
typedef enum {
    RA_ACMPLP_FILTER_OFF    = 0,    // No filter
    RA_ACMPLP_FILTER_PCLK8  = 1,    // PCLK/8
    RA_ACMPLP_FILTER_PCLK16 = 2,    // PCLK/16
    RA_ACMPLP_FILTER_PCLK32 = 3,    // PCLK/32
} ra_acmplp_filter_t;

// Edge/trigger settings
typedef enum {
    RA_ACMPLP_EDGE_RISING  = 0,     // Rising edge only
    RA_ACMPLP_EDGE_FALLING = 1,     // Falling edge only
    RA_ACMPLP_EDGE_BOTH    = 2,     // Both edges
} ra_acmplp_edge_t;

// Speed mode
typedef enum {
    RA_ACMPLP_SPEED_LOW  = 0,       // Low-power mode
    RA_ACMPLP_SPEED_HIGH = 1,       // High-speed mode
} ra_acmplp_speed_t;

// Input selection (IVCMP0/1)
typedef enum {
    RA_ACMPLP_INPUT_CMPIN0 = 0,
    RA_ACMPLP_INPUT_CMPIN1 = 1,
    RA_ACMPLP_INPUT_CMPIN2 = 2,
    RA_ACMPLP_INPUT_CMPIN3 = 3,
    RA_ACMPLP_INPUT_NONE   = 7,
} ra_acmplp_input_t;

// Reference voltage selection (IVREF0/1)
typedef enum {
    RA_ACMPLP_REF_CMPREF0  = 0,     // External reference pin
    RA_ACMPLP_REF_CMPREF1  = 1,
    RA_ACMPLP_REF_DAC8_CH0 = 2,     // Internal DAC8 channel 0
    RA_ACMPLP_REF_DAC8_CH1 = 3,     // Internal DAC8 channel 1
    RA_ACMPLP_REF_IVREF    = 4,     // Internal reference voltage
    RA_ACMPLP_REF_NONE     = 7,
} ra_acmplp_ref_t;

// Configuration structure
typedef struct {
    ra_acmplp_input_t  input;       // Positive input selection
    ra_acmplp_ref_t    reference;   // Reference voltage selection
    ra_acmplp_filter_t filter;      // Digital filter setting
    ra_acmplp_edge_t   edge;        // Edge detection for interrupt
    ra_acmplp_speed_t  speed;       // Speed mode
    bool               invert;      // Invert output polarity
    bool               output_pin;  // Enable output on VCOUT pin
    bool               window_mode; // Enable window mode
} ra_acmplp_config_t;

// Callback type for interrupt
typedef void (*ra_acmplp_callback_t)(uint8_t channel);

// API functions
bool ra_acmplp_init(uint8_t channel, const ra_acmplp_config_t *config);
void ra_acmplp_deinit(uint8_t channel);
void ra_acmplp_enable(uint8_t channel);
void ra_acmplp_disable(uint8_t channel);
bool ra_acmplp_get_output(uint8_t channel);
void ra_acmplp_set_speed(ra_acmplp_speed_t speed);
bool ra_acmplp_irq_enable(uint8_t channel, ra_acmplp_callback_t callback);
void ra_acmplp_irq_disable(uint8_t channel);

#endif // RA_ACMPLP_H

