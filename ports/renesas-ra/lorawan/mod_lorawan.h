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

/* mac.send() HardFault localization breadcrumbs. Each checkpoint writes
 * its enum ID into a .noinit RAM slot that survives a HardFault and a
 * subsequent JLink hard reset (RA4M2 has no D-cache, see linker .noinit
 * section in boards/VK_RA4M2/ra4m2ac3cfm.ld). Readable from Python via
 * lorawan._last_breadcrumb() after the next boot.
 *
 * Sites: S0..S5 in mod_lorawan.c lorawan_mac_send(); M0..M10 in
 * mac/LoRaMac.c LoRaMacMcpsRequest/Send/PrepareFrame/ScheduleTx/
 * SendFrameOnChannel. Foreground (synchronous mac.send) context only;
 * do NOT place inside any ISR (the .noinit write itself is safe, but the
 * breadcrumbs were placed to localize fg-call faults). */
#ifndef LORAWAN_SEND_BREADCRUMBS
#define LORAWAN_SEND_BREADCRUMBS (1)
#endif

typedef enum {
    LWBC_NONE = 0,
    LWBC_S0,
    LWBC_S1,
    LWBC_S2,
    LWBC_S3,
    LWBC_S4,
    LWBC_S5,
    LWBC_M0,
    LWBC_M1,
    LWBC_M2,
    LWBC_M3,
    LWBC_M4,
    LWBC_M5,
    LWBC_M6,
    LWBC_M7,
    LWBC_M8,
    LWBC_M9,
    LWBC_M10,
} lorawan_bc_t;

#define LORAWAN_BC_MAGIC (0xB12EAD51u)

extern uint16_t lorawan_bc_last;
extern uint32_t lorawan_bc_magic;

#if LORAWAN_SEND_BREADCRUMBS
#define SBC(id) (lorawan_bc_last = (uint16_t)(id))
#else
#define SBC(id) ((void)0)
#endif

// Forward declarations for the Phase 0 stub Mac class.
extern const mp_obj_type_t lorawan_mac_type;

#endif // MICROPY_HW_LORA_STACK_RENESAS

#endif // MICROPY_INCLUDED_RENESAS_RA_LORAWAN_MOD_LORAWAN_H
