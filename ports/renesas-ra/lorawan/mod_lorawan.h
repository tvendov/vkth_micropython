/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
 *
 * Renesas LoRaWAN C-stack — MicroPython binding header.
 *
 * This module is built only when the renesas-ra port is configured with
 *   MICROPY_HW_LORA_STACK = renesas
 * via boards/<BOARD>/mpconfigboard.mk. See lorawan/README.md for details.
 */

#ifndef MICROPY_INCLUDED_RENESAS_RA_LORAWAN_MOD_LORAWAN_H
#define MICROPY_INCLUDED_RENESAS_RA_LORAWAN_MOD_LORAWAN_H

#include "py/runtime.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

// Forward declaration for the MicroPython Mac/LoRaWAN type.
extern const mp_obj_type_t lorawan_mac_type;

#endif // MICROPY_HW_LORA_STACK_RENESAS

#endif // MICROPY_INCLUDED_RENESAS_RA_LORAWAN_MOD_LORAWAN_H
