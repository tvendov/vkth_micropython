/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
 *
 * Renesas LoRaWAN C-stack — MicroPython binding header (Phase 0 skeleton).
 *
 * This module is built only when the renesas-ra port is configured with
 *   MICROPY_HW_LORA_STACK = renesas
 * via boards/<BOARD>/mpconfigboard.mk. See lorawan/README.md for details.
 */

#ifndef MICROPY_INCLUDED_RENESAS_RA_LORAWAN_MOD_LORAWAN_H
#define MICROPY_INCLUDED_RENESAS_RA_LORAWAN_MOD_LORAWAN_H

#include "py/runtime.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

// Phase status — incremented as the stack is brought up. Exposed as
// `lorawan._PHASE` for debug / progress tracking from Python.
//   0 = skeleton only
//   1 = radio HAL (SX126x board glue + DIO1/BUSY IRQ)
//   2 = timer service AGT4 + direct ICU + runtime tuning
//   3 = AGT5 sub-ms hand-off (RX1/RX2 ±200 µs)
//   4 = LoRaMac compiles (imported tree, no calls yet)
//   5 = LoRaMac binding: init + keys + process + join + send + recv
//   6 = ADR + NVM persistence (RAM-backed in 6a.1, flash in 6a.2)    ← current
//   7 = production
#define LORAWAN_PHASE       (6)

// Forward declarations for the Phase 0 stub Mac class.
extern const mp_obj_type_t lorawan_mac_type;

#endif // MICROPY_HW_LORA_STACK_RENESAS

#endif // MICROPY_INCLUDED_RENESAS_RA_LORAWAN_MOD_LORAWAN_H
