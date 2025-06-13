#pragma once
#include <stdint.h>
#include "r_ioport.h"
#include "bsp_pin_cfg.h"

// Analog pin definitions
static const uint32_t PIN_A0 = A0;
static const uint32_t PIN_A1 = A1;
static const uint32_t PIN_A2 = A2;
static const uint32_t PIN_A3 = A3;
static const uint32_t PIN_A4 = A4;
static const uint32_t PIN_A5 = A5;

static const uint8_t A0_INDEX = 0;
static const uint8_t A1_INDEX = 1;
static const uint8_t A2_INDEX = 2;
static const uint8_t A3_INDEX = 3;
static const uint8_t A4_INDEX = 4;
static const uint8_t A5_INDEX = 5;

// Digital pin definitions
static const uint32_t PIN_D0 = D0;
static const uint32_t PIN_D1 = D1;
static const uint32_t PIN_D2 = D2;
static const uint32_t PIN_D3 = D3;
static const uint32_t PIN_D4 = D4;
static const uint32_t PIN_D5 = D5;
static const uint32_t PIN_D6 = D6;
static const uint32_t PIN_D7 = D7;
static const uint32_t PIN_D8 = D8;
static const uint32_t PIN_D9 = D9;
static const uint32_t PIN_D10 = D10;
static const uint32_t PIN_D11 = D11;
static const uint32_t PIN_D12 = D12;
static const uint32_t PIN_D13 = D13;

#define NUM_DIGITAL_PINS    14
#define NUM_ANALOG_INPUTS    6

#define LED_BUILTIN LED_R
