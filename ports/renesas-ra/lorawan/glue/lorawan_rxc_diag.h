/*
 * T-V3.1 — Class C RX (RxC) instrumentation probe.
 *
 * 14 cumulative-since-boot counters / last-value snapshots that surface
 * via mac.stats()['rxc']. Capture points are listed in the architect spec
 * (RXC_PROBE_DESIGN_2026-05-13.md) and pinpoint the divergence between
 * the LoRaMac scheduler (OpenContinuousRxCWindow) and the radio driver
 * (RadioRx/RadioRxBoosted -> SX126xSetRx*).
 *
 * Storage lives in mod_lorawan.c (see lorawan_rxc_diag definition there)
 * to keep the diff scope minimal and avoid touching vendor MAC ctx.
 *
 * Concurrency: counters use __atomic_fetch_add, snapshots use
 * __atomic_store_n. RA4M2 (Cortex-M33) has no D-cache so plain atomics
 * are sufficient — no cache-maintenance ops needed.
 *
 * Build flag: UNCONDITIONAL. Diagnostic only; never gate by
 * LORAWAN_BUILD_PHASE because the storage is referenced from both
 * radio.c and LoRaMac.c.
 */

#ifndef MICROPY_INCLUDED_RENESAS_RA_LORAWAN_RXC_DIAG_H
#define MICROPY_INCLUDED_RENESAS_RA_LORAWAN_RXC_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mod_lorawan_rxc_diag {
    uint16_t rxc_open_attempts;
    uint16_t rxc_open_skipped_rf_rx;
    uint16_t rxc_open_skipped_rf_tx;
    uint16_t rxc_region_ok;
    uint16_t rxc_region_fail;
    uint16_t rxc_radio_rx_result;
    uint32_t last_rxc_freq;
    uint8_t  last_rxc_dr;
    uint8_t  last_rxc_continuous;
    uint8_t  last_rx_done_slot;
    uint8_t  last_rx_timeout_slot;
    uint8_t  last_rx_error_slot;
    int8_t   last_rx_rssi_dbm;
    int8_t   last_rx_snr_db;
    uint8_t  last_rx_stats_valid;
    uint8_t  last_rx_done_slot_id;  /* r13 — slot of most recent RxDone, 0 on JOIN_REQ */
    uint8_t  _pad[1];
    uint32_t last_radio_rx_timeout_arg_caller;
    uint32_t last_radio_rx_timeout_arg;
    /* r13 Phase 1 — RX-window open/close timestamps (DWT CYCCNT @100 MHz).
     * Reset to 0 on MLME_JOIN_REQ enqueue (except rx2_skipped_total which
     * is cumulative since boot). cyc/100 yields microseconds. */
    uint32_t last_rx1_open_cyc;
    uint32_t last_rx1_close_cyc;
    uint32_t last_rx2_open_cyc;
    uint32_t last_rx2_close_cyc;
    uint32_t rx2_skipped_total;
    /* WindowTimeout from RegionComputeRxWindowParameters is in SYMBOLS,
     * not milliseconds. Old field name (*_ms) was a footgun — kept the
     * type, fixed the name in the r13-fix wave. */
    uint32_t last_join_rx1_window_timeout_symbols;
    uint32_t last_join_rx2_window_timeout_symbols;
    /* Join-only RX1 compute visibility.
     * Reset to 0 on MLME_JOIN_REQ enqueue alongside the timeout snapshots.
     *
     * last_join_used_override_flag:
     *     Reserved (always 0 in clean upstream — no override applied).
     * last_join_effective_min_rx_symbols:
     *     MinRxSymbols passed into RegionComputeRxWindowParameters for
     *     the JOIN RX1 call. Equals MacParams.MinRxSymbols.
     * last_join_effective_system_max_rx_error_ms:
     *     SystemMaxRxError actually passed into the JOIN RX1 compute,
     *     i.e. MacParams.SystemMaxRxError (region default).
     * last_join_rx1_window_offset_ms:
     *     RxWindow1Config.WindowOffset (ms, signed) returned by the JOIN
     *     RX1 compute, BEFORE the LoRaMacGetStackProcessTime subtraction.
     *     Saturated to int16 range — actual values are tens of ms. */
    uint8_t  last_join_used_override_flag;
    uint8_t  last_join_effective_min_rx_symbols;
    uint8_t  _pad2[2];
    uint16_t last_join_effective_system_max_rx_error_ms;
    int16_t  last_join_rx1_window_offset_ms;
} mod_lorawan_rxc_diag_t;

extern volatile mod_lorawan_rxc_diag_t lorawan_rxc_diag;

#define LORAWAN_RXC_INC_U16(field) \
    ((void)__atomic_fetch_add((uint16_t *)&lorawan_rxc_diag.field, 1u, \
                              __ATOMIC_RELAXED))

#define LORAWAN_RXC_STORE_U32(field, val) \
    __atomic_store_n((uint32_t *)&lorawan_rxc_diag.field, (uint32_t)(val), \
                     __ATOMIC_RELAXED)

#define LORAWAN_RXC_STORE_U8(field, val) \
    __atomic_store_n((uint8_t *)&lorawan_rxc_diag.field, (uint8_t)(val), \
                     __ATOMIC_RELAXED)

#ifdef __cplusplus
}
#endif

#endif /* MICROPY_INCLUDED_RENESAS_RA_LORAWAN_RXC_DIAG_H */
