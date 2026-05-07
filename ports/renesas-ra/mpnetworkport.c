/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Damien P. George
 * Copyright (c) 2023 Arduino SA
 * Copyright (c) 2023 Vekatech Ltd.
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "py/objlist.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "shared/netutils/netutils.h"
#include "systick.h"
#include "pendsv.h"
#include "extmod/modnetwork.h"

#if MICROPY_PY_LWIP

#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/dns.h"
#include "lwip/dhcp.h"
#include "lwip/apps/mdns.h"

#if defined(MICROPY_HW_ETH_MDC)
#include "hal_data.h"
#endif

// Poll lwIP every 128 ms.  Bit-mask trick: if (tick & 0x7f) == 0 -> divisor 128.
#define LWIP_TICK(tick) (((tick) & ~(SYSTICK_DISPATCH_NUM_SLOTS - 1) & 0x7f) == 0)

u32_t sys_now(void) {
    return mp_hal_ticks_ms();
}

static void pyb_lwip_poll(void) {
    // Run the lwIP internal updates (DHCP retries, ARP timeouts, TCP retransmit, ...).
    sys_check_timeouts();

    #if defined(MICROPY_HW_ETH_MDC)
    // Drive R_ETHER_LinkProcess only when the driver has been opened.  Calling
    // it before R_ETHER_Open dereferences a NULL p_ether_cfg pointer in some
    // FSP code paths.  eth_is_open() is set by eth_init() after a successful
    // R_ETHER_Open.
    extern bool eth_is_open(void);
    if (eth_is_open()) {
        (void)R_ETHER_LinkProcess(&g_ether0_ctrl);
    }
    #endif
}

void mod_network_lwip_poll_wrapper(uint32_t ticks_ms) {
    if (LWIP_TICK(ticks_ms)) {
        pendsv_schedule_dispatch(PENDSV_DISPATCH_LWIP, pyb_lwip_poll);
    }
}

void mod_network_lwip_init(void) {
    // Install systick hook so that pyb_lwip_poll runs from PendSV every 128 ms
    // independently of what the main Python thread is doing.
    systick_enable_dispatch(SYSTICK_DISPATCH_LWIP, mod_network_lwip_poll_wrapper);
}

void mod_network_poll_events(void) {
    pendsv_schedule_dispatch(PENDSV_DISPATCH_LWIP, pyb_lwip_poll);
}

#endif // MICROPY_PY_LWIP
