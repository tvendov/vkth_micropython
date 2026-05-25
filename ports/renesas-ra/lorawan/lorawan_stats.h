/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
 *
 * Internal observation counter storage for the Renesas LoRaWAN C-stack
 * port.
 *
 * Design references (kept under SESSION_VK_RA4M2_LoRaWAN_C_STACK_PORT/QA_RESULTS):
 *   phase1_step2_stats_schema.md   ? 29 leaves across 5 groups.
 *   phase1_step3_atomicity.md      ? primitive choice + struct layout.
 *   phase1_step4_stats_reset.md    ? NVIC-mask + memset + opcode 0xFF.
 *
 * Compile-time escape hatch
 * -------------------------
 * Pass -DLORAWAN_OBSERVATION_DISABLE=1 in CFLAGS_EXTRA to collapse every
 * counter increment / max-update / store / opcode-latch / SPI-bytes-add
 * call site to ((void)0). The g_lorawan_stats storage remains so internal
 * users still link, while reset becomes a storage-only no-op.
 *
 * Example (one-shot sanity build):
 *     CFLAGS_EXTRA="-DLORAWAN_OBSERVATION_DISABLE=1" \
 *         make BOARD=VK_RA4M2 BUILD=build-VK_RA4M2-obsdis LORAWAN_BUILD_PHASE=4
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set true by lorawan_stats_dwt_init() iff DWT->CYCCNT was observed to
 * advance during a short probe loop. Written once at init, read-only
 * afterwards ? no atomics required. Internal readers may skip cycle-max
 * reporting / re-enable attempts when false. */
extern volatile bool s_dwt_available;

/* r12/r13 last-RX-stats triplet (restored 2026-05-24 for class_a_demo).
 * Producer: mac_mcps_indication in mod_lorawan.c reads McpsIndication_t
 * .Rssi / .Snr — these are already filled by LoRaMac.c from
 * Radio.PacketStatus, which per sx126x.c GetPacketStatus is converted
 * to dBm / dB (see reference_sx126x_pkt_status_preconverted.md). Single
 * writer (scheduler/foreground context). Consumer: lorawan_mac_last_rx_stats
 * Python binding. _valid is reset on join start + factory reset; the
 * triplet is left intact across resets-without-new-RX so the last value
 * stays visible to user code. */
extern volatile int8_t  s_lorawan_last_rx_rssi_dbm;
extern volatile int8_t  s_lorawan_last_rx_snr_db;
extern volatile uint8_t s_lorawan_last_rx_stats_valid;

/* Storage layout. Order matches Step 2 schema; mac group sits first so the
 * subsequent uint64_t in the spi group naturally lands on an 8-byte
 * boundary (offset 16 from struct base which is itself 8-byte aligned).
 *
 * Total logical size: 12 + 4 pad + 8 + 16 + 20 + 44 + 24 = 128 B.
 */
typedef struct lorawan_stats {
    /* group: mac */
    uint32_t mac_process_count;
    uint32_t mac_process_last_us;
    uint32_t mac_process_max_us;

    /* group: spi ? spi_bytes_total first to satisfy 8-byte alignment */
    uint64_t spi_bytes_total;
    uint32_t spi_xfer_count;
    uint32_t spi_max_len;
    uint32_t spi_one_byte_count;
    /* incremented on s_spi_xfer_busy re-entry (formerly named
       sx126x_spi_busy_reject_count). This is a nested-call rejection, not a
       chip BUSY-line condition. */
    uint32_t spi_nested_reject_count;
    /* AD5.7 ? per-stage micro-timing. spi_stage_pre_busy_max_us and
       spi_stage_post_busy_max_us separate the busy_wait_max_us aggregate
       (pre-CS + post-CS) into its two contributors so cycle-budget
       analysis can attribute wall-time. spi_stage_byte_xfer_max_us covers the
       synchronous byte-shift window while CS is asserted.
       Both busy_wait_*_us and the new stage counters coexist. */
    uint32_t spi_stage_pre_busy_max_us;
    uint32_t spi_stage_byte_xfer_max_us;
    uint32_t spi_stage_post_busy_max_us;
    /* P3.0 ? counts NSS-low wake pulses issued by SX126xWakeup(). Increments
       once per call to vendor's SX126xCheckDeviceReady() that finds the chip
       in MODE_SLEEP/MODE_RX_DC/MODE_COLD_SLEEP. Should be 0 during init
       (no sleep before first SetSleep) and grow by 1 per join+uplink cycle.

       Q3.2 ? derive wake_skip_count in Python as:
         skip = spi_xfer_count - sx126x_wake_count
       Each *_e entrypoint calls CheckDeviceReady() exactly once (after
       which sx126x_spi_xfer() increments spi_xfer_count exactly once), so
       the difference equals the count of SPI calls where the chip was
       already awake (gate decided not to issue a wake pulse). Healthy
       ratio: wake/xfer < 15% under steady traffic. */
    uint32_t sx126x_wake_count;

    /* group: busy */
    uint32_t busy_wait_count;
    uint32_t busy_wait_last_us;
    uint32_t busy_wait_max_us;
    uint8_t  busy_last_opcode;          /* sentinel 0xFF = invalid */
    uint8_t  _pad_busy[3];              /* keep next u32 4-byte aligned */

    /* group: isr */
    uint32_t hard_isr_dio1_count;
    uint32_t hard_isr_timer0_count;
    uint32_t hard_isr_timer1_count;
    uint32_t hard_isr_queue_overflow_count;
    uint32_t hard_isr_queue_push_count;
    uint32_t hard_isr_dio1_cycles_max;
    uint32_t hard_isr_timer0_cycles_max;
    uint32_t hard_isr_timer1_cycles_max;
    uint32_t hard_isr_dio1_reentry_count;
    uint32_t hard_isr_timer0_reentry_count;
    uint32_t hard_isr_timer1_reentry_count;
    /* Diagnostic counters for hard-event paths. SPI is synchronous now, so
       hard_isr_spi_done_* are compatibility slots and should remain zero. */
    uint32_t hard_isr_spi_done_count;
    uint32_t hard_isr_spi_done_cycles_max;
    uint32_t hard_isr_busy_low_count;
    uint32_t hard_isr_busy_low_cycles_max;

    /* group: nvm */
    uint32_t nvm_save_count;
    uint32_t nvm_save_last_ms;
    uint32_t nvm_save_max_ms;
    uint32_t nvm_save_error_count;
    uint32_t nvm_save_call_us;
    uint32_t nvm_save_done_us;

    /* group: mcps ? storage + zero-init only. Increment sites are optional. */
    uint32_t mcps_indication_queued_count;
    uint32_t mcps_indication_dropped_count;
    uint32_t mcps_indication_queue_high_water;
    uint32_t event_cb_drain_count;
    uint32_t event_cb_drain_reentry_skip_count;
} lorawan_stats_t;

extern volatile lorawan_stats_t g_lorawan_stats;

/* Counter primitives.
 *
 * STATS_INC / STATS_ADD use GCC __atomic_fetch_add with __ATOMIC_RELAXED.
 * On Cortex-M33 this lowers to LDREX/STREX without a DMB ? sufficient for
 * counter-only state where no other write is being published in order.
 *
 * STATS_UPDATE_MAX uses a CAS retry loop so a higher max from a preempting
 * writer is never lost. Naive read-compare-store can lose updates.
 *
 * STATS_SET_OPCODE is a plain volatile store. The single-writer invariant
 * is enforced by s_spi_xfer_busy in sx126x_board.c (see Step 3 ?5).
 *
 * STATS_STORE is a plain volatile store for single-writer u32 fields
 * (last_us / last_ms / call_us / done_us style). Used in places where
 * the writer is naturally serialized (s_spi_xfer_busy guard, the single
 * mac_process pump call site, NvmDataMgmtStore which runs in scheduler
 * context). No atomic needed ? internal readers never preempt the writer.
 *
 * LORAWAN_OBSERVATION_DISABLE collapses all five to (void)0 so Step 14
 * build #4 can measure the macro footprint without removing the storage
 * or internal storage surface.
 */
#ifdef LORAWAN_OBSERVATION_DISABLE

#define STATS_INC(field)                ((void)0)
#define STATS_ADD(field, n)             ((void)0)
#define STATS_UPDATE_MAX(field, val)    ((void)0)
#define STATS_SET_OPCODE(field, op)     ((void)0)
#define STATS_STORE(field, val)         ((void)0)

#else  /* observation enabled */

#define STATS_INC(field) \
    ((void)__atomic_fetch_add((uint32_t *)&g_lorawan_stats.field, 1u, \
                              __ATOMIC_RELAXED))

#define STATS_ADD(field, n) \
    ((void)__atomic_fetch_add((uint32_t *)&g_lorawan_stats.field, \
                              (uint32_t)(n), __ATOMIC_RELAXED))

#define STATS_UPDATE_MAX(field, val) do {                                    \
    uint32_t _v = (uint32_t)(val);                                           \
    uint32_t _cur = __atomic_load_n((uint32_t *)&g_lorawan_stats.field,      \
                                     __ATOMIC_RELAXED);                      \
    while (_v > _cur) {                                                      \
        if (__atomic_compare_exchange_n((uint32_t *)&g_lorawan_stats.field,  \
                                        &_cur, _v, true,                     \
                                        __ATOMIC_RELAXED,                    \
                                        __ATOMIC_RELAXED)) {                 \
            break;                                                           \
        }                                                                    \
    }                                                                        \
} while (0)

#define STATS_SET_OPCODE(field, op) \
    (g_lorawan_stats.field = (uint8_t)(op))

#define STATS_STORE(field, val) \
    do { g_lorawan_stats.field = (val); } while (0)

#endif  /* LORAWAN_OBSERVATION_DISABLE */

/* Hard-ISR cycle-delta wrapper. Used by DIO1 and timer hard callbacks to
 * sample DWT->CYCCNT around the IRQ body and feed STATS_UPDATE_MAX. Under
 * LORAWAN_OBSERVATION_DISABLE both macros collapse to no-ops AND the
 * DWT->CYCCNT reads are gone ? so Step 14 build #4 drops the access
 * entirely from the ELF, not just the counter update. */
#ifdef LORAWAN_OBSERVATION_DISABLE
#define LORAWAN_ISR_CYCLES_BEGIN()        ((void)0)
#define LORAWAN_ISR_CYCLES_END(field)     ((void)0)
#else
#define LORAWAN_ISR_CYCLES_BEGIN()        uint32_t _isr_t0 = DWT->CYCCNT
#define LORAWAN_ISR_CYCLES_END(field) \
    STATS_UPDATE_MAX(field, DWT->CYCCNT - _isr_t0)
#endif

/* BUSY-poll timing wrapper. Sample mp_hal_ticks_us() before the poll loop
 * and, on the way out, store the delta into <last_field> + bump
 * <max_field>. Same disable contract as LORAWAN_ISR_CYCLES_BEGIN/END:
 * under LORAWAN_OBSERVATION_DISABLE both macros vanish and the
 * mp_hal_ticks_us() calls are gone too ? no orphan locals, no -Werror
 * unused-variable noise. _BEGIN must precede the loop in the same block
 * scope as _END so the local `_busy_t0` is visible at the end. */
#ifdef LORAWAN_OBSERVATION_DISABLE
#define LORAWAN_BUSY_TIMING_BEGIN()                  ((void)0)
#define LORAWAN_BUSY_TIMING_END(last_field, max_field)  ((void)0)
#else
#define LORAWAN_BUSY_TIMING_BEGIN() \
    uint32_t _busy_t0 = mp_hal_ticks_us()
#define LORAWAN_BUSY_TIMING_END(last_field, max_field) do {                  \
    uint32_t _busy_dt = mp_hal_ticks_us() - _busy_t0;                        \
    STATS_STORE(last_field, _busy_dt);                                       \
    STATS_UPDATE_MAX(max_field, _busy_dt);                                   \
} while (0)
#endif

/* 64-bit seqlock helpers for spi_bytes_total.
 *
 * Single-writer invariant: writes occur only inside the s_spi_xfer_busy
 * critical region in sx126x_board.c, so the writer never races with
 * itself. Readers twin-read the high half and retry until they observe a
 * stable epoch.
 */
static inline void stats_spi_bytes_add(uint32_t n) {
#ifdef LORAWAN_OBSERVATION_DISABLE
    (void)n;
#else
    uint32_t *p_lo = (uint32_t *)&g_lorawan_stats.spi_bytes_total;
    uint32_t *p_hi = p_lo + 1;
    uint32_t lo = __atomic_load_n(p_lo, __ATOMIC_RELAXED);
    uint32_t new_lo = lo + n;
    __atomic_store_n(p_lo, new_lo, __ATOMIC_RELAXED);
    if (new_lo < lo) {
        uint32_t hi = __atomic_load_n(p_hi, __ATOMIC_RELAXED);
        __atomic_store_n(p_hi, hi + 1u, __ATOMIC_RELAXED);
    }
#endif
}

/* Internal observation lifecycle prototype. */
void lorawan_stats_dwt_init(void);

#ifdef __cplusplus
}
#endif

