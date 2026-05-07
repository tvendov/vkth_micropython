/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
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

#include <string.h>
#include "py/mphal.h"
#include "py/mperrno.h"
#include "shared/netutils/netutils.h"
#include "extmod/modnetwork.h"
#include "hal_data.h"
#include "pendsv.h"
#include "eth.h"

// Forward declaration: defined in mpnetworkport.c, runs from PendSV.
extern void pyb_lwip_poll(void);

#if defined(MICROPY_HW_ETH_MDC)

#include "lwip/etharp.h"
#include "lwip/dns.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"


#define ETHER_EXAMPLE_ETHER_ISR_EE_FR_MASK     (1UL << 18)
#define ETHER_EXAMPLE_ETHER_ISR_EE_TC_MASK     (1UL << 21)
#define ETHER_EXAMPLE_ETHER_ISR_EC_MPD_MASK    (1UL << 1)

typedef struct _eth_t {
    uint32_t trace_flags;
    struct netif netif;
    struct dhcp dhcp_struct;
} eth_t;

uint8_t tx_TMPbuf[1536] __attribute__((aligned(4))); /* g_ether0_cfg.ether_buffer_size */

// --- RX FIFO ------------------------------------------------------------
// Ring buffer of received frames, populated by the ETH ISR and drained by
// PendSV.  Decouples the two contexts so the ISR never touches lwIP, which
// keeps ETH IRQ time to a few microseconds even at peak traffic.
//
// Sized at 8 slots — twice the EDMAC RX descriptor count.  Gives the PendSV
// consumer headroom for back-to-back bursts (e.g. TCP ACK trains during
// stress-loops).  Empirically, 4 slots saw ~5% drops at 100 Mbps; 8 slots
// brings drops to 0 in the same load.
//
// Memory cost: 8 * 1538 = ~12 KB SRAM.  Could be moved to OSPI RAM if the
// extra latency proves acceptable, but not by default.
#define ETH_RX_FIFO_SLOTS  8
typedef struct {
    uint16_t len;
    uint8_t  buf[1536] __attribute__((aligned(4)));
} eth_rx_slot_t;
static eth_rx_slot_t eth_rx_fifo[ETH_RX_FIFO_SLOTS];
static volatile uint8_t eth_rx_head = 0;          // written by ISR
static volatile uint8_t eth_rx_tail = 0;          // written by PendSV
volatile uint32_t eth_rx_dropped = 0;             // diagnostic
volatile uint32_t eth_rx_high_water = 0;          // peak FIFO occupancy

eth_t eth_instance;
uint8_t phy_link_status = 0;
static bool eth_open_flag = false;
const machine_pin_obj_t *phy_RST = pin_P400;

bool eth_is_open(void) {
    return eth_open_flag;
}

// Diagnostic counters — bumped from ETH_IRQHandler.
volatile uint32_t eth_irq_events = 0;
volatile uint32_t eth_irq_link_on = 0;
volatile uint32_t eth_irq_link_off = 0;
volatile uint32_t eth_irq_interrupt = 0;
volatile uint32_t eth_irq_rx_frames = 0;
volatile uint32_t eth_irq_rx_failed = 0;
volatile uint32_t eth_last_eesr = 0;

// ETH-LwIP bindings

#define TRACE_ASYNC_EV (0x0001)
#define TRACE_ETH_TX (0x0002)
#define TRACE_ETH_RX (0x0004)
#define TRACE_ETH_FULL (0x0008)

static void eth_trace(eth_t *self, size_t len, const void *data, unsigned int flags) {
    if (((flags & NETUTILS_TRACE_IS_TX) && (self->trace_flags & TRACE_ETH_TX))
        || (!(flags & NETUTILS_TRACE_IS_TX) && (self->trace_flags & TRACE_ETH_RX))) {
        const uint8_t *buf;
        if (len == (size_t)-1) {
            // data is a pbuf
            const struct pbuf *pbuf = data;
            buf = pbuf->payload;
            len = pbuf->len; // restricted to print only the first chunk of the pbuf
        } else {
            // data is actual data buffer
            buf = data;
        }
        if (self->trace_flags & TRACE_ETH_FULL) {
            flags |= NETUTILS_TRACE_PAYLOAD;
        }
        netutils_ethernet_trace(MP_PYTHON_PRINTER, len, buf, flags);
    }
}

static void eth_process_frame(eth_t *self, size_t len, const uint8_t *buf) {
    eth_trace(self, len, buf, NETUTILS_TRACE_NEWLINE);

    struct netif *netif = &self->netif;
    if (netif->flags & NETIF_FLAG_LINK_UP) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        if (p != NULL) {
            pbuf_take(p, buf, len);
            if (netif->input(p, netif) != ERR_OK) {
                pbuf_free(p);
            }
        }
    }
}

static err_t eth_netif_output(struct netif *netif, struct pbuf *p) {
    // This function should always be called from a context where PendSV-level IRQs are disabled

    LINK_STATS_INC(link.xmit);
    eth_trace(netif->state, (size_t)-1, p, NETUTILS_TRACE_IS_TX | NETUTILS_TRACE_NEWLINE);

    pbuf_copy_partial(p, tx_TMPbuf, p->tot_len, 0);
    if (FSP_SUCCESS == R_ETHER_Write(&g_ether0_ctrl, tx_TMPbuf, p->tot_len)) {
        return ERR_OK;
    } else {
        return ERR_BUF;
    }
}

static err_t eth_netif_init(struct netif *netif) {
    netif->linkoutput = eth_netif_output;
    netif->output = etharp_output;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    // Checksums only need to be checked on incoming frames, not computed on outgoing frames
    /*NETIF_SET_CHECKSUM_CTRL(netif,
        NETIF_CHECKSUM_CHECK_IP
        | NETIF_CHECKSUM_CHECK_UDP
        | NETIF_CHECKSUM_CHECK_TCP
        | NETIF_CHECKSUM_CHECK_ICMP
        | NETIF_CHECKSUM_CHECK_ICMP6);
    */
    return ERR_OK;
}

static void eth_lwip_init(eth_t *self) {
    // err_t e;

    // Start with all-zero so DHCP populates the address.  Caller can also
    // override with lan.ifconfig((ip, mask, gw, dns)) if there is no DHCP
    // server on this network.
    ip_addr_t ipconfig[4];
    IP4_ADDR(&ipconfig[0], 0, 0, 0, 0);
    IP4_ADDR(&ipconfig[1], 0, 0, 0, 0);
    IP4_ADDR(&ipconfig[2], 0, 0, 0, 0);
    IP4_ADDR(&ipconfig[3], 8, 8, 8, 8);

    MICROPY_PY_LWIP_ENTER

    struct netif *n = &self->netif;
    n->name[0] = 'e';
    n->name[1] = '0';
    netif_add(n, &ipconfig[0], &ipconfig[1], &ipconfig[2], self, eth_netif_init, ethernet_input);
    netif_set_hostname(n, mod_network_hostname_data);
    netif_set_default(n);
    netif_set_up(n);

    dns_setserver(0, &ipconfig[3]);
    dhcp_set_struct(n, &self->dhcp_struct);
    dhcp_start(n);

    netif_set_link_up(n);

    // Do NOT block here waiting for DHCP.  PendSV-driven sys_check_timeouts()
    // will run DHCP retries in the background; the caller can poll
    // lan.ifconfig() / lan.isconnected() to see when the address arrives:
    //
    //   lan.active(True)                         # returns within ~1 ms
    //   while lan.ifconfig()[0] == '0.0.0.0':
    //       time.sleep_ms(100)
    //   ...

    MICROPY_PY_LWIP_EXIT
}

static void eth_lwip_deinit(eth_t *self) {
    MICROPY_PY_LWIP_ENTER
    for (struct netif *netif = netif_list; netif != NULL; netif = netif->next) {
        if (netif == &self->netif) {
            netif_remove(netif);
            netif->ip_addr.addr = 0;
            netif->flags = 0;
        }
    }
    MICROPY_PY_LWIP_EXIT
}

// Drain all pending RX frames from the software FIFO (filled by ETH ISR)
// and feed them into lwIP.  Runs from PendSV (low priority IRQ).
//
// Also acts as a fall-back direct EDMAC drain — if the FIFO is empty but
// EDMAC still has frames waiting (e.g. the ISR was masked or skipped),
// pull them straight from EDMAC.  This keeps the system live even if the
// FIFO path ever stalls.
void eth_drain_rx(void) {
    eth_t *self = &eth_instance;

    // 1. Consume anything queued by the ISR.
    while (eth_rx_tail != eth_rx_head) {
        uint8_t t = eth_rx_tail;
        eth_rx_slot_t *slot = &eth_rx_fifo[t];
        if (slot->len > 0) {
            eth_irq_rx_frames++;
            eth_process_frame(self, slot->len, slot->buf);
            slot->len = 0;
        }
        eth_rx_tail = (uint8_t)((t + 1) % ETH_RX_FIFO_SLOTS);
    }

    // 2. Fall-back: pull anything still sitting in EDMAC descriptors.
    //    This covers the case where the ISR was missed or the FIFO was full
    //    when an interrupt fired (drop accounted for in eth_rx_dropped).
    static uint8_t scratch_buf[1536] __attribute__((aligned(4)));
    uint32_t len = 0;
    while (FSP_SUCCESS == R_ETHER_Read(&g_ether0_ctrl, scratch_buf, &len)) {
        if (len > 0) {
            eth_irq_rx_frames++;
            eth_process_frame(self, len, scratch_buf);
        }
        len = 0;
    }
}

void ETH_IRQHandler(ether_callback_args_t *p_args) {
    eth_irq_events++;

    switch (p_args->event)
    {
        case ETHER_EVENT_WAKEON_LAN:
            break;
        case ETHER_EVENT_LINK_ON:
            phy_link_status = 1;
            eth_irq_link_on++;
            break;
        case ETHER_EVENT_LINK_OFF:
            phy_link_status = 0;
            eth_irq_link_off++;
            break;

        case ETHER_EVENT_INTERRUPT: {
            eth_irq_interrupt++;
            eth_last_eesr = p_args->status_eesr;
            if (ETHER_EXAMPLE_ETHER_ISR_EE_FR_MASK == (p_args->status_eesr & ETHER_EXAMPLE_ETHER_ISR_EE_FR_MASK)) {
                // Pull every available frame out of EDMAC and queue it in
                // the software FIFO.  Keeps the ISR short — no lwIP, no
                // pbuf allocation, just 16-byte memcpy of the frame body
                // (1500 B max) into a fixed buffer.  PendSV does the rest.
                static uint8_t isr_drop_buf[1536] __attribute__((aligned(4)));
                while (1) {
                    uint8_t h = eth_rx_head;
                    uint8_t next = (uint8_t)((h + 1) % ETH_RX_FIFO_SLOTS);
                    if (next == eth_rx_tail) {
                        // FIFO full — pull the next frame into a *separate*
                        // scratch buffer (must NOT clobber slot[h] which
                        // already holds an unconsumed frame) and drop it.
                        // EDMAC keeps the descriptor ring rolling.
                        uint32_t l = 0;
                        if (FSP_SUCCESS == R_ETHER_Read(&g_ether0_ctrl,
                                                       isr_drop_buf, &l)) {
                            eth_rx_dropped++;
                            continue;
                        }
                        break;
                    }
                    uint32_t len = 0;
                    fsp_err_t err = R_ETHER_Read(&g_ether0_ctrl,
                                                 eth_rx_fifo[h].buf, &len);
                    if (FSP_SUCCESS != err || len == 0) {
                        if (FSP_SUCCESS != err) {
                            eth_irq_rx_failed++;
                        }
                        break;
                    }
                    eth_rx_fifo[h].len = (uint16_t)len;
                    eth_rx_head = next;
                    // Track peak occupancy for diagnostics.
                    uint8_t occ = (uint8_t)((eth_rx_head + ETH_RX_FIFO_SLOTS
                                            - eth_rx_tail) % ETH_RX_FIFO_SLOTS);
                    if (occ > eth_rx_high_water) {
                        eth_rx_high_water = occ;
                    }
                }
                pendsv_schedule_dispatch(PENDSV_DISPATCH_LWIP, pyb_lwip_poll);
            }

            if (ETHER_EXAMPLE_ETHER_ISR_EE_TC_MASK == (p_args->status_eesr & ETHER_EXAMPLE_ETHER_ISR_EE_TC_MASK)) {
            }

            if (ETHER_EXAMPLE_ETHER_ISR_EC_MPD_MASK == (p_args->status_ecsr & ETHER_EXAMPLE_ETHER_ISR_EC_MPD_MASK)) {
            }
        }
        break;

        default: {
        }
    }
}

// ------------------------------------------------------------------------------

void eth_init(eth_t *self, int mac_idx) {
    fsp_err_t err;

    mp_hal_pin_output(phy_RST);

    mp_hal_pin_low(phy_RST);
    mp_hal_delay_us(200);
    mp_hal_pin_high(phy_RST);
    mp_hal_delay_us(200);

    if ((err = R_ETHER_Open(&g_ether0_ctrl, &g_ether0_cfg)) == FSP_SUCCESS) {
        self->netif.hwaddr_len = 6;
        memcpy(self->netif.hwaddr, g_ether0_cfg.p_mac_address, self->netif.hwaddr_len);
        eth_open_flag = true;
    }
}

void eth_set_trace(eth_t *self, uint32_t value) {
    self->trace_flags = value;
}

struct netif *eth_netif(eth_t *self) {
    return &self->netif;
}

int eth_link_status(eth_t *self) {

    struct netif *netif = &self->netif;
    if ((netif->flags & (NETIF_FLAG_UP | NETIF_FLAG_LINK_UP))
        == (NETIF_FLAG_UP | NETIF_FLAG_LINK_UP)) {
        if (netif->ip_addr.addr != 0) {
            return 3; // link up
        } else {
            return 2; // link no-ip;
        }
    } else {
        return phy_link_status;  // 1: link up | 0: link down
    }
}

int eth_start(eth_t *self) {
    eth_lwip_deinit(self);

    // Fire-and-forget: do not busy-loop on R_ETHER_LinkProcess until success.
    // The PendSV-driven lwIP poll calls R_ETHER_LinkProcess regularly and the
    // PHY auto-negotiation will complete in the background.  This keeps
    // lan.active(True) non-blocking — control returns to Python within a few
    // hundred microseconds instead of seconds.
    (void)R_ETHER_LinkProcess(&g_ether0_ctrl);

    eth_lwip_init(self);

    return 0;
}

int eth_stop(eth_t *self) {
    eth_lwip_deinit(self);
    return 0;
}

void eth_low_power_mode(eth_t *self, bool enable) {
    (void)self;
    printf("eth_low_power_mode() not implemented \r\n");
}

#endif // defined(MICROPY_HW_ETH_MDC)
