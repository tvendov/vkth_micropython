/*
 * lorawan/glue/lorawan_pump.h
 *
 * Phase 4 — guarded C pump + counter surface for the clean-port Renesas
 * LoRaWAN stack. Scaffolding only; LoRaMacProcess() invocation is gated
 * by LORAWAN_C_PUMP_ENABLE (default 0 — see lorawan.mk). Phase 5 wires
 * MacProcessNotify -> lorawan_driver_request_pump(); Phase 6 wires timer
 * backend; Phase 7 wires DIO1 ISR.
 *
 * Counter list per operator decisions 5 (pump-domain) + 10 (latency
 * gates). Storage is file-private to lorawan_pump.c; only the snapshot
 * getter is exposed.
 */

#ifndef LORAWAN_GLUE_LORAWAN_PUMP_H
#define LORAWAN_GLUE_LORAWAN_PUMP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reason codes for lorawan_driver_request_pump(). Each reason is the
   t0-source identity used by the histogram latency tables. Phase 4
   exercises REASON_PY (Python mac.process() trampoline); the other
   four are placeholders for the wiring phases noted above. */
typedef enum {
    LORAWAN_PUMP_REASON_NOTIFY   = 1, /* from MacProcessNotify   (Phase 5) */
    LORAWAN_PUMP_REASON_TIMER    = 2, /* from C timer backend    (Phase 6) */
    LORAWAN_PUMP_REASON_DIO1     = 3, /* from DIO1 ISR           (Phase 7) */
    LORAWAN_PUMP_REASON_PY       = 4, /* from Python mac.process() */
    LORAWAN_PUMP_REASON_INTERNAL = 5, /* re-run after pending flag */
} lorawan_pump_reason_t;

/* Snapshot surface — read by Python mac.pump_diag(). Fields written
   inside the pump module via __atomic_relaxed primitives; the reader
   takes a non-atomic copy. p50/p95/p99/max are computed at read time
   from the bucketed histogram, so the four numbers in each quartet
   are mutually consistent for that snapshot. */
typedef struct lorawan_pump_stats {
    uint32_t mac_process_reentry_count;
    uint32_t pump_request_count;
    uint32_t pump_run_count;
    uint32_t pump_deferred_spi_busy_count;
    uint32_t pump_deferred_flash_busy_count;
    /* Phase 6 debug — TimerStart-side stamp count for the rx1_arm t0
       anchor. Pairs with the SetRx t1 consumer added in Phase 8; remove
       this field once the pair closes and the histogram tells the full
       story. Increments inside lorawan_pump_stamp_rx1_arm_t0(). */
    uint32_t rx1_arm_t0_stamp_count;

    /* pump_dispatch_latency_us — request->run wait time. Non-zero in
       Phase 4 (REQ-PY exercise path). first_sample_boot_ms added in
       p7-instrument-003 — captures mp_hal_ticks_ms() at count 0->1 to
       discriminate boot-transient vs steady-state outliers. */
    uint32_t pump_dispatch_latency_us_p50;
    uint32_t pump_dispatch_latency_us_p95;
    uint32_t pump_dispatch_latency_us_p99;
    uint32_t pump_dispatch_latency_us_max;
    uint32_t pump_dispatch_latency_us_count;
    uint32_t pump_dispatch_latency_us_first_sample_boot_ms;

    /* dio1_to_pump_us — DIO1 ICU edge -> pump_run() entry. Reads 0
       until Phase 7 wires t0 in dio1_icu_isr. */
    uint32_t dio1_to_pump_us_p50;
    uint32_t dio1_to_pump_us_p95;
    uint32_t dio1_to_pump_us_p99;
    uint32_t dio1_to_pump_us_max;
    uint32_t dio1_to_pump_us_count;
    uint32_t dio1_to_pump_us_first_sample_boot_ms;

    /* rx1_arm_to_setrx_us — TimerStart(RxWindowTimer1) -> opcode 0x82
       SetRx stage. Reads 0 until Phase 6/8 wires t0. HARD release gate
       p99 <= 2000 us (operator decision 10). */
    uint32_t rx1_arm_to_setrx_us_p50;
    uint32_t rx1_arm_to_setrx_us_p95;
    uint32_t rx1_arm_to_setrx_us_p99;
    uint32_t rx1_arm_to_setrx_us_max;
    uint32_t rx1_arm_to_setrx_us_count;
    uint32_t rx1_arm_to_setrx_us_first_sample_boot_ms;

    /* p7-instrument-003 — per-reason pump_dispatch_latency split. Index
       by LORAWAN_PUMP_REASON_* (idx 0 / INTERNAL=5 unused for split).
       Sum invariant: sum(by_reason[1..4].count) <= aggregate.count
       (out-of-range reasons feed only the aggregate). */
    uint32_t pump_dispatch_latency_by_reason_p50[6];
    uint32_t pump_dispatch_latency_by_reason_p95[6];
    uint32_t pump_dispatch_latency_by_reason_p99[6];
    uint32_t pump_dispatch_latency_by_reason_max[6];
    uint32_t pump_dispatch_latency_by_reason_count[6];
    uint32_t pump_dispatch_latency_by_reason_first_sample_boot_ms[6];

    /* Phase 7 cleanup — worst-case µs across the DIO1 dispatch wrapper
       (sx126x_board_dispatch_dio1_irq) and SPI BUSY-wait yield loops. DWT-
       sampled @ 100 MHz; pinpoint latency long-poles in the radio-touched
       pump path without disturbing control flow. */
    uint32_t dio1_dispatch_max_us;
    uint32_t spi_busy_wait_max_us;

    /* Phase 6 instrumentation (p6-instrument-001) — per-reason pump_request
       counter; sum invariant: sum(by_reason) == pump_request_count. Indices
       map to LORAWAN_PUMP_REASON_* enum. Size 6 covers 0..INTERNAL=5. */
    uint32_t pump_request_by_reason[6];

    /* Phase 8 — anti-fake-pass guard. SHOULD stay 0 across any TR-1/TR-3
       run because Phase 8 deletes the SPI-side `s_rx_window_active = 1u`
       write. If a maintainer accidentally re-introduces a SPI-path write
       and gates it through lorawan_pump_observe_rx_window_active_set_via_spi(),
       this counter rises and the test fails. */
    uint32_t rx_window_active_set_via_spi_count;

    /* Phase 8 — observation counters for the cached SetRx (opcode 0x82)
       command buffer. setrx_cmd_cache_count rises on every 0x82 SPI write
       (the cache is overwritten); setrx_cmd_cache_ready is the last-known
       state of the s_setrx_cmd_ready flag at snapshot time. Phase 11 will
       consume the cache from a hard-ISR DTC start. Phase 8 keeps these
       observation-only so the cache write path is exercised under TR-3. */
    uint32_t setrx_cmd_cache_count;
    uint32_t setrx_cmd_cache_ready;
} lorawan_pump_stats_t;

/* Phase 5+ entry points. Phase 4 already exports them for the Python
   trampoline (REASON_PY); the bodies handle the no-LoRaMac case via
   LORAWAN_C_PUMP_ENABLE. */
void lorawan_driver_request_pump(uint8_t reason);
void lorawan_driver_pump_run(void);

/* Lifecycle. Called from lorawan.Mac() init / deinit in mod_lorawan.c. */
void lorawan_pump_init(void);
void lorawan_pump_deinit(void);

/* Introspection. is_running == "inside the do-while body" (after the
   defer gates passed); is_pending == "request arrived during a run". */
bool lorawan_pump_is_running(void);
bool lorawan_pump_is_pending(void);

/* Snapshot getter — fills *out from the file-private state + histograms.
   Safe to call from scheduler / Python context. Not safe from hard ISR
   (it does a small loop computing percentiles). */
void lorawan_pump_stats_get(lorawan_pump_stats_t *out);

/* Sampling-site hooks — called from glue at the canonical t0 points.
   Phase 4 declares them; only the DIO1 hook is wired by Phase 7. The
   RX1-arm hook is wired by Phase 6/8 (TimerStart -> SetRx). In Phase 4
   these can be called from anywhere with no side effect beyond stamping
   a per-reason t0 breadcrumb. */
void lorawan_pump_stamp_dio1_t0(void);
/* Phase 6 — TimerStart subscribe-hook stamps t0 anchor for the
   rx1_arm_to_setrx_us histogram. Caller passes a DWT cycle sample
   taken at the canonical t0 site (inside TimerStart for the RX1
   timer event). Phase 8 closes the pair at the SX126x SetRx
   opcode 0x82 site. */
void lorawan_pump_stamp_rx1_arm_t0(uint32_t dwt_cycles);

/* Phase 8 — atomic read-and-clear of the t0 anchor stamped by
   lorawan_pump_stamp_rx1_arm_t0(). Called from sx126x_spi_xfer at the
   opcode 0x82 detection block to close the rx1_arm_to_setrx_us pair.
   Returns 0 when no t0 is pending (e.g. retransmit / class-C continuous-
   RX rearm); non-zero is the captured DWT cycle sample, after which
   the internal valid flag is cleared so subsequent 0x82 writes without
   a fresh TimerStart do not double-count. The read+clear is bracketed
   by mp_hal_quiet_timing_enter()/exit() (port equivalent of
   __disable_irq) — see implementation for the race rationale. */
uint32_t lorawan_pump_consume_rx1_arm_t0(void);

/* Phase 8 — anti-fake-pass observation hook. Any future SPI-path code
   that wants to set s_rx_window_active MUST route through this helper
   so the counter is visible to QA. Phase 8 has zero callers; the helper
   exists so an accidental re-introduction is loud rather than silent. */
void lorawan_pump_observe_rx_window_active_set_via_spi(void);

/* Phase 8 — observation hook for the cached SetRx (opcode 0x82) command
   buffer. Called from sx126x_spi_xfer on every 0x82 detection after the
   memcpy into the glue-private buffer. Bumps setrx_cmd_cache_count and
   stores ready=1 so mac.pump_diag() can verify the buffer is being
   refreshed each RX-window arm. Hard-ISR DTC consumer (Phase 11) will
   add invalidation hooks for the radio-config opcodes that follow. */
void lorawan_pump_observe_setrx_cmd_cache(void);

/* Phase 8 — push a (t1 - t0) µs sample into the rx1_arm_to_setrx_us
   histogram from sx126x_spi_xfer at the opcode 0x82 detection block.
   The histogram itself is file-private to lorawan_pump.c; this helper
   exists so the SPI TU does not see histogram internals. Caller is
   responsible for the cycle→µs conversion (matches lorawan_pump.c's
   cyc_to_us: (cyc + 50) / 100 for DWT @ 100 MHz). */
void lorawan_pump_observe_rx1_arm_to_setrx_us(uint32_t sample_us);

/* Phase 6 instrumentation (p6-instrument-001) — copy the TimerInit ordinal
   log (callback pointers in fixed source order, capped at 8) into the
   caller-supplied buffer. Used by mac.pump_diag() so QA can cross-check
   the captured ordinal-1 callback against objdump'd addresses of the
   upstream OnRxWindow*TimerEvent / OnAckTimeoutTimerEvent /
   OnTxDelayedTimerEvent symbols. Returns the number of valid entries
   (0..min(cap, 8)). */
uint8_t lorawan_timer_init_log_get(uintptr_t *out, uint8_t cap);

/* Phase 6 P3 instrumentation (p6-instrument-002) — copy the 8-entry
   TimerStart-side and timer_dispatch-side callback-pointer ring logs.
   Both buffers must hold 8 uint32_t. Either may be NULL to skip. Slots
   are returned in physical order (wrap is implicit; head pointer not
   exposed). Used by mac.pump_diag() to discriminate H5 vs H6 against
   the objdump'd OnRxWindow1TimerEvent runtime address. */
void lorawan_timer_logs_get(uint32_t *out_ts, uint32_t *out_disp);

/* Phase 7 — DIO1 hard-ISR -> pump-body handshake.
   s_radio_irq_pending lives in lorawan_pump.c so the pump body owns it;
   sx126x_board.c sets it from dio1_icu_isr right before requesting the
   guarded pump. Single-producer (hard ISR) / single-consumer (pump body
   running in scheduler ctx) — read-then-clear is race-safe via the seq
   counter exposed by lorawan_dio1_state_get(). */
extern volatile uint8_t s_radio_irq_pending;

/* Phase 7 — read the (t0_dwt, isr_seq) pair stamped by dio1_icu_isr.
   Storage is file-private to sx126x_board.c (where the writer is); this
   accessor lets the pump body sample the latest values when consuming
   s_radio_irq_pending. Either out pointer may be NULL. */
void lorawan_dio1_state_get(uint32_t *out_t0_dwt, uint32_t *out_isr_seq);

/* Phase 7 — pump-side snapshot of the last consumed DIO1 ISR seq counter.
   Reads 0 until the first ISR is consumed; advances on every consume.
   Used by mac.pump_diag() so QA can compute (s_dio1_isr_seq -
   s_dio1_pump_seen_seq) as a race-detection delta. */
uint32_t lorawan_dio1_pump_seen_seq_get(void);

/* Phase 7 cleanup — observation-only worst-case µs trackers. Updated
   by sx126x_board.c (single-writer per site: hard-ISR / SPI BUSY-poll).
   Reads via lorawan_pump_stats_get(); these setters take a fully-converted
   µs value and keep the storage centralised in lorawan_pump.c. */
void lorawan_pump_observe_dio1_dispatch_us(uint32_t sample_us);
void lorawan_pump_observe_spi_busy_wait_us(uint32_t sample_us);

#ifdef __cplusplus
}
#endif

#endif /* LORAWAN_GLUE_LORAWAN_PUMP_H */
