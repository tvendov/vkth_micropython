/*
 * lorawan/glue/lorawan_pump.c
 *
 * Phase 4 — guarded C pump scaffolding + counter surface.
 *
 * State machine (matches BOUNDARY_AUDIT.md §Phase 4 prerequisite):
 *
 *   request_pump(reason):
 *     pump_request_count++
 *     stamp t0_dwt into reason-indexed table
 *     if s_pump_scheduled || s_process_running:
 *         s_process_pending = 1
 *         return
 *     s_pump_scheduled = 1
 *     mp_sched_schedule(pump_trampoline)
 *
 *   pump_run() (scheduler ctx):
 *     sample t1 = DWT
 *     for each reason table slot with non-zero t0:
 *         push (t1 - t0) to dispatch_hist (or dio1_hist if reason==DIO1)
 *         clear t0
 *     if s_process_running:        # defensive — schedule serialises
 *         mac_process_reentry_count++; return
 *     if sx126x_spi_busy():
 *         pump_deferred_spi_busy_count++; reschedule; return
 *     if dflash_busy():
 *         pump_deferred_flash_busy_count++; reschedule; return
 *     s_process_running = 1; pump_run_count++
 *     do {
 *         s_process_pending = 0;
 *  #if LORAWAN_C_PUMP_ENABLE
 *         LoRaMacProcess();        # Phase 5+; gated out in Phase 4
 *  #endif
 *     } while (s_process_pending);
 *     s_process_running = 0; s_pump_scheduled = 0;
 *
 * Time source: DWT->CYCCNT @ 100 MHz (RA4M2). Wrap period ~42.9 s — far
 * longer than any single request->run dispatch latency we want to log,
 * and (t1 - t0) modular arithmetic is wrap-safe by construction. The
 * stats getter converts cycles -> microseconds at read time.
 *
 * Histogram: bucketed array (32 log-spaced buckets, ~10 us .. ~10 s).
 * Simpler than P-square; bounded RAM; p50/p95/p99/max computed in one
 * pass over buckets at snapshot time. count[] capped at UINT32_MAX (no
 * decay).
 *
 * Anti-fake-pass guard for Phase 4: LORAWAN_C_PUMP_ENABLE=0 -> the
 * macroed-out LoRaMacProcess() call is absent from the compiled body,
 * so QA can prove via radio state that no MAC work happened.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "glue/lorawan_pump.h"
#include "glue/lorawan_stats.h"
#include "glue/sx126x_board.h"
#include "mac/LoRaMac.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

/* DWT->CYCCNT direct access — keeps this TU free of CMSIS includes that
   the imported Renesas tree may not provide consistently. The 100 MHz
   conversion factor matches the rest of the lorawan glue (RA4M2 PCLKD
   = 100 MHz). */
#define DWT_CYCCNT_ADDR  (0xE0001004u)
#define DWT_CYCCNT       (*(volatile uint32_t *)DWT_CYCCNT_ADDR)
#define CYC_PER_US       (100u)

/* Reschedule reason for the defer-then-retry path. */
#define LORAWAN_PUMP_REASON_RESCHED  LORAWAN_PUMP_REASON_INTERNAL

/* ---- File-private state ----------------------------------------------- */

static volatile uint8_t s_process_running;
static volatile uint8_t s_process_pending;
static volatile uint8_t s_pump_scheduled;
static volatile uint8_t s_pump_initialized;

/* Reason -> t0_dwt breadcrumb. Index 0 unused (reason enum starts at 1).
   Slot is written by request_pump and consumed by pump_run; the writer
   side overwrites if a previous reason of the same kind has not been
   drained yet (acceptable — latest request wins for that reason). */
#define LORAWAN_PUMP_REASON_COUNT  6u
static volatile uint32_t s_reason_t0[LORAWAN_PUMP_REASON_COUNT];

/* Bucketed histogram. Bucket boundaries (microseconds):
   [0]<10 [1]<20 [2]<50 [3]<100 [4]<200 [5]<500 [6]<1000 [7]<2000
   [8]<5000 [9]<10000 [10]<20000 [11]<50000 [12]<100000
   [13]<200000 [14]<500000 [15]>=500000
   Captures the 1-us .. 1-s range with sub-decade resolution where it
   matters for p99 (operator decision 10 gate at 2 ms). */
#define LORAWAN_HIST_BUCKETS  16u
typedef struct {
    uint32_t bucket[LORAWAN_HIST_BUCKETS];
    uint32_t max_us;
    uint32_t count;
    /* p7-instrument-003 — mp_hal_ticks_ms() captured on count 0->1.
       Same monotonic source as Python time.ticks_ms(); lets QA decide
       whether the first sample is a boot transient or steady-state. */
    uint32_t first_sample_boot_ms;
} lorawan_pump_hist_t;

static lorawan_pump_hist_t s_dispatch_hist;
static lorawan_pump_hist_t s_dio1_hist;
static lorawan_pump_hist_t s_rx1_arm_hist;

/* p7-instrument-003 — per-reason pump_dispatch_latency histograms.
   Index by LORAWAN_PUMP_REASON_* enum: idx 0 reserved / idx 5 INTERNAL
   for reschedule defer path; idx 1=NOTIFY, 2=TIMER, 3=DIO1, 4=PY.
   Attribution is per-sample via the loop-variable r over s_reason_t0[],
   not via a single shared 'last reason' stash — that gives us full
   per-call fidelity instead of latest-wins collapsing. */
static lorawan_pump_hist_t s_dispatch_hist_by_reason[6];

static const uint32_t s_bucket_upper_us[LORAWAN_HIST_BUCKETS] = {
    10u, 20u, 50u, 100u, 200u, 500u, 1000u, 2000u,
    5000u, 10000u, 20000u, 50000u, 100000u, 200000u, 500000u, 0xFFFFFFFFu,
};

/* Pump counters — file-private storage feeding the snapshot getter.
   Separate from g_lorawan_stats to keep the existing 132-byte struct
   ABI stable (downstream tools index counters by offset). */
static uint32_t s_mac_process_reentry_count;
static uint32_t s_pump_request_count;
static uint32_t s_pump_run_count;
static uint32_t s_pump_deferred_spi_busy_count;
static uint32_t s_pump_deferred_flash_busy_count;

/* Phase 6 instrumentation (p6-instrument-001) — per-reason request count.
   Invariant: sum(s_pump_request_by_reason[0..5]) == s_pump_request_count
   for any valid reason; out-of-range reasons fall through without
   incrementing per-reason but still bump the aggregate (matches existing
   t0-stamping which collapses to INTERNAL). */
static uint32_t s_pump_request_by_reason[6];

/* Phase 6 — t0 anchor captured at TimerStart(RxWindowTimer1). Phase 8
   will add the SetRx (opcode 0x82) t1 consumer that closes the pair
   into the rx1_arm_to_setrx_us histogram. Keeping these file-private
   so Phase 8 lands as a pure addition next door. */
static uint32_t s_rx1_arm_t0_dwt;
static uint32_t s_rx1_arm_t0_valid;
static uint32_t s_rx1_arm_t0_stamp_count;

/* Phase 7 — DIO1 hard-ISR -> pump-body pending flag. Set by dio1_icu_isr
   in sx126x_board.c, consumed inside the do-while body below BEFORE the
   gated LoRaMacProcess() call. Single-producer (hard ISR) / single-
   consumer (pump body, scheduler ctx); a plain volatile uint8_t is
   race-safe under the same-NVIC-line / tail-chain guarantees on Cortex-M.
   The seq snapshot captured at consume time is exposed via the
   lorawan_dio1_pump_seen_seq_get() getter so QA can compute
   (s_dio1_isr_seq - s_dio1_pump_seen_seq) as a race-detection delta. */
volatile uint8_t s_radio_irq_pending;
static uint32_t  s_dio1_pump_seen_seq;

/* Phase 7 cleanup — worst-case µs trackers for the radio-touched pump
   path. Single-writer per site via __atomic relaxed CAS-update; readers
   take a non-atomic snapshot in lorawan_pump_stats_get(). */
static uint32_t s_dio1_dispatch_max_us;
static uint32_t s_spi_busy_wait_max_us;

/* Phase 8 — anti-fake-pass counter. After Phase 8 deletes the SPI-side
   write of s_rx_window_active, this counter MUST stay 0 across TR-1 /
   TR-3. lorawan_pump_observe_rx_window_active_set_via_spi() is the only
   way to bump it; Phase 8 has zero callers of that observer. If a future
   commit re-introduces a SPI-path write and routes it through the helper,
   the QA test surfaces it; if it does NOT route through the helper, the
   reader-audit in this commit's RESP is the breadcrumb pointing at the
   regression. */
static uint32_t s_rx_window_active_set_via_spi_count;

/* Phase 8 — observation counters for the cached SetRx command buffer.
   The cache itself lives in sx126x_board.c (single-writer == SPI path);
   the count + ready flag live here so the pump_diag snapshot path sees
   one shared surface for all glue-side observation. */
static uint32_t s_setrx_cmd_cache_count;
static uint32_t s_setrx_cmd_cache_ready;

/* MicroPython scheduler node — single-shot trampoline. Re-used per
   request because mp_sched_schedule_node coalesces duplicates. */
static mp_sched_node_t s_pump_sched_node;

/* ---- Forward decls ---------------------------------------------------- */

static void pump_trampoline(mp_sched_node_t *node);
static void pump_schedule(void);

/* Defer-condition probes — Phase 4 stubs. The SPI re-entry probe links
   against the s_spi_xfer_busy flag in sx126x_board.c (extern visible
   via the lorawan_stats.h indirection — we reach it through a small
   weak shim below to avoid the cross-TU dependency rippling further).
   The dflash probe is a Phase 9 stub returning false. */
static bool sx126x_spi_busy_probe(void);
static bool dflash_busy_probe(void);

/* ---- Histogram primitives -------------------------------------------- */

static inline uint32_t cyc_to_us(uint32_t cyc) {
    return (cyc + (CYC_PER_US / 2u)) / CYC_PER_US;
}

static void hist_record(lorawan_pump_hist_t *h, uint32_t sample_us) {
    if (h == NULL) {
        return;
    }
    if (h->count == 0u) {
        /* p7-instrument-003 — same monotonic source as Python
           time.ticks_ms(); captured once, retained for life of histogram. */
        h->first_sample_boot_ms = (uint32_t)mp_hal_ticks_ms();
    }
    uint32_t idx;
    for (idx = 0; idx < LORAWAN_HIST_BUCKETS - 1u; idx++) {
        if (sample_us < s_bucket_upper_us[idx]) {
            break;
        }
    }
    if (h->bucket[idx] != UINT32_MAX) {
        h->bucket[idx]++;
    }
    if (sample_us > h->max_us) {
        h->max_us = sample_us;
    }
    if (h->count != UINT32_MAX) {
        h->count++;
    }
}

static uint32_t hist_percentile(const lorawan_pump_hist_t *h, uint32_t pct) {
    if (h == NULL || h->count == 0u) {
        return 0u;
    }
    /* Target count: ceil(pct/100 * total). Use 64-bit intermediate to
       avoid overflow if count grows large. */
    uint64_t target = ((uint64_t)h->count * (uint64_t)pct + 99ull) / 100ull;
    if (target == 0ull) {
        target = 1ull;
    }
    uint64_t cum = 0;
    for (uint32_t i = 0; i < LORAWAN_HIST_BUCKETS; i++) {
        cum += h->bucket[i];
        if (cum >= target) {
            /* Report the bucket's upper bound, except the open-ended
               last bucket which reports the recorded max so the QA
               assertion vs max stays self-consistent. */
            if (i == LORAWAN_HIST_BUCKETS - 1u) {
                return h->max_us;
            }
            return s_bucket_upper_us[i];
        }
    }
    return h->max_us;
}

/* ---- Defer probes ---------------------------------------------------- */

static bool sx126x_spi_busy_probe(void) {
    return sx126x_board_spi_busy();
}

static bool dflash_busy_probe(void) {
    /* Phase 9 dependency. Until BGO async flash lands, every save is
       blocking inside the caller — no concurrent busy window the pump
       could observe. Stub returns false to keep the gate inert. */
    return false;
}

/* ---- Public API ------------------------------------------------------ */

void lorawan_pump_init(void) {
    if (s_pump_initialized) {
        return;
    }
    s_process_running = 0;
    s_process_pending = 0;
    s_pump_scheduled = 0;
    s_mac_process_reentry_count = 0;
    s_pump_request_count = 0;
    s_pump_run_count = 0;
    s_pump_deferred_spi_busy_count = 0;
    s_pump_deferred_flash_busy_count = 0;
    memset(s_pump_request_by_reason, 0, sizeof(s_pump_request_by_reason));
    s_rx1_arm_t0_dwt = 0;
    s_rx1_arm_t0_valid = 0;
    s_rx1_arm_t0_stamp_count = 0;
    s_radio_irq_pending = 0u;
    s_dio1_pump_seen_seq = 0u;
    s_dio1_dispatch_max_us = 0u;
    s_spi_busy_wait_max_us = 0u;
    s_rx_window_active_set_via_spi_count = 0u;
    s_setrx_cmd_cache_count = 0u;
    s_setrx_cmd_cache_ready = 0u;
    memset((void *)s_reason_t0, 0, sizeof(s_reason_t0));
    memset(&s_dispatch_hist, 0, sizeof(s_dispatch_hist));
    memset(&s_dio1_hist, 0, sizeof(s_dio1_hist));
    memset(&s_rx1_arm_hist, 0, sizeof(s_rx1_arm_hist));
    memset(&s_dispatch_hist_by_reason, 0, sizeof(s_dispatch_hist_by_reason));
    memset(&s_pump_sched_node, 0, sizeof(s_pump_sched_node));
    s_pump_initialized = 1;
}

void lorawan_pump_deinit(void) {
    s_pump_initialized = 0;
    s_process_running = 0;
    s_process_pending = 0;
    s_pump_scheduled = 0;
}

bool lorawan_pump_is_running(void) {
    return s_process_running != 0u;
}

bool lorawan_pump_is_pending(void) {
    return s_process_pending != 0u;
}

void lorawan_pump_stamp_dio1_t0(void) {
    /* Phase 7 will call this from dio1_icu_isr right before the
       mp_sched_schedule_node. Phase 4 declares the entry so the histo
       symbol exists; manual callers may exercise it for TR-5. */
    s_reason_t0[LORAWAN_PUMP_REASON_DIO1] = DWT_CYCCNT;
}

void lorawan_pump_stamp_rx1_arm_t0(uint32_t dwt_cycles) {
    /* Phase 6 — called from TimerStart() inside timer_board.c when the
       armed event's callback matches the captured RX1 callback pointer
       (Strategy B, see timer_board.c). t1 is the SX126x SetRx (opcode
       0x82) entry, consumed by lorawan_pump_consume_rx1_arm_t0() below.

       Writer side is already inside enter_critical()/leave_critical() in
       TimerStart (timer_board.c critical-section scope), so the (dwt,
       valid, count) triple is updated atomically wrt the reader at the
       opcode 0x82 site. The reader side does its own critical-section
       bracket because it can run from any context that issues a SPI
       command — Python pump trampoline, MAC callback chain, etc. */
    s_rx1_arm_t0_dwt = dwt_cycles;
    s_rx1_arm_t0_valid = 1u;
    s_rx1_arm_t0_stamp_count++;
    /* Phase 8 Edit B1 — re-source provenance of s_rx_window_active away
       from the SPI transport. TimerStart for the RX1 hook is the new
       authoritative set site: a TimerStart hit means the MAC has just
       committed to opening an RX window, and any NVM save attempted
       between this point and the matching DIO1/RxTimeout MUST be
       deferred. Earlier set than the SPI-side write deleted in Edit B2
       (~1-2 ms wider deferral window). Cleared by:
         (a) pump body after consuming a DIO1 event (Edit B3 below);
         (b) mac_mcps_indication / mac_mcps_confirm / mac_mlme_confirm
             in mod_lorawan.c (existing clear sites at :888, :898, :927).
       Both clear paths remain idempotent — the earliest one wins.

       Direct-byte store: s_rx_window_active is `volatile uint8_t` and
       aligned-byte stores are atomic at the AHB bus on Armv8-M (per
       lorawan_stats.h header note). No critical section required. */
    s_rx_window_active = 1u;
}

uint32_t lorawan_pump_consume_rx1_arm_t0(void) {
    /* Phase 8 Edit A — atomic read-and-clear. Called from sx126x_spi_xfer
       at the opcode 0x82 detection block. Single-producer (TimerStart in
       timer_board.c) / single-consumer (this function via SPI path) —
       but the producer may pre-empt the consumer if a TimerStart fires
       from a hard ISR while a SPI command is in progress (currently it
       does not — TimerStart is invoked only from MAC callbacks running
       in scheduler ctx — but the critical section is cheap insurance).

       Race window without the CS: reader samples valid=1, producer
       overwrites (dwt, valid, count) with a fresh stamp, reader stores
       valid=0 — the new stamp's valid bit is lost. With the CS, producer
       blocks until reader finishes. ~100 ns on Cortex-M33 @ 100 MHz per
       master MSG R5; acceptable for a sub-ms RX-window arm path. */
    uint32_t state = MICROPY_BEGIN_ATOMIC_SECTION();
    uint32_t t0 = 0u;
    if (s_rx1_arm_t0_valid != 0u) {
        t0 = s_rx1_arm_t0_dwt;
        s_rx1_arm_t0_valid = 0u;
        s_rx1_arm_t0_dwt = 0u;
    }
    MICROPY_END_ATOMIC_SECTION(state);
    return t0;
}

void lorawan_pump_observe_rx_window_active_set_via_spi(void) {
    __atomic_fetch_add(&s_rx_window_active_set_via_spi_count, 1u,
                       __ATOMIC_RELAXED);
}

void lorawan_pump_observe_setrx_cmd_cache(void) {
    __atomic_fetch_add(&s_setrx_cmd_cache_count, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_setrx_cmd_cache_ready, 1u, __ATOMIC_RELAXED);
}

void lorawan_pump_observe_rx1_arm_to_setrx_us(uint32_t sample_us) {
    /* Single-writer (sx126x_spi_xfer at opcode 0x82). hist_record is
       not re-entrant across writers; safe under the s_spi_xfer_busy
       single-writer gate that wraps the 0x82 detection block. */
    hist_record(&s_rx1_arm_hist, sample_us);
}

void lorawan_driver_request_pump(uint8_t reason) {
    if (!s_pump_initialized) {
        return;
    }
    /* Phase 6 instrumentation (p6-instrument-001) — per-reason counter
       BEFORE the aggregate so the sum invariant holds even under
       concurrent observation. */
    if (reason < 6u) {
        __atomic_fetch_add(&s_pump_request_by_reason[reason], 1u,
                           __ATOMIC_RELAXED);
    }
    __atomic_fetch_add(&s_pump_request_count, 1u, __ATOMIC_RELAXED);

    /* Stamp t0 for this reason. Reason bounds-check; out-of-range
       reasons collapse onto INTERNAL (won't be sampled into the
       per-reason histogram but still feeds dispatch_hist). */
    uint8_t r = reason;
    if (r == 0u || r >= LORAWAN_PUMP_REASON_COUNT) {
        r = LORAWAN_PUMP_REASON_INTERNAL;
    }
    s_reason_t0[r] = DWT_CYCCNT;

    /* If the pump is already scheduled OR already running, just mark
       pending and let the in-flight pass pick the work up via the
       do-while pending loop. */
    if (s_pump_scheduled || s_process_running) {
        s_process_pending = 1u;
        return;
    }

    s_pump_scheduled = 1u;
    pump_schedule();
}

void lorawan_driver_pump_run(void) {
    /* t1 sample BEFORE any other work so dispatch latency is honest. */
    uint32_t t1 = DWT_CYCCNT;

    /* Drain every reason that has a stamped t0; push the wait into the
       appropriate histogram. The dispatch_hist gets samples from every
       reason (request->run total wait); the dio1/rx1 histograms get
       samples only from their own reason. */
    for (uint32_t r = 1u; r < LORAWAN_PUMP_REASON_COUNT; r++) {
        uint32_t t0 = s_reason_t0[r];
        if (t0 == 0u) {
            continue;
        }
        s_reason_t0[r] = 0u;
        uint32_t dt_us = cyc_to_us(t1 - t0);
        hist_record(&s_dispatch_hist, dt_us);
        /* p7-instrument-003 — feed the per-reason split using the
           t0-slot index r (matches the aggregate record above on the
           same iteration). Out-of-range r is impossible here since the
           loop guard is r < LORAWAN_PUMP_REASON_COUNT == 6. */
        hist_record(&s_dispatch_hist_by_reason[r], dt_us);
        if (r == LORAWAN_PUMP_REASON_DIO1) {
            hist_record(&s_dio1_hist, dt_us);
        }
        /* Phase 8 — Phase 6 placeholder contribution into s_rx1_arm_hist
           on REASON_TIMER is removed. That latency was actually pump-
           request→pump-run for TIMER reason, NOT TimerStart→SetRx. The
           dispatch_hist_by_reason[TIMER] split still captures the former
           cleanly. Phase 8 wires the SPI-side opcode 0x82 t1 into
           s_rx1_arm_hist via lorawan_pump_observe_rx1_arm_to_setrx_us,
           which is the canonical TimerStart→SetRx pair-closure. Mixing
           the two sources would dilute the p99 vs the HARD release gate
           of 2000 µs (operator decision 10). */
    }

    /* Defensive re-entry guard. mp_sched serialises trampoline
       invocations between bytecodes, so this branch should never fire;
       the counter is here to prove it. */
    if (s_process_running) {
        __atomic_fetch_add(&s_mac_process_reentry_count, 1u, __ATOMIC_RELAXED);
        return;
    }

    /* Defer if SX1262 SPI transport is mid-transaction. The pump pass
       runs in scheduler context, so a SPI call from this site would
       hit the same s_spi_xfer_busy gate that increments
       spi_nested_reject_count anyway — we'd rather skip pre-emptively
       and reschedule than waste a Radio.IrqProcess() pass. */
    if (sx126x_spi_busy_probe()) {
        __atomic_fetch_add(&s_pump_deferred_spi_busy_count, 1u, __ATOMIC_RELAXED);
        /* Re-stamp INTERNAL t0 so the deferred pass shows real latency. */
        s_reason_t0[LORAWAN_PUMP_REASON_RESCHED] = DWT_CYCCNT;
        pump_schedule();
        return;
    }

    if (dflash_busy_probe()) {
        __atomic_fetch_add(&s_pump_deferred_flash_busy_count, 1u, __ATOMIC_RELAXED);
        s_reason_t0[LORAWAN_PUMP_REASON_RESCHED] = DWT_CYCCNT;
        pump_schedule();
        return;
    }

    s_process_running = 1u;
    __atomic_fetch_add(&s_pump_run_count, 1u, __ATOMIC_RELAXED);

    do {
        s_process_pending = 0u;
#if defined(LORAWAN_C_PUMP_ENABLE) && (LORAWAN_C_PUMP_ENABLE != 0)
        /* Phase 7 — consume DIO1 hard-ISR pending flag in safe context.
           Order is HARD-mandatory: RadioOnDioIrq() (called via the
           encapsulated sx126x_board_dispatch_dio1_irq helper so the
           pump TU does not link directly against radio.c) MUST run
           BEFORE LoRaMacProcess() so the MAC state machine sees the
           radio IRQ on this pump iteration. Read the (t0, seq) pair
           via the sx126x_board accessor, close the dio1_to_pump_us
           histogram, then snapshot the seq for race-detection. */
        if (s_radio_irq_pending) {
            s_radio_irq_pending = 0u;

            uint32_t t0_dwt = 0u;
            uint32_t isr_seq = 0u;
            lorawan_dio1_state_get(&t0_dwt, &isr_seq);

            uint32_t t1 = DWT_CYCCNT;
            uint32_t dt_us = cyc_to_us(t1 - t0_dwt);
            hist_record(&s_dio1_hist, dt_us);

            s_dio1_pump_seen_seq = isr_seq;

            /* Phase 8 Edit B3 — RX window finished. Any DIO1 fire while
               s_rx_window_active==1 is RxDone, RxTimeout, or RxError; in
               all three cases the window is over. Clearing here makes
               NvmDataMgmtStore() free to write flash as soon as the MAC
               callback chain returns. The mod_lorawan.c callback clears
               at :888 / :898 / :927 remain idempotent — they re-clear
               the same byte plus call NvmDataMgmtFlushDeferred(), which
               handles a no-op flush if we already opened the gate here.

               The next RX2 / class-C continuous-RX TimerStart will re-set
               the flag inside lorawan_pump_stamp_rx1_arm_t0(); a SetRx
               without a fresh TimerStart (retransmit code path) does NOT
               re-arm the deferral — which is correct, because there is
               no MAC-side RX window to protect in that case. */
            s_rx_window_active = 0u;

            sx126x_board_dispatch_dio1_irq();
        }
        LoRaMacProcess();
#endif
    } while (s_process_pending);

    s_process_running = 0u;
    s_pump_scheduled = 0u;
}

void lorawan_pump_stats_get(lorawan_pump_stats_t *out) {
    if (out == NULL) {
        return;
    }
    out->mac_process_reentry_count =
        __atomic_load_n(&s_mac_process_reentry_count, __ATOMIC_RELAXED);
    out->pump_request_count =
        __atomic_load_n(&s_pump_request_count, __ATOMIC_RELAXED);
    out->pump_run_count =
        __atomic_load_n(&s_pump_run_count, __ATOMIC_RELAXED);
    out->pump_deferred_spi_busy_count =
        __atomic_load_n(&s_pump_deferred_spi_busy_count, __ATOMIC_RELAXED);
    out->pump_deferred_flash_busy_count =
        __atomic_load_n(&s_pump_deferred_flash_busy_count, __ATOMIC_RELAXED);
    out->rx1_arm_t0_stamp_count =
        __atomic_load_n(&s_rx1_arm_t0_stamp_count, __ATOMIC_RELAXED);

    out->pump_dispatch_latency_us_p50 = hist_percentile(&s_dispatch_hist, 50u);
    out->pump_dispatch_latency_us_p95 = hist_percentile(&s_dispatch_hist, 95u);
    out->pump_dispatch_latency_us_p99 = hist_percentile(&s_dispatch_hist, 99u);
    out->pump_dispatch_latency_us_max = s_dispatch_hist.max_us;
    out->pump_dispatch_latency_us_count = s_dispatch_hist.count;
    out->pump_dispatch_latency_us_first_sample_boot_ms =
        s_dispatch_hist.first_sample_boot_ms;

    out->dio1_to_pump_us_p50 = hist_percentile(&s_dio1_hist, 50u);
    out->dio1_to_pump_us_p95 = hist_percentile(&s_dio1_hist, 95u);
    out->dio1_to_pump_us_p99 = hist_percentile(&s_dio1_hist, 99u);
    out->dio1_to_pump_us_max = s_dio1_hist.max_us;
    out->dio1_to_pump_us_count = s_dio1_hist.count;
    out->dio1_to_pump_us_first_sample_boot_ms =
        s_dio1_hist.first_sample_boot_ms;

    out->rx1_arm_to_setrx_us_p50 = hist_percentile(&s_rx1_arm_hist, 50u);
    out->rx1_arm_to_setrx_us_p95 = hist_percentile(&s_rx1_arm_hist, 95u);
    out->rx1_arm_to_setrx_us_p99 = hist_percentile(&s_rx1_arm_hist, 99u);
    out->rx1_arm_to_setrx_us_max = s_rx1_arm_hist.max_us;
    out->rx1_arm_to_setrx_us_count = s_rx1_arm_hist.count;
    out->rx1_arm_to_setrx_us_first_sample_boot_ms =
        s_rx1_arm_hist.first_sample_boot_ms;

    /* p7-instrument-003 — per-reason dispatch split. Computes 6 sets;
       idx 0 / 5 will read 0 for all fields when no samples flowed (no
       reason==RESERVED or ==INTERNAL appears in the t0 loop unless
       the defer-then-retry path stamped INTERNAL t0, which IS valid
       Phase 7 behaviour). */
    for (uint32_t i = 0; i < 6u; i++) {
        out->pump_dispatch_latency_by_reason_p50[i] =
            hist_percentile(&s_dispatch_hist_by_reason[i], 50u);
        out->pump_dispatch_latency_by_reason_p95[i] =
            hist_percentile(&s_dispatch_hist_by_reason[i], 95u);
        out->pump_dispatch_latency_by_reason_p99[i] =
            hist_percentile(&s_dispatch_hist_by_reason[i], 99u);
        out->pump_dispatch_latency_by_reason_max[i] =
            s_dispatch_hist_by_reason[i].max_us;
        out->pump_dispatch_latency_by_reason_count[i] =
            s_dispatch_hist_by_reason[i].count;
        out->pump_dispatch_latency_by_reason_first_sample_boot_ms[i] =
            s_dispatch_hist_by_reason[i].first_sample_boot_ms;
    }

    out->dio1_dispatch_max_us =
        __atomic_load_n(&s_dio1_dispatch_max_us, __ATOMIC_RELAXED);
    out->spi_busy_wait_max_us =
        __atomic_load_n(&s_spi_busy_wait_max_us, __ATOMIC_RELAXED);

    for (uint32_t i = 0; i < 6u; i++) {
        out->pump_request_by_reason[i] =
            __atomic_load_n(&s_pump_request_by_reason[i], __ATOMIC_RELAXED);
    }

    out->rx_window_active_set_via_spi_count =
        __atomic_load_n(&s_rx_window_active_set_via_spi_count, __ATOMIC_RELAXED);
    out->setrx_cmd_cache_count =
        __atomic_load_n(&s_setrx_cmd_cache_count, __ATOMIC_RELAXED);
    out->setrx_cmd_cache_ready =
        __atomic_load_n(&s_setrx_cmd_cache_ready, __ATOMIC_RELAXED);
}

void lorawan_pump_observe_dio1_dispatch_us(uint32_t sample_us) {
    /* Single-writer (sx126x_board_dispatch_dio1_irq, pump body context).
       Relaxed load + compare + store is race-free under that invariant; the
       only other reader is lorawan_pump_stats_get() which takes a snapshot. */
    uint32_t prev = __atomic_load_n(&s_dio1_dispatch_max_us, __ATOMIC_RELAXED);
    if (sample_us > prev) {
        __atomic_store_n(&s_dio1_dispatch_max_us, sample_us, __ATOMIC_RELAXED);
    }
}

void lorawan_pump_observe_spi_busy_wait_us(uint32_t sample_us) {
    /* Two writer sites (pre-CS and DTC-yield BUSY-wait loops in
       sx126x_spi_xfer); both protected by s_spi_xfer_busy single-writer
       gate, so the load+CAS is race-free. Worst-case across both sites. */
    uint32_t prev = __atomic_load_n(&s_spi_busy_wait_max_us, __ATOMIC_RELAXED);
    if (sample_us > prev) {
        __atomic_store_n(&s_spi_busy_wait_max_us, sample_us, __ATOMIC_RELAXED);
    }
}

uint32_t lorawan_dio1_pump_seen_seq_get(void) {
    return s_dio1_pump_seen_seq;
}

/* ---- Trampoline / scheduling ----------------------------------------- */

static void pump_trampoline(mp_sched_node_t *node) {
    (void)node;
    lorawan_driver_pump_run();
}

static void pump_schedule(void) {
    (void)mp_sched_schedule_node(&s_pump_sched_node, pump_trampoline);
}

#else  /* !MICROPY_HW_LORA_STACK_RENESAS — keep symbols for link clean */

void lorawan_pump_init(void) { }
void lorawan_pump_deinit(void) { }
bool lorawan_pump_is_running(void) { return false; }
bool lorawan_pump_is_pending(void) { return false; }
void lorawan_pump_stamp_dio1_t0(void) { }
void lorawan_pump_stamp_rx1_arm_t0(uint32_t dwt_cycles) { (void)dwt_cycles; }
uint32_t lorawan_pump_consume_rx1_arm_t0(void) { return 0u; }
void lorawan_pump_observe_rx_window_active_set_via_spi(void) { }
void lorawan_pump_observe_setrx_cmd_cache(void) { }
void lorawan_pump_observe_rx1_arm_to_setrx_us(uint32_t sample_us) { (void)sample_us; }
void lorawan_driver_request_pump(uint8_t reason) { (void)reason; }
void lorawan_driver_pump_run(void) { }
void lorawan_pump_stats_get(lorawan_pump_stats_t *out) {
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}
volatile uint8_t s_radio_irq_pending;
uint32_t lorawan_dio1_pump_seen_seq_get(void) { return 0u; }
void lorawan_pump_observe_dio1_dispatch_us(uint32_t sample_us) { (void)sample_us; }
void lorawan_pump_observe_spi_busy_wait_us(uint32_t sample_us) { (void)sample_us; }

#endif /* MICROPY_HW_LORA_STACK_RENESAS */
