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

/* 040/041 — "Python supplied a machine.SPI object" sentinel.
   Set true by lorawan_mac_make_new() AFTER it has either created or
   type-checked the MicroPython machine.SPI object and stashed the
   reference in lorawan_mac_obj_t::spi_obj. Read by
   sx126x_board_init() before any SPI bus access; if false, the board
   layer refuses to init and raises -MP_EIO.
   Caller contract:
     - `lorawan.LoRaWAN(spi_bus=3, clk=..., mosi=..., miso=..., ...)`
       creates a normal MicroPython machine.SPI object through the public
       constructor path, then roots it for the LoRaWAN lifetime.
     - callers may still pass `spi=machine.SPI(...)` as a shortcut.
     - Python/MicroPython owns the resource lifecycle and must NOT call
       spi.deinit() while LoRaWAN is active.
   What this flag does NOT prove:
     - for `spi=...`, it does NOT prove SPI id/baud/polarity/phase/pins;
     - it does NOT prove SCI9 is open or remains open;
     - it does NOT prevent user-driven spi.deinit().
   Runtime bus-state failures surface through the board-layer byte transfer
   path, which calls the standard MicroPython SPI protocol on this rooted
   object. The Renesas backend state remains private to `machine_spi.c`. */
extern bool lorawan_spi_pinned;

#endif // MICROPY_HW_LORA_STACK_RENESAS

#endif // MICROPY_INCLUDED_RENESAS_RA_LORAWAN_MOD_LORAWAN_H
