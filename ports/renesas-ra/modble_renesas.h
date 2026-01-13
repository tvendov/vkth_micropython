/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Renesas Electronics Corporation
 */

#ifndef MICROPY_INCLUDED_RENESAS_RA_MODBLE_RENESAS_H
#define MICROPY_INCLUDED_RENESAS_RA_MODBLE_RENESAS_H

#include "py/obj.h"

// Process BLE events from main loop
void modble_renesas_process_events(void);

#endif // MICROPY_INCLUDED_RENESAS_RA_MODBLE_RENESAS_H

