/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
 *
 * Renesas LoRaWAN C-stack — MicroPython binding (Phase 1).
 *
 * Phase 1 exposes the SX126x radio HAL to Python so we can validate the
 * SPI transport, BUSY polling, reset sequence, and DIO1 IRQ wiring before
 * the LoRaMac state machine lands (Phase 3+).
 *
 * Pin defaults come from the bench-tested Python driver
 *   ports/renesas-ra/boards/VK_RA4M2/examples/LoRa/lorawan_upstream/
 *      lorawan_app.py:1056-1059
 * and are kept as compile-time defaults so a parameterless
 *   >>> import lorawan
 *   >>> mac = lorawan.Mac()
 *   >>> mac.radio_init()
 * does the right thing on VK_RA4M2 + Wio-SX1262.
 */

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/smallint.h"  /* Z3 — MP_SMALL_INT_MAX for saturating writes */

#include "mod_lorawan.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

#include "pin.h"
#include "extint.h"
#include "glue/sx126x_board.h"
#include "glue/timer_board.h"
#include "glue/nvm_board.h"
#include "glue/dflash.h"
#include "glue/lorawan_stats.h"
#include "glue/lorawan_rxc_diag.h"
#include "glue/lorawan_pump.h"

/* Z0 heap counters — see
 *   LORAWAN_TESTS/coordination/ZERO_ALLOC_REMOVAL_PLAN_2026-05-13.md §Z0
 * for the design rationale (post-init alloc bar, ISR tripwire). The
 * counters wrap every mp_obj_new_* call site in this file via the
 * MOD_LORAWAN_NEW_* macros below. Vendor LoRaMac + radio + SPI drivers
 * are alloc-free per MEM_AUDIT_DESIGN_2026-05-13.md §2; instrumenting
 * mod_lorawan.c alone covers the entire C alloc surface.
 *
 * Under -DLORAWAN_OBSERVATION_DISABLE the macros collapse to direct
 * mp_obj_new_* calls so the reference build keeps zero counter cost.
 *
 * SCB->ICSR / SCB_ICSR_VECTACTIVE_Msk come via py/mphal.h →
 * mphalport.h → RA HAL → CMSIS device header (same chain used by
 * lorawan_stats_dwt_init below and by pendsv.c).
 */

#ifdef LORAWAN_OBSERVATION_DISABLE

#define MOD_LORAWAN_HEAP_BUMP(field)            ((void)0)
#define MOD_LORAWAN_HEAP_ARM_POST_INIT()        ((void)0)
#define MOD_LORAWAN_HEAP_CLEAR()                ((void)0)

#define MOD_LORAWAN_NEW_TUPLE(n, items)         mp_obj_new_tuple((n), (items))
#define MOD_LORAWAN_NEW_DICT(n)                 mp_obj_new_dict((n))
#define MOD_LORAWAN_NEW_BYTES(p, n)             mp_obj_new_bytes((p), (n))
#define MOD_LORAWAN_NEW_STR(p, n)               mp_obj_new_str((p), (n))
#define MOD_LORAWAN_NEW_BOOL(v)                 mp_obj_new_bool((v))
#define MOD_LORAWAN_NEW_INT_U(v)                mp_obj_new_int_from_uint((v))
#define MOD_LORAWAN_NEW_INT_ULL(v)              mp_obj_new_int_from_ull((v))

#else  /* observation enabled — Z0 heap counters live */

/* Z0 leaf storage. RAM-only globals; NOT part of g_lorawan_stats (no
 * struct ABI change to flash NVM payloads). All atomic-relaxed to match
 * the rest of the observation surface. */
typedef struct mod_lorawan_heap_stats {
    uint32_t c_alloc_count;             /* every mp_obj_new_* in this file */
    uint32_t c_free_count;              /* paired counter (placeholder — */
                                        /* no explicit m_del/free sites today) */
    uint32_t mp_alloc_count_post_init;  /* armed=1 after lorawan_init returns */
    uint32_t mp_free_count_post_init;
    uint32_t isr_alloc_count;           /* TRIPWIRE — always 0 if correct */
    uint32_t init_baseline_count;       /* snapshot of c_alloc_count at arm */
    uint8_t  observation_armed;         /* 0 before init returns, 1 after */
    uint8_t  _pad[3];
} mod_lorawan_heap_stats_t;

static volatile mod_lorawan_heap_stats_t s_heap = {0};

/* T-V3.1 — Class C RX probe storage. See glue/lorawan_rxc_diag.h.
 * Unconditional (not gated by LORAWAN_OBSERVATION_DISABLE) because the
 * capture sites live in vendor LoRaMac.c / radio.c, and the leaves are
 * read from mac.stats()['rxc'] regardless of observation build mode. */
volatile mod_lorawan_rxc_diag_t lorawan_rxc_diag = {0};

#define MOD_LORAWAN_HEAP_BUMP(field) \
    ((void)__atomic_fetch_add((uint32_t *)&s_heap.field, 1u, __ATOMIC_RELAXED))

#define MOD_LORAWAN_HEAP_ARM_POST_INIT() do {                                  \
    /* snapshot baseline BEFORE flipping armed=1 so the relationship */        \
    /* delta = c_alloc_count - init_baseline_count holds atomically */         \
    /* w.r.t. any subsequent allocation. */                                    \
    uint32_t _base = __atomic_load_n((uint32_t *)&s_heap.c_alloc_count,        \
                                     __ATOMIC_RELAXED);                        \
    __atomic_store_n((uint32_t *)&s_heap.init_baseline_count, _base,           \
                     __ATOMIC_RELAXED);                                        \
    __atomic_store_n((uint8_t *)&s_heap.observation_armed, 1u,                 \
                     __ATOMIC_RELAXED);                                        \
} while (0)

#define MOD_LORAWAN_HEAP_CLEAR() do {                                          \
    memset((void *)&s_heap, 0, sizeof(s_heap));                                \
} while (0)

static inline void mod_lorawan_heap_count_alloc(void) {
    MOD_LORAWAN_HEAP_BUMP(c_alloc_count);
    /* SCB_ICSR.VECTACTIVE is nonzero whenever we are inside an exception
     * or IRQ handler (Cortex-M33 ARMv8-M ARM B3.4.3). Any nonzero hit
     * here is a hard-bar violation — vendor drivers and our glue must
     * never reach Python-object constructors in ISR context. */
    if ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0u) {
        MOD_LORAWAN_HEAP_BUMP(isr_alloc_count);
    }
    if (__atomic_load_n((uint8_t *)&s_heap.observation_armed,
                        __ATOMIC_RELAXED)) {
        MOD_LORAWAN_HEAP_BUMP(mp_alloc_count_post_init);
    }
}

#define MOD_LORAWAN_NEW_TUPLE(n, items) \
    (mod_lorawan_heap_count_alloc(), mp_obj_new_tuple((n), (items)))
#define MOD_LORAWAN_NEW_DICT(n) \
    (mod_lorawan_heap_count_alloc(), mp_obj_new_dict((n)))
#define MOD_LORAWAN_NEW_BYTES(p, n) \
    (mod_lorawan_heap_count_alloc(), mp_obj_new_bytes((p), (n)))
#define MOD_LORAWAN_NEW_STR(p, n) \
    (mod_lorawan_heap_count_alloc(), mp_obj_new_str((p), (n)))
#define MOD_LORAWAN_NEW_BOOL(v) \
    (mod_lorawan_heap_count_alloc(), mp_obj_new_bool((v)))
#define MOD_LORAWAN_NEW_INT_U(v) \
    (mod_lorawan_heap_count_alloc(), mp_obj_new_int_from_uint((v)))
#define MOD_LORAWAN_NEW_INT_ULL(v) \
    (mod_lorawan_heap_count_alloc(), mp_obj_new_int_from_ull((v)))

#endif  /* LORAWAN_OBSERVATION_DISABLE */

/* LoRaMac stack — only available when LORAWAN_BUILD_PHASE >= 4 (the
   imported Renesas C tree is part of the build). Phase 5+ wires this
   in through static primitives + callbacks defined below. */
#if defined(LORAWAN_BUILD_PHASE) && (LORAWAN_BUILD_PHASE >= 4)
#define LORAWAN_PHASE5_AVAILABLE  (1)
#include "LoRaMac.h"
#include "glue/board.h"
#else
#define LORAWAN_PHASE5_AVAILABLE  (0)
#endif

// ---- Pin defaults (mpconfigboard.h-compatible literals) -----------------
//
// Resolved at Mac.__init__ via machine_pin_find() so the Python user can
// override any of them. Defaults match the proven Python LoRaWAN app.

#define LORAWAN_DEFAULT_SPI_ID      (3)
#define LORAWAN_DEFAULT_SPI_BAUD    (8000000)
#define LORAWAN_DEFAULT_CS_QSTR     MP_QSTR_P206
#define LORAWAN_DEFAULT_RESET_QSTR  MP_QSTR_P001
#define LORAWAN_DEFAULT_BUSY_QSTR   MP_QSTR_P002
#define LORAWAN_DEFAULT_DIO1_QSTR   MP_QSTR_P015
#define LORAWAN_DEFAULT_RF_SW_QSTR  MP_QSTR_P100

// ---- Mac object ----------------------------------------------------------

typedef struct _lorawan_mac_obj_t {
    mp_obj_base_t base;
    qstr region;
    bool radio_initialized;
    bool timer_initialized;
    bool stack_initialized;        // Phase 5 — LoRaMacInitialization succeeded
    bool keys_set;                  // Phase 5 — DEVEUI/JOINEUI/APPKEY loaded
    bool joined;                    // Phase 5b — OTAA join completed

    // Phase 5c — RX queue. Indications arrive asynchronously from the
    // MCPS layer; we copy them here for Python `mac.recv()` polling.
    // Single-slot for now — LoRaMac never delivers two indications at
    // once on a single end-device, so a full ring is overkill.
    bool     rx_pending;
    uint8_t  rx_port;
    uint16_t rx_len;
    uint8_t  rx_buf[256];
    /* Callbacks live in MP_STATE_VM(lorawan_mac_root_callbacks)[0..2]
       so the GC sees them during root scan. Indices: 0=dio1, 1=event,
       2=test_timer. Per HARDFAULT_ZALLOC_2026-05-13 §root-cause: this
       singleton is in .bss, which gc_collect_root() does NOT walk, so
       closure cells held only via these slots would otherwise be swept. */
    TimerEvent_t test_timer;       // backing storage for Mac.arm_oneshot test

    // Stable storage for OTAA credentials. LoRaMac MIB takes pointers
    // and never copies; the bytes objects from Python must outlive the
    // call. We copy into these arrays at set_keys() time.
    uint8_t deveui[8];
    uint8_t joineui[8];
    uint8_t appkey[16];

    /* Last LinkCheckAns from server (populated by mac_mlme_confirm
       when MlmeRequest==MLME_LINK_CHECK and Status==OK). */
    bool    link_check_valid;
    int8_t  link_check_margin;       // dB above demod sensitivity, 0..254
    uint8_t link_check_gateways;     // number of GWs that heard the uplink
} lorawan_mac_obj_t;

static lorawan_mac_obj_t lorawan_mac_singleton;

/* HardFault breadcrumb storage. Lives in .noinit (NOLOAD) so the value
 * written by the last SBC() call survives both the fault and the JLink
 * hardware nRESET that follows; see boards/VK_RA4M2/ra4m2ac3cfm.ld
 * SECTIONS{.noinit (NOLOAD)} and bsp system.c (BSS-only memset).
 * RA4M2 has no D-cache, so no cache maintenance needed. The magic is
 * written at first Mac() construction so a power-cycle (RAM = X) reads
 * back as "not valid", distinguishing it from a stale-but-real crumb. */
__attribute__((section(".noinit"))) uint16_t lorawan_bc_last;
__attribute__((section(".noinit"))) uint32_t lorawan_bc_magic;

/* GC root slots for the three Python callbacks. Routed via MP_STATE_VM so
   gc_collect_root() can see them — the singleton itself lives in .bss and
   would otherwise hide closure cells from the collector. See
   HARDFAULT_ZALLOC_2026-05-13.md §1. The MP_REGISTER_ROOT_POINTER directive
   sits at end-of-file with the other port-level root pointers. */
enum {
    LORAWAN_CB_DIO1 = 0,
    LORAWAN_CB_EVENT = 1,
    LORAWAN_CB_TEST_TIMER = 2,
    LORAWAN_CB_COUNT = 3,  /* KEEP IN SYNC with literal "3" in MP_REGISTER_ROOT_POINTER below; the qstr/root-pointer collector parses this file before enums are visible. */
};
#define LORAWAN_CB_SLOT(i)  (MP_STATE_VM(lorawan_mac_root_callbacks)[(i)])

/* Phase 1 Observation — single owner of the counter storage. Includes
 * sentinel init for BUSY opcode fields per Step 3 §5; 0xFF means "no
 * opcode observed yet" and is also re-applied after lorawan_stats_reset(). */
volatile lorawan_stats_t g_lorawan_stats = {
    .busy_last_opcode = 0xFFu,
    .busy_timeout_opcode = 0xFFu,
};

void lorawan_stats_reset(void) {
    uint32_t state = MICROPY_BEGIN_ATOMIC_SECTION();
    memset((void *)&g_lorawan_stats, 0, sizeof(g_lorawan_stats));
    g_lorawan_stats.busy_last_opcode = 0xFFu;
    g_lorawan_stats.busy_timeout_opcode = 0xFFu;
    MICROPY_END_ATOMIC_SECTION(state);
}

/* Idempotent DWT CYCCNT enable for the ISR cycle-max metrics. DWT may
 * already be enabled by mphalport.c:141 (mp_hal_ticks_cpu_enable) on
 * other code paths — the guarded write makes this safe to call from any
 * LoRa init entry. If the bit cannot be set (locked / DebugMon owned),
 * CYCCNT stays 0 and hard_isr_*_cycles_max reads as 0 — acceptable per
 * phase1_step3_atomicity.md §6 (zero-bin vs inflated). DWT/CoreDebug
 * macros come via py/mphal.h → mphalport.h → RA_HAL_H CMSIS chain.
 *
 * After enabling we run a short probe (a few __NOP() + __DSB()) and
 * compare CYCCNT before/after. s_dwt_available reflects whether CYCCNT
 * actually advanced — false means cycle-max readings are useless and
 * Step 12's mac.stats() will surface that to Python. Step 0 §10 / Step 3
 * §6 required runtime confirmation, not just a successful bit-set. */
volatile bool s_dwt_available = false;

/* Phase 1 Step 10/10.5 — RX-window-active flag. Set in sx126x_spi_xfer
 * when staging SetRx (0x82); cleared in mac_mcps_indication,
 * mac_mlme_confirm, and mac_mcps_confirm; read in NvmDataMgmtStore.
 * All sites run in scheduler/Python context. Aligned byte stores on Armv8-M
 * are atomic at the bus level, so a plain volatile uint8_t is correct
 * without atomics. See lorawan_stats.h for the full clear-edge contract. */
volatile uint8_t s_rx_window_active = 0;

/* Z2 — packed-SMALL_INT event dispatch. The earlier static-tuple ring
 * (commit afb75f64) HardFaulted on first MAC event after boot: rebinding
 * a struct onto mp_type_tuple is not a valid mp_obj_t under the current
 * runtime — mp_sched_schedule eventually walks the object through a path
 * that assumes a real heap-allocated tuple. Replacement: pack (tag_id,
 * status) into a single SMALL_INT (tagged pointer, zero allocation).
 *   layout:  bits 0..7   = tag_id  (uint8, see mac_post_event)
 *            bits 8..31  = status  (int24, sign-extended on unpack)
 *
 * Safe-range proof: on this port MP_SMALL_INT_MAX = 2^29 - 1. The packed
 * value is `(status << 8) | tag_id`, so |packed| < 2^31. We need
 * |packed| < 2^29, i.e. |status| < 2^21. LoRaMacEventInfoStatus_t is an
 * enum with values 0..~20 (LORAMAC_EVENT_INFO_STATUS_*), well inside
 * the bound. mac_error also passes small LoRaMacStatus_t codes. The
 * 24-bit field is intentionally generous; tighten if any new caller
 * ever passes a large status.
 *
 * Backpressure: s_event_drop_count ticks when mp_sched_schedule returns
 * false (scheduler queue full → event dispatched dropped). Nonzero ⇒
 * the Python pump (mac.process() cadence) fell behind. Exposed in
 * mac.stats()['heap']['event_drop_count']. */
#ifndef LORAWAN_OBSERVATION_DISABLE
static volatile uint32_t s_event_drop_count;
#endif

#ifndef LORAWAN_OBSERVATION_DISABLE
void lorawan_stats_dwt_init(void) {
    #if __CORTEX_M >= 4 && __CORTEX_M != 23
    /* P3.0 / Bug-2 — robust enable + read-back-verify with retries.
       Prior versions raced when CYCCNT was sampled before TRCENA fully
       latched (observed ~50% True/False across cold boots). Sequence:
         1. force TRCENA + reset CYCCNT + set CYCCNTENA, each with DSB.
         2. retry the NOP-probe up to 8 times; accept the first observed
            CYCCNT advance. Single retry already covered first-boot race;
            8 is generous belt-and-braces for cold flash + JLink reset. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();
    __ISB();
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    bool ok = false;
    for (int attempt = 0; attempt < 8 && !ok; attempt++) {
        /* Per-iteration barrier (architect review): if TRCENA latch
           propagation lags, tight-looping reads the same dead value;
           need a fresh DSB/ISB + a few NOPs of wall-time between tries. */
        __DSB();
        __ISB();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        uint32_t t0 = DWT->CYCCNT;
        __NOP(); __NOP(); __NOP(); __NOP();
        __DSB();
        uint32_t t1 = DWT->CYCCNT;
        ok = (t1 != t0);
    }
    s_dwt_available = ok;
    #else
    s_dwt_available = false;
    #endif
}
#else
void lorawan_stats_dwt_init(void) {}
#endif

// C-side trampoline for DIO1 IRQ. Runs in scheduler context (via direct
// ICU registration in glue/sx126x_board.c). Closes over the singleton
// to reach the Mac instance — matches the upstream LoRaMac convention
// `typedef void (DioIrqHandler)(void)`.
static void lorawan_dio1_py_trampoline(void) {
    lorawan_mac_obj_t *self = &lorawan_mac_singleton;
    mp_obj_t cb = LORAWAN_CB_SLOT(LORAWAN_CB_DIO1);
    if (cb == MP_OBJ_NULL || cb == mp_const_none) {
        #ifndef LORAWAN_OBSERVATION_DISABLE
        s_event_drop_count++;
        #endif
        return;
    }
    (void)mp_sched_schedule(cb, MP_OBJ_FROM_PTR(self));
}

// Trampoline for arm_oneshot test callback. Upstream TimerEvent_t
// callback signature is `void(*)(void)` — we close over the singleton
// to reach the Mac instance.
static void lorawan_oneshot_py_trampoline(void) {
    lorawan_mac_obj_t *self = &lorawan_mac_singleton;
    mp_obj_t cb = LORAWAN_CB_SLOT(LORAWAN_CB_TEST_TIMER);
    if (cb == MP_OBJ_NULL || cb == mp_const_none) {
        #ifndef LORAWAN_OBSERVATION_DISABLE
        s_event_drop_count++;
        #endif
        return;
    }
    (void)mp_sched_schedule(cb, MP_OBJ_FROM_PTR(self));
}

static void lorawan_mac_print(const mp_print_t *print, mp_obj_t self_in,
    mp_print_kind_t kind) {
    (void)kind;
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print,
        "Mac(region=%q, phase=%d, radio=%s)",
        self->region, LORAWAN_PHASE,
        self->radio_initialized ? "init" : "uninit");
}

static const machine_pin_obj_t *resolve_pin(mp_obj_t user_obj, qstr default_qstr) {
    if (user_obj == mp_const_none) {
        return machine_pin_find(MP_OBJ_NEW_QSTR(default_qstr));
    }
    return machine_pin_find(user_obj);
}

static mp_obj_t lorawan_mac_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_region, ARG_spi_id, ARG_spi_baud,
           ARG_cs, ARG_reset, ARG_busy, ARG_dio1, ARG_rf_sw };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_region,   MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(MP_QSTR_EU868) } },
        { MP_QSTR_spi_id,   MP_ARG_KW_ONLY | MP_ARG_INT,
            { .u_int = LORAWAN_DEFAULT_SPI_ID } },
        { MP_QSTR_spi_baud, MP_ARG_KW_ONLY | MP_ARG_INT,
            { .u_int = LORAWAN_DEFAULT_SPI_BAUD } },
        { MP_QSTR_cs,       MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_reset,    MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_busy,     MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_dio1,     MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_rf_sw,    MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_NONE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    qstr region_qstr = mp_obj_str_get_qstr(args[ARG_region].u_obj);
    if (region_qstr != MP_QSTR_EU868) {
        mp_raise_ValueError(MP_ERROR_TEXT("only EU868 supported in Phase 1"));
    }

    lorawan_mac_obj_t *self = &lorawan_mac_singleton;
    self->base.type = type;
    self->region = region_qstr;
    /* Mark the .noinit breadcrumb slot as valid for this boot. Leave
     * lorawan_bc_last untouched so any stale crumb from a prior fault
     * remains readable until the next SBC() overwrites it. */
    lorawan_bc_magic = LORAWAN_BC_MAGIC;
    self->radio_initialized = false;
    self->timer_initialized = false;
    self->stack_initialized = false;
    self->keys_set = false;
    self->joined = false;
    /* Z0 — reset heap counters at Mac() so each fresh instance starts
       with observation_armed=0 and zeroed leaves. */
    MOD_LORAWAN_HEAP_CLEAR();
    #ifndef LORAWAN_OBSERVATION_DISABLE
    s_event_drop_count = 0;
    #endif
    self->rx_pending = false;
    self->rx_port = 0;
    self->rx_len = 0;
    LORAWAN_CB_SLOT(LORAWAN_CB_DIO1) = mp_const_none;
    LORAWAN_CB_SLOT(LORAWAN_CB_EVENT) = mp_const_none;
    LORAWAN_CB_SLOT(LORAWAN_CB_TEST_TIMER) = mp_const_none;
    memset(self->deveui, 0, sizeof(self->deveui));
    memset(self->joineui, 0, sizeof(self->joineui));
    memset(self->appkey, 0, sizeof(self->appkey));

    // Resolve pins now so a wrong pin name fails fast at construction.
    const machine_pin_obj_t *cs_pin =
        resolve_pin(args[ARG_cs].u_obj,    LORAWAN_DEFAULT_CS_QSTR);
    const machine_pin_obj_t *rst_pin =
        resolve_pin(args[ARG_reset].u_obj, LORAWAN_DEFAULT_RESET_QSTR);
    const machine_pin_obj_t *busy_pin =
        resolve_pin(args[ARG_busy].u_obj,  LORAWAN_DEFAULT_BUSY_QSTR);
    const machine_pin_obj_t *dio1_pin =
        resolve_pin(args[ARG_dio1].u_obj,  LORAWAN_DEFAULT_DIO1_QSTR);
    const machine_pin_obj_t *rf_sw_pin =
        resolve_pin(args[ARG_rf_sw].u_obj, LORAWAN_DEFAULT_RF_SW_QSTR);

    sx126x_board_cfg_t cfg = {
        .spi_id      = (uint8_t)args[ARG_spi_id].u_int,
        .spi_baud_hz = (uint32_t)args[ARG_spi_baud].u_int,
        .cs_pin      = (void *)cs_pin,
        .reset_pin   = (void *)rst_pin,
        .busy_pin    = (void *)busy_pin,
        .dio1_pin    = (void *)dio1_pin,
        .rf_sw_pin   = (void *)rf_sw_pin,
    };
    /* DWT CYCCNT — required before any ISR can sample cycle deltas for
       hard_isr_*_cycles_max. Idempotent; safe to call repeatedly. */
    lorawan_stats_dwt_init();

    /* Phase 4 — initialise the guarded C pump state machine in the
       constructor so request_pump() / pump_run() are live from object
       birth. lorawan_pump_init() is idempotent (checks s_pump_initialized
       and bails if already done), so the later call inside
       lorawan_mac_lorawan_init() remains harmless. */
    lorawan_pump_init();

    sx126x_board_init(&cfg);
    self->radio_initialized = true;

    // AGT4 1 kHz tick + deadline-walk service. Reserves AGT4 (= machine
    // Timer 5 slot) for the duration of the Mac instance.
    timer_board_init();
    self->timer_initialized = true;

    // Phase 6a.2 — open data flash + restore any persisted blob from
    // the previous boot's session into our RAM cache. NVM contents
    // become available to LoRaMac once `lorawan_init()` runs.
    nvm_board_init();

    return MP_OBJ_FROM_PTR(self);
}

// ---- Phase 1 radio HAL methods ------------------------------------------

static mp_obj_t lorawan_mac_radio_reset(mp_obj_t self_in) {
    (void)self_in;
    SX126xReset();
    SX126xWaitOnBusy();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_radio_reset_obj,
    lorawan_mac_radio_reset);

static mp_obj_t lorawan_mac_radio_get_status(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(sx126x_phase1_get_status());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_radio_get_status_obj,
    lorawan_mac_radio_get_status);

static mp_obj_t lorawan_mac_radio_read_reg(mp_obj_t self_in, mp_obj_t addr_in) {
    (void)self_in;
    mp_int_t addr = mp_obj_get_int(addr_in);
    if (addr < 0 || addr > 0xFFFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("addr 0..0xFFFF"));
    }
    return MP_OBJ_NEW_SMALL_INT(sx126x_phase1_read_register((uint16_t)addr));
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_radio_read_reg_obj,
    lorawan_mac_radio_read_reg);

static mp_obj_t lorawan_mac_radio_write_reg(mp_obj_t self_in,
    mp_obj_t addr_in, mp_obj_t val_in) {
    (void)self_in;
    mp_int_t addr = mp_obj_get_int(addr_in);
    mp_int_t val  = mp_obj_get_int(val_in);
    if (addr < 0 || addr > 0xFFFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("addr 0..0xFFFF"));
    }
    if (val < 0 || val > 0xFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("value 0..0xFF"));
    }
    sx126x_phase1_write_register((uint16_t)addr, (uint8_t)val);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(lorawan_mac_radio_write_reg_obj,
    lorawan_mac_radio_write_reg);

static mp_obj_t lorawan_mac_radio_busy(mp_obj_t self_in) {
    (void)self_in;
    return MOD_LORAWAN_NEW_BOOL(sx126x_phase1_busy_high());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_radio_busy_obj,
    lorawan_mac_radio_busy);

// DIO1 callback — wired through direct ICU registration (Phase 2). The
// hard IRQ posts a sched-node into the glue layer, which calls our C
// trampoline `lorawan_dio1_py_trampoline`, which in turn schedules the
// user's Python callback via mp_sched_schedule. Pass None to unhook.
static mp_obj_t lorawan_mac_set_dio1_callback(mp_obj_t self_in,
    mp_obj_t callback) {
    (void)self_in;
    if (callback == mp_const_none) {
        SX126xIoIrqDeinit();
        LORAWAN_CB_SLOT(LORAWAN_CB_DIO1) = mp_const_none;
    } else {
        if (!mp_obj_is_callable(callback)) {
            mp_raise_TypeError(MP_ERROR_TEXT("callback not callable"));
        }
        LORAWAN_CB_SLOT(LORAWAN_CB_DIO1) = callback;
        SX126xIoIrqInit(lorawan_dio1_py_trampoline);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_dio1_callback_obj,
    lorawan_mac_set_dio1_callback);

// Optional BUSY-pin falling-edge IRQ (item 3 — fast path). Returns True
// if the BUSY pin has an ICU vector and the IRQ is now active.
static mp_obj_t lorawan_mac_enable_busy_irq(mp_obj_t self_in) {
    (void)self_in;
    return MOD_LORAWAN_NEW_BOOL(SX126xBusyIrqEnable());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_enable_busy_irq_obj,
    lorawan_mac_enable_busy_irq);

static mp_obj_t lorawan_mac_disable_busy_irq(mp_obj_t self_in) {
    (void)self_in;
    SX126xBusyIrqDisable();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_disable_busy_irq_obj,
    lorawan_mac_disable_busy_irq);

// Runtime SPI inter-byte gap (item 4).
static mp_obj_t lorawan_mac_set_spi_interbyte_us(mp_obj_t self_in,
    mp_obj_t us_in) {
    (void)self_in;
    mp_int_t us = mp_obj_get_int(us_in);
    if (us < 0 || us > 255) {
        mp_raise_ValueError(MP_ERROR_TEXT("us 0..255"));
    }
    sx126x_board_set_interbyte_us((uint8_t)us);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_spi_interbyte_us_obj,
    lorawan_mac_set_spi_interbyte_us);

static mp_obj_t lorawan_mac_get_spi_interbyte_us(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(sx126x_board_get_interbyte_us());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_get_spi_interbyte_us_obj,
    lorawan_mac_get_spi_interbyte_us);

/* Debug surface — expose internal SPI ID + a raw 1-byte exchange that
   bypasses the sx126x_spi_xfer wrapper so we can compare against the
   Python `spi.write_readinto` path directly. */
static mp_obj_t lorawan_mac_dbg_spi_id(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(sx126x_board_get_spi_id());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_dbg_spi_id_obj,
    lorawan_mac_dbg_spi_id);

static mp_obj_t lorawan_mac_dbg_xchg(mp_obj_t self_in, mp_obj_t b_in) {
    (void)self_in;
    mp_int_t b = mp_obj_get_int(b_in);
    return MP_OBJ_NEW_SMALL_INT(sx126x_board_debug_xchg((uint8_t)b));
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_dbg_xchg_obj,
    lorawan_mac_dbg_xchg);

#if LORAWAN_PHASE5_AVAILABLE

#include "radio/radio.h"
#include "radio/sx126x.h"

/* === Scan helpers — raw chip access through Renesas Radio API ===========
 *
 * Thin Python bindings that directly call the Renesas radio driver
 * functions from radio/radio.c. They go through:
 *
 *   Python  →  Radio.SetChannel/Rx/Rssi/...  →  sx126x.c  →  sx126x_board.c
 *                                                              (our adapter)
 *                                                              ra_sci_spi
 *                                                              SCI3 + DTC
 *
 * Used to validate the adapter independently of the LoRaMac state machine.
 * If a Python-driven scan via these helpers produces sensible RSSI, the
 * adapter is correct and any LoRaMac-level failure is in higher-level
 * timing/IRQ handling. If the scan fails, the bug is in the adapter
 * itself. */

static mp_obj_t lorawan_mac_scan_standby(mp_obj_t self_in) {
    (void)self_in;
    Radio.Standby();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_scan_standby_obj,
    lorawan_mac_scan_standby);

static mp_obj_t lorawan_mac_scan_set_freq(mp_obj_t self_in, mp_obj_t freq_in) {
    (void)self_in;
    uint32_t freq = (uint32_t)mp_obj_get_int(freq_in);
    Radio.SetChannel(freq);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_scan_set_freq_obj,
    lorawan_mac_scan_set_freq);

/* scan_set_lora_rx(sf, bw_idx)
 *   sf      LoRa spreading factor [5..12]
 *   bw_idx  LoRa bandwidth index per radio.h §SetRxConfig:
 *           0=125 kHz, 1=250 kHz, 2=500 kHz
 * Continuous-RX mode, fixed CR=4/5, preamble=8, no CRC, no IQ-invert. */
static mp_obj_t lorawan_mac_scan_set_lora_rx(size_t n_args, const mp_obj_t *args) {
    if (n_args != 3) {
        mp_raise_TypeError(MP_ERROR_TEXT("scan_set_lora_rx(sf, bw_idx)"));
    }
    uint32_t sf = (uint32_t)mp_obj_get_int(args[1]);
    uint32_t bw_idx = (uint32_t)mp_obj_get_int(args[2]);
    RadioResult_t res = Radio.SetRxConfig(
        MODEM_LORA,
        bw_idx,
        sf,
        1,       /* coderate 4/5 */
        0,       /* bandwidthAfc — n/a for LoRa */
        8,       /* preambleLen */
        0,       /* symbTimeout — continuous mode */
        false,   /* fixLen */
        0,       /* payloadLen */
        false,   /* crcOn */
        false,   /* freqHopOn */
        0,       /* hopPeriod */
        false,   /* iqInverted */
        true);   /* rxContinuous */
    return MP_OBJ_NEW_SMALL_INT((int)res);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_scan_set_lora_rx_obj, 3,
    lorawan_mac_scan_set_lora_rx);

static mp_obj_t lorawan_mac_scan_rx_continuous(mp_obj_t self_in) {
    (void)self_in;
    RadioResult_t res = Radio.Rx(0);
    return MP_OBJ_NEW_SMALL_INT((int)res);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_scan_rx_continuous_obj,
    lorawan_mac_scan_rx_continuous);

/* Direct SetRx(timeout) — bypasses Radio.Rx() which forces SetRx(0)
   when rxContinuous=true. SX126x interprets timeout=0 as Rx-single
   mode. To get true continuous RX (rssi_scanner.py reference),
   pass 0xFFFFFF explicitly. */
static mp_obj_t lorawan_mac_scan_set_rx_raw(mp_obj_t self_in, mp_obj_t timeout_in) {
    (void)self_in;
    uint32_t timeout = (uint32_t)mp_obj_get_int(timeout_in);
    SX126xSetRx(timeout);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_scan_set_rx_raw_obj,
    lorawan_mac_scan_set_rx_raw);

/* SX126xGetDeviceErrors — reports chip-side error flags. Useful when
   a state-changing command (SetRx/SetTx) returns cmd_status=5. */
static mp_obj_t lorawan_mac_scan_get_errors(mp_obj_t self_in) {
    (void)self_in;
    RadioError_t errs = SX126xGetDeviceErrors();
    return MOD_LORAWAN_NEW_INT_U(errs.Value);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_scan_get_errors_obj,
    lorawan_mac_scan_get_errors);

/* Disable DIO1 ICU IRQ at runtime — diagnostic for re-entrancy hypothesis.
   Once disabled, RadioOnDioIrq won't run and LoRaMac won't process TxDone/
   RxDone events, but our adapter's SPI calls will be free of nested-
   dispatch interference from the scheduler. Use only for diagnostic
   testing — disabling permanently breaks join/send. */
static mp_obj_t lorawan_mac_disable_dio1_irq(mp_obj_t self_in) {
    (void)self_in;
    SX126xIoIrqDeinit();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_disable_dio1_irq_obj,
    lorawan_mac_disable_dio1_irq);

static mp_obj_t lorawan_mac_scan_rssi(mp_obj_t self_in) {
    (void)self_in;
    int16_t rssi = Radio.Rssi(MODEM_LORA);
    return MP_OBJ_NEW_SMALL_INT((int)rssi);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_scan_rssi_obj,
    lorawan_mac_scan_rssi);

#endif /* LORAWAN_PHASE5_AVAILABLE */

// AGT4 monotonic ms (item 2 — timer service test surface).
static mp_obj_t lorawan_mac_now_ms(mp_obj_t self_in) {
    (void)self_in;
    return MOD_LORAWAN_NEW_INT_U(lorawan_timer_now_ms());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_now_ms_obj, lorawan_mac_now_ms);

// Schedule a Python callback after `ms` milliseconds via the AGT4 timer
// service. Single-shot; only one test_timer slot per Mac instance.
static mp_obj_t lorawan_mac_arm_oneshot(mp_obj_t self_in,
    mp_obj_t ms_in, mp_obj_t callback_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t ms = mp_obj_get_int(ms_in);
    if (ms < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("ms must be >= 0"));
    }
    if (callback_in != mp_const_none && !mp_obj_is_callable(callback_in)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback not callable"));
    }
    LORAWAN_CB_SLOT(LORAWAN_CB_TEST_TIMER) = callback_in;
    if (!lorawan_timer_oneshot(&self->test_timer, (uint32_t)ms,
            lorawan_oneshot_py_trampoline)) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(lorawan_mac_arm_oneshot_obj,
    lorawan_mac_arm_oneshot);

// Phase 3 v1: arm with µs precision (AGT4 → AGT5 hand-off in last 5 ms).
// Total range = 32-bit µs (~71 minutes). Same single-slot test_timer
// storage as arm_oneshot — call this OR arm_oneshot, not both at once.
static mp_obj_t lorawan_mac_arm_oneshot_us(mp_obj_t self_in,
    mp_obj_t us_in, mp_obj_t callback_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t us = mp_obj_get_int(us_in);
    if (us < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("us must be >= 0"));
    }
    if (callback_in != mp_const_none && !mp_obj_is_callable(callback_in)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback not callable"));
    }
    LORAWAN_CB_SLOT(LORAWAN_CB_TEST_TIMER) = callback_in;
    if (!lorawan_timer_oneshot_us(&self->test_timer, (uint32_t)us,
            lorawan_oneshot_py_trampoline)) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(lorawan_mac_arm_oneshot_us_obj,
    lorawan_mac_arm_oneshot_us);

static mp_obj_t lorawan_mac_deinit(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    SX126xIoIrqDeinit();
    SX126xBusyIrqDisable();
    sx126x_board_deinit();
    self->radio_initialized = false;
    if (self->timer_initialized) {
        timer_board_deinit();
        self->timer_initialized = false;
    }
    LORAWAN_CB_SLOT(LORAWAN_CB_DIO1) = mp_const_none;
    LORAWAN_CB_SLOT(LORAWAN_CB_TEST_TIMER) = mp_const_none;
    /* Phase 4 — drop pump scheduling state so a subsequent Mac() ctor
       starts from clean counters. */
    lorawan_pump_deinit();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_deinit_obj, lorawan_mac_deinit);

// ---- Phase 5 — LoRaMac stack binding -----------------------------------

#if LORAWAN_PHASE5_AVAILABLE

/* LoRaMac primitive callbacks. Called from `LoRaMacProcess()` (which
   we run in scheduler context — see lorawan_mac_process below), so it
   is safe to schedule a Python callback via mp_sched_schedule. We do
   not pass the C struct pointer through to Python because the data is
   copied/released by LoRaMac after the callback returns; instead we
   forward a small QSTR tag so Python can switch on event type. */

/* Forward the LoRaMac primitive event to the user's Python callback as
   a packed SMALL_INT (tagged pointer — zero allocation in this path).
   Encoding: bits 0..7 = tag_id, bits 8..31 = status (sign-extended on
   unpack). The Python helper `_lorawan_test_helpers.make_ev_cb` performs
   the unpack. tag_id values are stable wire-contract (also mirrored in
   the helper's _TAG_TO_ID table): 0=mcps_confirm, 1=mcps_indication,
   2=mlme_confirm, 3=mlme_indication, 4=mac_error. Any new tag MUST be
   added at the next free id AND in the helper LUT in the same change.

   The qstr argument is kept on the C side for source clarity at call
   sites (mac_mcps_confirm etc. read better with MP_QSTR_mcps_confirm
   than with a magic 0) but is mapped to a small int here. The mapping
   is a compile-time switch — no allocation, no string materialisation.
   Runs in scheduler/Python context (LoRaMacProcess is serialised by
   mac.process()); mp_sched_schedule queues the bound callback for
   dispatch between bytecodes. */
static void mac_post_event(qstr tag, int status) {
    mp_obj_t cb = LORAWAN_CB_SLOT(LORAWAN_CB_EVENT);
    if (cb == MP_OBJ_NULL || cb == mp_const_none) {
        #ifndef LORAWAN_OBSERVATION_DISABLE
        s_event_drop_count++;
        #endif
        return;
    }
    uint8_t tag_id;
    switch (tag) {
        case MP_QSTR_mcps_confirm:    tag_id = 0u; break;
        case MP_QSTR_mcps_indication: tag_id = 1u; break;
        case MP_QSTR_mlme_confirm:    tag_id = 2u; break;
        case MP_QSTR_mlme_indication: tag_id = 3u; break;
        case MP_QSTR_mac_error:       tag_id = 4u; break;
        default:                      tag_id = 255u; break;
    }
    int32_t packed = ((int32_t)status << 8) | (int32_t)tag_id;
    mp_obj_t arg = MP_OBJ_NEW_SMALL_INT(packed);
    if (!mp_sched_schedule(cb, arg)) {
        #ifndef LORAWAN_OBSERVATION_DISABLE
        s_event_drop_count++;
        #endif
    }
}

static void mac_mcps_confirm(McpsConfirm_t *cnf) {
    /* Step 10.5: see lorawan_stats.h for the full clear-edge contract.
       Class A uplink with NO downlink completes via the LoRaMac RX-timeout
       path: ProcessRadioRxTimeout -> HandleRadioRxErrorTimeout -> MacDone=1
       -> MacMcpsConfirm, with no MacMcpsIndication. Without this clear edge,
       s_rx_window_active stays latched after the RX window closes and the
       next NvmDataMgmtStore() is falsely counted as in-window. */
    s_rx_window_active = 0u;
    NvmDataMgmtFlushDeferred();  /* P3.4 — drain pending save */
    mac_post_event(MP_QSTR_mcps_confirm, (int)cnf->Status);
}

static void mac_mcps_indication(McpsIndication_t *ind) {
    /* Phase 1 Step 10 — a real MCPS downlink indication closes the active
       RX window. The no-downlink timeout path is covered separately by
       mac_mcps_confirm (Step 10.5); MLME exchanges are covered by
       mac_mlme_confirm. */
    s_rx_window_active = 0;
    NvmDataMgmtFlushDeferred();  /* P3.4 — drain pending save */
    /* Mirror the upstream Renesas LoRaSample handler
       (samples/.../LoRaSample/main.c:240-258): copy payload only when
       Status is OK and BufferSize > 0. RxData and (BufferSize != 0)
       are equivalent in upstream LoRaMac.c (set/cleared together for
       FRAME_TYPE_A/D), so the explicit RxData test is the canonical
       form here. Overwrites any unread previous message (single-slot
       policy — Class A end-devices only see a downlink at most once
       per uplink, so a queue is unnecessary). */
    if (ind->RxData) {
        lorawan_mac_obj_t *self = &lorawan_mac_singleton;
        size_t n = ind->BufferSize;
        if (n > sizeof(self->rx_buf)) {
            n = sizeof(self->rx_buf);
        }
        memcpy(self->rx_buf, ind->Buffer, n);
        self->rx_port = ind->Port;
        self->rx_len  = (uint16_t)n;
        self->rx_pending = true;
    }
    mac_post_event(MP_QSTR_mcps_indication, (int)ind->Status);
}

static void mac_mlme_confirm(MlmeConfirm_t *cnf) {
    /* Phase 1 Step 10 — MLME-level confirm (JOIN, LINK_CHECK, ...) closes
       any outstanding RX window the MAC opened to receive the response.
       Clear here as a second-source so an MCPS-less MLME exchange (Join,
       LinkCheck) does not leave the flag latched. */
    s_rx_window_active = 0;
    NvmDataMgmtFlushDeferred();  /* P3.4 — drain pending save */
    /* On OTAA join completion (MLME_JOIN), update the cached join state
       so Python `mac.is_joined()` becomes true without an extra MIB
       round-trip. Other MLME types (LINK_CHECK, TXCW, etc.) just pass
       through to the user's event sink. */
    if (cnf->MlmeRequest == MLME_JOIN &&
        cnf->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
        lorawan_mac_singleton.joined = true;
    }
    if (cnf->MlmeRequest == MLME_LINK_CHECK &&
        cnf->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
        lorawan_mac_singleton.link_check_margin = cnf->DemodMargin;
        lorawan_mac_singleton.link_check_gateways = cnf->NbGateways;
        lorawan_mac_singleton.link_check_valid = true;
    }
    mac_post_event(MP_QSTR_mlme_confirm, (int)cnf->Status);
}

static void mac_mlme_indication(MlmeIndication_t *ind) {
    mac_post_event(MP_QSTR_mlme_indication, (int)ind->Status);
}

static uint8_t mac_get_battery_level(void) {
    return BoardGetBatteryLevel();
}

static float mac_get_temperature_level(void) {
    return 25.0f;  /* placeholder — board has no temperature sensor */
}

static void mac_nvm_context_change(uint32_t notifyMibFlags) {
    (void)notifyMibFlags;
    /* Phase 6a — LoRaMac flagged a persistent-state change (DevNonce,
       FCnt, session keys, ...). Snapshot the contexts into RAM-backed
       NVM. Phase 6a.2 will replace the RAM buffer with R_FLASH_HP BGO
       writes; the call sequence stays the same. */
    (void)NvmDataMgmtStore();
}

/* Phase 5 — MacProcessNotify now feeds the guarded C pump. Historical
   context: a prior direct mp_sched_schedule of LoRaMacProcess from this
   callback caused re-entrancy: scheduled dispatch fired from inside the
   SX1262 adapter's BUSY-poll yield mid-sx126x_spi_xfer, triggering nested
   LoRaMacProcess → RadioIrqProcess → SX126x* SPI commands that clobbered
   the outer call's static s_tx_buf/s_rx_buf and the chip's command FIFO
   state. The guarded pump (glue/lorawan_pump.c) replaces that path: it
   coalesces requests via mp_sched_schedule_node, defers against
   sx126x_board_spi_busy() and flash-busy gates, and brackets
   LoRaMacProcess() with an s_process_running re-entry guard. See
   BOUNDARY_AUDIT.md §Risk(c). */

static void mac_process_notify(void) {
    lorawan_driver_request_pump(LORAWAN_PUMP_REASON_NOTIFY);
}

static void mac_error_notify(LoRaMacErrorNotificationStatus_t status) {
    mac_post_event(MP_QSTR_mac_error, (int)status);
}

static LoRaMacPrimitives_t s_primitives = {
    .MacMcpsConfirm    = mac_mcps_confirm,
    .MacMcpsIndication = mac_mcps_indication,
    .MacMlmeConfirm    = mac_mlme_confirm,
    .MacMlmeIndication = mac_mlme_indication,
};

static LoRaMacCallback_t s_callbacks = {
    .GetBatteryLevel     = mac_get_battery_level,
    .GetTemperatureLevel = mac_get_temperature_level,
    .NvmContextChange    = mac_nvm_context_change,
    .MacProcessNotify    = mac_process_notify,
    .MacErrorNotify      = mac_error_notify,
};

static mp_obj_t lorawan_mac_lorawan_init(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->radio_initialized || !self->timer_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("radio/timer not ready"));
    }
    LoRaMacStatus_t st = LoRaMacInitialization(&s_primitives, &s_callbacks,
        LORAMAC_REGION_EU868);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("LoRaMacInitialization failed: %d"), (int)st);
    }
    /* Phase 6a — restore persisted contexts (DevNonce, FCnt, session
       keys) from NVM if a previous boot saved them. Must run AFTER
       LoRaMacInitialization (which allocates internal context buffers
       that MIB_NVM_CTXS set will fill). Returns 0 on a fresh / empty
       NVM, otherwise total bytes restored. */
    (void)NvmDataMgmtRestore();

    /* Bring the MAC out of stopped state so MlmeRequest/McpsRequest
       are accepted. LoRaMacStart() is the canonical follow-up to
       LoRaMacInitialization in the upstream sample apps. */
    st = LoRaMacStart();
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("LoRaMacStart failed: %d"), (int)st);
    }
    self->stack_initialized = true;
    /* Phase 4 — initialise the guarded C pump state machine + counter
       histograms. Idempotent: a previous deinit() left storage stale
       but the init zeros everything. Must happen after stack_initialized
       so request_pump() called from any Phase-5+ ISR wiring finds a
       running pump. */
    lorawan_pump_init();
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NETWORK_ACTIVATION;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        self->joined = (mib.Param.NetworkActivation != ACTIVATION_TYPE_NONE);
    } else {
        self->joined = false;
    }
    /* Z0 — arm post-init alloc observation. Every mp_obj_new_* via
       MOD_LORAWAN_NEW_* after this point counts toward
       mp_alloc_count_post_init; init_baseline_count is snapshotted at
       this instant so the delta (c_alloc_count - init_baseline_count)
       isolates post-init activity. */
    MOD_LORAWAN_HEAP_ARM_POST_INIT();
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_lorawan_init_obj,
    lorawan_mac_lorawan_init);

static void copy_key(mp_obj_t key_obj, uint8_t *dst, size_t expected) {
    mp_buffer_info_t buf;
    mp_get_buffer_raise(key_obj, &buf, MP_BUFFER_READ);
    if (buf.len != expected) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("expected %d-byte key, got %d"),
            (int)expected, (int)buf.len);
    }
    memcpy(dst, buf.buf, expected);
}

static mp_obj_t lorawan_mac_set_keys(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_deveui, ARG_joineui, ARG_appkey };
    if (n_args != 4) {
        mp_raise_TypeError(MP_ERROR_TEXT(
            "set_keys(deveui:8B, joineui:8B, appkey:16B)"));
    }
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(args[ARG_self]);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init() first"));
    }

    copy_key(args[ARG_deveui],  self->deveui,  8);
    copy_key(args[ARG_joineui], self->joineui, 8);
    copy_key(args[ARG_appkey],  self->appkey,  16);

    MibRequestConfirm_t mib;
    LoRaMacStatus_t st;

    mib.Type = MIB_DEV_EUI;
    mib.Param.DevEui = self->deveui;
    st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_DEV_EUI failed: %d"), (int)st);
    }

    mib.Type = MIB_JOIN_EUI;
    mib.Param.JoinEui = self->joineui;
    st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_JOIN_EUI failed: %d"), (int)st);
    }

    /* LoRaWAN 1.0.x: AppKey == NwkKey at the network root. We set both
       so the stack works with either spec interpretation. */
    mib.Type = MIB_NWK_KEY;
    mib.Param.NwkKey = self->appkey;
    st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_NWK_KEY failed: %d"), (int)st);
    }

    mib.Type = MIB_APP_KEY;
    mib.Param.AppKey = self->appkey;
    st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_APP_KEY failed: %d"), (int)st);
    }

    self->keys_set = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_set_keys_obj, 4,
    lorawan_mac_set_keys);

/* Phase 1 Observation — wraps the single LoRaMacProcess() call site so
 * mac.stats() can report process count, latest duration, and historical
 * peak. mac_process_last_us is single-writer (only this Python pump
 * touches it) so STATS_STORE (a plain volatile store) is sufficient; the
 * count and max use the atomic / CAS macros. Under
 * LORAWAN_OBSERVATION_DISABLE the t0/dt timing collapses entirely so
 * LoRaMacProcess() is the only thing that runs. */
static void instrument_mac_process_call(void) {
    #ifndef LORAWAN_OBSERVATION_DISABLE
    uint32_t t0 = mp_hal_ticks_us();
    #endif
    LoRaMacProcess();
    #ifndef LORAWAN_OBSERVATION_DISABLE
    uint32_t dt = mp_hal_ticks_us() - t0;
    STATS_INC(mac_process_count);
    STATS_STORE(mac_process_last_us, dt);
    STATS_UPDATE_MAX(mac_process_max_us, dt);
    #endif
}

static mp_obj_t lorawan_mac_process(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        return mp_const_none;
    }
    instrument_mac_process_call();
    /* Phase 4 — exercise the guarded C pump from the Python side so
       TR-5 can observe pump_request_count and pump_run_count climb.
       The pump body is gated on LORAWAN_C_PUMP_ENABLE; in Phase 4 the
       body is a no-op (LoRaMacProcess is NOT called from here), so
       this introduces no behavioural change to the live r13-fix path
       running via instrument_mac_process_call above. */
    lorawan_driver_request_pump(LORAWAN_PUMP_REASON_PY);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_process_obj, lorawan_mac_process);

static mp_obj_t lorawan_mac_set_event_callback(mp_obj_t self_in,
    mp_obj_t cb_in) {
    (void)self_in;
    if (cb_in != mp_const_none && !mp_obj_is_callable(cb_in)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback not callable"));
    }
    LORAWAN_CB_SLOT(LORAWAN_CB_EVENT) = cb_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_event_callback_obj,
    lorawan_mac_set_event_callback);

#else  /* LORAWAN_PHASE5_AVAILABLE == 0 — stubs for Phase 0..3 builds. */

static mp_obj_t lorawan_mac_phase5_unavailable(size_t n_args,
    const mp_obj_t *args) {
    (void)n_args; (void)args;
    mp_raise_NotImplementedError(MP_ERROR_TEXT(
        "rebuild with LORAWAN_BUILD_PHASE>=4"));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_lorawan_init_obj, 1,
    lorawan_mac_phase5_unavailable);
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_set_keys_obj, 1,
    lorawan_mac_phase5_unavailable);
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_process_obj, 1,
    lorawan_mac_phase5_unavailable);
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_set_event_callback_obj, 1,
    lorawan_mac_phase5_unavailable);

#endif  /* LORAWAN_PHASE5_AVAILABLE */

#if LORAWAN_PHASE5_AVAILABLE

/* Phase 5b — OTAA join.
 *
 * `mac.join(datarate=5)` triggers an asynchronous MLME_JOIN. The call
 * returns immediately with the LoRaMac status code (0 = accepted). The
 * actual join completion arrives later as an `mlme_confirm` event
 * delivered via `set_event_callback`, which sets `mac.is_joined()` to
 * True on success.
 *
 * EU868 datarates: 0=SF12/125kHz ... 5=SF7/125kHz. Default 5 (fastest)
 * matches typical TTN OTAA practice.
 */
static mp_obj_t lorawan_mac_join(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_datarate };
    if (n_args < 1 || n_args > 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("join([datarate])"));
    }
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(args[ARG_self]);
    if (!self->stack_initialized || !self->keys_set) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init + set_keys first"));
    }
    int8_t datarate = 5;
    if (n_args == 2) {
        mp_int_t dr = mp_obj_get_int(args[ARG_datarate]);
        if (dr < 0 || dr > 5) {
            mp_raise_ValueError(MP_ERROR_TEXT("EU868 datarate 0..5"));
        }
        datarate = (int8_t)dr;
    }

    /* Reset cached join state — will be set true again from
       mac_mlme_confirm when the JoinAccept arrives. */
    self->joined = false;
    __atomic_store_n(&lorawan_rxc_diag.last_rx_rssi_dbm,    0, __ATOMIC_RELAXED);
    __atomic_store_n(&lorawan_rxc_diag.last_rx_snr_db,      0, __ATOMIC_RELAXED);
    __atomic_store_n(&lorawan_rxc_diag.last_rx_stats_valid, 0, __ATOMIC_RELAXED);

    MlmeReq_t req;
    memset(&req, 0, sizeof(req));
    req.Type = MLME_JOIN;
    req.Req.Join.Datarate = datarate;
    LoRaMacStatus_t st = LoRaMacMlmeRequest(&req);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_join_obj, 1, lorawan_mac_join);

static mp_obj_t lorawan_mac_is_joined(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MOD_LORAWAN_NEW_BOOL(self->joined);
}

/* MLME_LINK_CHECK — queues a MAC LinkCheckReq command to be piggybacked
   on the next uplink. After the server's LinkCheckAns arrives via the
   downlink, `mac.last_link_check()` returns (margin_dB, gateway_count).
   Returns the LoRaMacStatus_t code from the queue request. */
static mp_obj_t lorawan_mac_link_check(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    self->link_check_valid = false;   // arm new request
    MlmeReq_t req;
    memset(&req, 0, sizeof(req));
    req.Type = MLME_LINK_CHECK;
    LoRaMacStatus_t st = LoRaMacMlmeRequest(&req);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_link_check_obj,
    lorawan_mac_link_check);

static mp_obj_t lorawan_mac_last_link_check(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->link_check_valid) {
        return mp_const_none;
    }
    mp_obj_t items[2] = {
        MP_OBJ_NEW_SMALL_INT(self->link_check_margin),
        MP_OBJ_NEW_SMALL_INT(self->link_check_gateways),
    };
    return MOD_LORAWAN_NEW_TUPLE(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_last_link_check_obj,
    lorawan_mac_last_link_check);

static mp_obj_t lorawan_mac_get_datarate(mp_obj_t self_in) {
    (void)self_in;
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_CHANNELS_DATARATE;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        return mp_const_none;
    }
    return MP_OBJ_NEW_SMALL_INT(mib.Param.ChannelsDatarate);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_get_datarate_obj,
    lorawan_mac_get_datarate);

/* MLME_DEVICE_TIME — queues a MAC DeviceTimeReq command. Server
   responds with DeviceTimeAns (5 bytes: GPS seconds + 1/256 s
   fraction). The Ans triggers MlmeConfirm with Type=MLME_DEVICE_TIME;
   we capture LastTxSysTime / SysTime via MIB after that. */
static mp_obj_t lorawan_mac_device_time_req(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    MlmeReq_t req;
    memset(&req, 0, sizeof(req));
    req.Type = MLME_DEVICE_TIME;
    LoRaMacStatus_t st = LoRaMacMlmeRequest(&req);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_device_time_req_obj,
    lorawan_mac_device_time_req);

static mp_obj_t lorawan_mac_get_sys_time(mp_obj_t self_in) {
    (void)self_in;
    /* MIB_SYSTEM_TIME / similar isn't directly exposed in this fork;
       LastTxSysTime is the closest bookkeeping value. Not perfect for
       absolute GPS time but proves the DeviceTimeAns payload was
       parsed and applied. Returns (seconds, sub_second). */
    SysTime_t t = SysTimeGet();
    mp_obj_t items[2] = {
        MOD_LORAWAN_NEW_INT_U((uint32_t)t.Seconds),
        MP_OBJ_NEW_SMALL_INT(t.SubSeconds),
    };
    return MOD_LORAWAN_NEW_TUPLE(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_get_sys_time_obj,
    lorawan_mac_get_sys_time);

/* Switch end-device class — accepts 'A' or 'C'. Class B requires
   LORAMAC_CLASSB_ENABLED at compile time and a beacon-broadcasting
   gateway, currently not enabled in this build. */
static mp_obj_t lorawan_mac_set_class(mp_obj_t self_in, mp_obj_t cls_obj) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    const char *s = mp_obj_str_get_str(cls_obj);
    DeviceClass_t cls;
    if (s[0] == 'A' || s[0] == 'a') cls = CLASS_A;
    else if (s[0] == 'B' || s[0] == 'b') cls = CLASS_B;
    else if (s[0] == 'C' || s[0] == 'c') cls = CLASS_C;
    else {
        mp_raise_ValueError(MP_ERROR_TEXT("class must be 'A', 'B' or 'C'"));
    }
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_DEVICE_CLASS;
    mib.Param.Class = cls;
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_class_obj,
    lorawan_mac_set_class);

static mp_obj_t lorawan_mac_get_class(mp_obj_t self_in) {
    (void)self_in;
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_DEVICE_CLASS;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        return mp_const_none;
    }
    char c = (mib.Param.Class == CLASS_A) ? 'A' :
             (mib.Param.Class == CLASS_B) ? 'B' :
             (mib.Param.Class == CLASS_C) ? 'C' : '?';
    return MOD_LORAWAN_NEW_STR(&c, 1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_get_class_obj,
    lorawan_mac_get_class);

/* Class B — start beacon acquisition (~3-5 min until first beacon
   lock at default ±30 ppm clock tolerance). Returns LoRaMacStatus_t. */
static mp_obj_t lorawan_mac_beacon_acquisition(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    MlmeReq_t req;
    memset(&req, 0, sizeof(req));
    req.Type = MLME_BEACON_ACQUISITION;
    LoRaMacStatus_t st = LoRaMacMlmeRequest(&req);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_beacon_acquisition_obj,
    lorawan_mac_beacon_acquisition);

/* Class B — set ping-slot periodicity 0..7. Default 0 = every 32 s
   (4 ping slots per 128 s beacon). 7 = every 0.25 s (512 slots per
   beacon, max receive frequency, max battery cost). Server-side
   profile must match. Returns LoRaMacStatus_t. */
static mp_obj_t lorawan_mac_set_ping_slot_periodicity(mp_obj_t self_in,
    mp_obj_t period_obj) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    mp_int_t p = mp_obj_get_int(period_obj);
    if (p < 0 || p > 7) {
        mp_raise_ValueError(MP_ERROR_TEXT("periodicity 0..7"));
    }
    MlmeReq_t req;
    memset(&req, 0, sizeof(req));
    req.Type = MLME_PING_SLOT_INFO;
    req.Req.PingSlotInfo.PingSlot.Fields.Periodicity = (uint8_t)p;
    LoRaMacStatus_t st = LoRaMacMlmeRequest(&req);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_ping_slot_periodicity_obj,
    lorawan_mac_set_ping_slot_periodicity);
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_is_joined_obj,
    lorawan_mac_is_joined);

/* Phase 6b — Adaptive Data Rate.
 *
 * When enabled, the LoRaMac stack auto-tunes the data rate based on
 * link quality (SNR margin from gateway link-check responses). For
 * stationary nodes this minimises airtime + power consumption. For
 * mobile nodes leave it disabled so the node keeps using a robust DR.
 *
 * Default after `lorawan_init` is ADR off.
 */
static mp_obj_t lorawan_mac_set_adr(mp_obj_t self_in, mp_obj_t enable_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    bool enable = mp_obj_is_true(enable_in);
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_ADR;
    mib.Param.AdrEnable = enable;
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_ADR set failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_adr_obj,
    lorawan_mac_set_adr);

static mp_obj_t lorawan_mac_get_adr(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        return mp_const_none;
    }
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_ADR;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        return mp_const_none;
    }
    return MOD_LORAWAN_NEW_BOOL(mib.Param.AdrEnable);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_get_adr_obj,
    lorawan_mac_get_adr);

/* Phase 7 — RX window timing tunables.
 *
 * `SystemMaxRxError` (default 10 ms) bounds how much wider LoRaMac
 * makes the RX window vs the nominal RX1/RX2 open time, to absorb
 * clock drift between the end-device and the gateway. Raising it
 * widens the window symmetrically around the nominal open time.
 *
 * `MinRxSymbols` (default 6) is the minimum number of preamble
 * symbols the chip waits for before declaring RX timeout. Raising
 * it makes the window stay open longer, especially at high SF.
 *
 * Use these when initial OTAA joins fail with `mlme_confirm` status
 * RX_TIMEOUT — the gateway's JoinAccept arrives but our RX1/RX2
 * window has already closed. 200 ms / 15 symbols are conservative
 * "always works" values; tighten back once timing is characterised.
 */
static mp_obj_t lorawan_mac_set_max_rx_error(mp_obj_t self_in,
    mp_obj_t ms_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    mp_int_t ms = mp_obj_get_int(ms_in);
    if (ms < 1 || ms > 1000) {
        mp_raise_ValueError(MP_ERROR_TEXT("max_rx_error 1..1000 ms"));
    }
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_SYSTEM_MAX_RX_ERROR;
    mib.Param.SystemMaxRxError = (uint32_t)ms;
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_SYSTEM_MAX_RX_ERROR set failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_max_rx_error_obj,
    lorawan_mac_set_max_rx_error);

static mp_obj_t lorawan_mac_rx_diag(mp_obj_t self_in) {
    /* Phase 7 — read all RX-relevant MIBs so Python can verify the
       LoRaMac stack is configured per LoRaWAN spec for downlinks. */
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    MibRequestConfirm_t mib;
    mp_obj_t dict = MOD_LORAWAN_NEW_DICT(7);

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_PUBLIC_NETWORK;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_public_network),
            MOD_LORAWAN_NEW_BOOL(mib.Param.EnablePublicNetwork));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_RX2_CHANNEL;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_rx2_freq),
            MOD_LORAWAN_NEW_INT_U(mib.Param.Rx2Channel.Frequency));
        mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_rx2_dr),
            MP_OBJ_NEW_SMALL_INT(mib.Param.Rx2Channel.Datarate));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_RX2_DEFAULT_CHANNEL;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        mp_obj_dict_store(dict,
            MP_OBJ_NEW_QSTR(MP_QSTR_rx2_default_freq),
            MOD_LORAWAN_NEW_INT_U(mib.Param.Rx2DefaultChannel.Frequency));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_SYSTEM_MAX_RX_ERROR;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        mp_obj_dict_store(dict,
            MP_OBJ_NEW_QSTR(MP_QSTR_max_rx_error_ms),
            MOD_LORAWAN_NEW_INT_U(mib.Param.SystemMaxRxError));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_MIN_RX_SYMBOLS;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        mp_obj_dict_store(dict,
            MP_OBJ_NEW_QSTR(MP_QSTR_min_rx_symbols),
            MP_OBJ_NEW_SMALL_INT(mib.Param.MinRxSymbols));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NETWORK_ACTIVATION;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        mp_obj_dict_store(dict,
            MP_OBJ_NEW_QSTR(MP_QSTR_activation),
            MP_OBJ_NEW_SMALL_INT(mib.Param.NetworkActivation));
    }

    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_rx_diag_obj,
    lorawan_mac_rx_diag);

static mp_obj_t lorawan_mac_last_rx_stats(mp_obj_t self_in) {
    (void)self_in;
    mp_obj_t t[3] = {
        MP_OBJ_NEW_SMALL_INT((int8_t)__atomic_load_n(&lorawan_rxc_diag.last_rx_rssi_dbm,   __ATOMIC_RELAXED)),
        MP_OBJ_NEW_SMALL_INT((int8_t)__atomic_load_n(&lorawan_rxc_diag.last_rx_snr_db,     __ATOMIC_RELAXED)),
        mp_obj_new_bool(__atomic_load_n(&lorawan_rxc_diag.last_rx_stats_valid, __ATOMIC_RELAXED)),
    };
    return mp_obj_new_tuple(3, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_last_rx_stats_obj, lorawan_mac_last_rx_stats);

/* r13 — RX-window diag tuple. 12-tuple as of r13-fix.
 * Order (do not reorder — Python test scripts index by position):
 *   [ 0] rx1_open_us
 *   [ 1] rx1_close_us
 *   [ 2] rx2_open_us
 *   [ 3] rx2_close_us
 *   [ 4] last_rx_done_slot_id            (uint8; 0 reset on MLME_JOIN_REQ)
 *   [ 5] rx2_skipped_total               (uint32; cumulative since boot)
 *   [ 6] last_join_rx1_window_timeout_symbols  (uint32; SYMBOLS not ms)
 *   [ 7] last_join_rx2_window_timeout_symbols  (uint32; SYMBOLS not ms)
 *   [ 8] last_join_used_override_flag    (0/1; r13-fix)
 *   [ 9] last_join_effective_min_rx_symbols (uint8; r13-fix)
 *   [10] last_join_effective_system_max_rx_error_ms (uint16 ms; r13-fix)
 *   [11] last_join_rx1_window_offset_ms  (int16 ms, signed; r13-fix —
 *                                         pre-stack-process-subtract)
 * CYCCNT is 32-bit @ 100 MHz so it wraps every ~42.9 s; the caller is
 * responsible for treating timestamps as ~modular deltas. */
static mp_obj_t lorawan_mac_rx_window_diag(mp_obj_t self_in) {
    (void)self_in;
    const uint32_t cyc_per_us = 100u; /* 100 MHz core. */
    uint32_t r1o = __atomic_load_n((uint32_t *)&lorawan_rxc_diag.last_rx1_open_cyc,  __ATOMIC_RELAXED);
    uint32_t r1c = __atomic_load_n((uint32_t *)&lorawan_rxc_diag.last_rx1_close_cyc, __ATOMIC_RELAXED);
    uint32_t r2o = __atomic_load_n((uint32_t *)&lorawan_rxc_diag.last_rx2_open_cyc,  __ATOMIC_RELAXED);
    uint32_t r2c = __atomic_load_n((uint32_t *)&lorawan_rxc_diag.last_rx2_close_cyc, __ATOMIC_RELAXED);
    uint8_t  sl  = __atomic_load_n(&lorawan_rxc_diag.last_rx_done_slot_id,           __ATOMIC_RELAXED);
    uint32_t rsk = __atomic_load_n((uint32_t *)&lorawan_rxc_diag.rx2_skipped_total,  __ATOMIC_RELAXED);
    uint32_t to1 = __atomic_load_n((uint32_t *)&lorawan_rxc_diag.last_join_rx1_window_timeout_symbols, __ATOMIC_RELAXED);
    uint32_t to2 = __atomic_load_n((uint32_t *)&lorawan_rxc_diag.last_join_rx2_window_timeout_symbols, __ATOMIC_RELAXED);
    uint8_t  ovr = __atomic_load_n(&lorawan_rxc_diag.last_join_used_override_flag,        __ATOMIC_RELAXED);
    uint8_t  efm = __atomic_load_n(&lorawan_rxc_diag.last_join_effective_min_rx_symbols,  __ATOMIC_RELAXED);
    uint16_t efe = __atomic_load_n((uint16_t *)&lorawan_rxc_diag.last_join_effective_system_max_rx_error_ms, __ATOMIC_RELAXED);
    int16_t  rwo = __atomic_load_n((int16_t  *)&lorawan_rxc_diag.last_join_rx1_window_offset_ms,             __ATOMIC_RELAXED);
    mp_obj_t t[12] = {
        mp_obj_new_int_from_uint(r1o / cyc_per_us),
        mp_obj_new_int_from_uint(r1c / cyc_per_us),
        mp_obj_new_int_from_uint(r2o / cyc_per_us),
        mp_obj_new_int_from_uint(r2c / cyc_per_us),
        MP_OBJ_NEW_SMALL_INT(sl),
        mp_obj_new_int_from_uint(rsk),
        mp_obj_new_int_from_uint(to1),
        mp_obj_new_int_from_uint(to2),
        MP_OBJ_NEW_SMALL_INT(ovr),
        MP_OBJ_NEW_SMALL_INT(efm),
        MP_OBJ_NEW_SMALL_INT(efe),
        MP_OBJ_NEW_SMALL_INT(rwo),
    };
    return mp_obj_new_tuple(12, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_rx_window_diag_obj, lorawan_mac_rx_window_diag);

/* mac.pump_diag() — Phase 4 introspection. Returns a dict with the
 * guarded-C-pump counter surface (operator decisions 5 + 9 + 10).
 *
 * Three histogram quartets are exposed as sub-dicts ({p50, p95, p99, max}
 * in microseconds). In Phase 4 only pump_dispatch_latency_us produces
 * non-zero samples (request->run wait); dio1_to_pump_us and
 * rx1_arm_to_setrx_us read 0 until Phase 7 / 6 wire their t0 stamps.
 *
 * The five mcps_* / event_cb_* counters live in g_lorawan_stats and
 * also read 0 in Phase 4 (Phase 10 wires the increment sites). Storage
 * exists so the dict schema is stable now.
 *
 * Co-exists with the legacy 12-tuple from rx_window_diag() — that ABI
 * remains stable for r13-fix demo backwards-compat. */
static mp_obj_t lorawan_mac_pump_diag(mp_obj_t self_in) {
    (void)self_in;
    lorawan_pump_stats_t s;
    lorawan_pump_stats_get(&s);

    mp_obj_t dict = MOD_LORAWAN_NEW_DICT(24);

    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_mac_process_reentry_count),
                      MOD_LORAWAN_NEW_INT_U(s.mac_process_reentry_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_spi_nested_reject_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.spi_nested_reject_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_pump_request_count),
                      MOD_LORAWAN_NEW_INT_U(s.pump_request_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_pump_run_count),
                      MOD_LORAWAN_NEW_INT_U(s.pump_run_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_pump_deferred_spi_busy_count),
                      MOD_LORAWAN_NEW_INT_U(s.pump_deferred_spi_busy_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_pump_deferred_flash_busy_count),
                      MOD_LORAWAN_NEW_INT_U(s.pump_deferred_flash_busy_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_rx1_arm_t0_stamp_count),
                      MOD_LORAWAN_NEW_INT_U(s.rx1_arm_t0_stamp_count));

    mp_obj_t hd = MOD_LORAWAN_NEW_DICT(6);
    mp_obj_dict_store(hd, MP_ROM_QSTR(MP_QSTR_p50),
                      MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_us_p50));
    mp_obj_dict_store(hd, MP_ROM_QSTR(MP_QSTR_p95),
                      MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_us_p95));
    mp_obj_dict_store(hd, MP_ROM_QSTR(MP_QSTR_p99),
                      MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_us_p99));
    mp_obj_dict_store(hd, MP_ROM_QSTR(MP_QSTR_max),
                      MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_us_max));
    mp_obj_dict_store(hd, MP_ROM_QSTR(MP_QSTR_count),
                      MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_us_count));
    mp_obj_dict_store(hd, MP_ROM_QSTR(MP_QSTR_first_sample_boot_ms),
                      MOD_LORAWAN_NEW_INT_U(
                          s.pump_dispatch_latency_us_first_sample_boot_ms));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_pump_dispatch_latency_us), hd);

    mp_obj_t hi = MOD_LORAWAN_NEW_DICT(6);
    mp_obj_dict_store(hi, MP_ROM_QSTR(MP_QSTR_p50),
                      MOD_LORAWAN_NEW_INT_U(s.dio1_to_pump_us_p50));
    mp_obj_dict_store(hi, MP_ROM_QSTR(MP_QSTR_p95),
                      MOD_LORAWAN_NEW_INT_U(s.dio1_to_pump_us_p95));
    mp_obj_dict_store(hi, MP_ROM_QSTR(MP_QSTR_p99),
                      MOD_LORAWAN_NEW_INT_U(s.dio1_to_pump_us_p99));
    mp_obj_dict_store(hi, MP_ROM_QSTR(MP_QSTR_max),
                      MOD_LORAWAN_NEW_INT_U(s.dio1_to_pump_us_max));
    mp_obj_dict_store(hi, MP_ROM_QSTR(MP_QSTR_count),
                      MOD_LORAWAN_NEW_INT_U(s.dio1_to_pump_us_count));
    mp_obj_dict_store(hi, MP_ROM_QSTR(MP_QSTR_first_sample_boot_ms),
                      MOD_LORAWAN_NEW_INT_U(
                          s.dio1_to_pump_us_first_sample_boot_ms));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_dio1_to_pump_us), hi);

    mp_obj_t hr = MOD_LORAWAN_NEW_DICT(6);
    mp_obj_dict_store(hr, MP_ROM_QSTR(MP_QSTR_p50),
                      MOD_LORAWAN_NEW_INT_U(s.rx1_arm_to_setrx_us_p50));
    mp_obj_dict_store(hr, MP_ROM_QSTR(MP_QSTR_p95),
                      MOD_LORAWAN_NEW_INT_U(s.rx1_arm_to_setrx_us_p95));
    mp_obj_dict_store(hr, MP_ROM_QSTR(MP_QSTR_p99),
                      MOD_LORAWAN_NEW_INT_U(s.rx1_arm_to_setrx_us_p99));
    mp_obj_dict_store(hr, MP_ROM_QSTR(MP_QSTR_max),
                      MOD_LORAWAN_NEW_INT_U(s.rx1_arm_to_setrx_us_max));
    mp_obj_dict_store(hr, MP_ROM_QSTR(MP_QSTR_count),
                      MOD_LORAWAN_NEW_INT_U(s.rx1_arm_to_setrx_us_count));
    mp_obj_dict_store(hr, MP_ROM_QSTR(MP_QSTR_first_sample_boot_ms),
                      MOD_LORAWAN_NEW_INT_U(
                          s.rx1_arm_to_setrx_us_first_sample_boot_ms));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_rx1_arm_to_setrx_us), hr);

    /* p7-instrument-003 — per-reason pump_dispatch_latency split. Four
       sub-dicts keyed by reason string; each sub-dict has the same 6
       histogram fields as the aggregate above. Used to decide whether a
       latency outlier is boot-bound or driven by one specific reason
       (DIO1 = radio IRQ path; TIMER = MAC timer path; NOTIFY = MAC
       process notification; PY = Python mac.process trampoline). */
    {
        mp_obj_t by_reason = MOD_LORAWAN_NEW_DICT(4);
        static const struct { qstr key; uint8_t idx; } reason_map[4] = {
            { MP_QSTR_NOTIFY, LORAWAN_PUMP_REASON_NOTIFY },
            { MP_QSTR_TIMER,  LORAWAN_PUMP_REASON_TIMER  },
            { MP_QSTR_DIO1,   LORAWAN_PUMP_REASON_DIO1   },
            { MP_QSTR_PY,     LORAWAN_PUMP_REASON_PY     },
        };
        for (uint32_t k = 0; k < 4u; k++) {
            uint8_t i = reason_map[k].idx;
            mp_obj_t hb = MOD_LORAWAN_NEW_DICT(6);
            mp_obj_dict_store(hb, MP_ROM_QSTR(MP_QSTR_p50),
                MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_by_reason_p50[i]));
            mp_obj_dict_store(hb, MP_ROM_QSTR(MP_QSTR_p95),
                MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_by_reason_p95[i]));
            mp_obj_dict_store(hb, MP_ROM_QSTR(MP_QSTR_p99),
                MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_by_reason_p99[i]));
            mp_obj_dict_store(hb, MP_ROM_QSTR(MP_QSTR_max),
                MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_by_reason_max[i]));
            mp_obj_dict_store(hb, MP_ROM_QSTR(MP_QSTR_count),
                MOD_LORAWAN_NEW_INT_U(s.pump_dispatch_latency_by_reason_count[i]));
            mp_obj_dict_store(hb, MP_ROM_QSTR(MP_QSTR_first_sample_boot_ms),
                MOD_LORAWAN_NEW_INT_U(
                    s.pump_dispatch_latency_by_reason_first_sample_boot_ms[i]));
            mp_obj_dict_store(by_reason, MP_ROM_QSTR(reason_map[k].key), hb);
        }
        mp_obj_dict_store(dict,
            MP_ROM_QSTR(MP_QSTR_pump_dispatch_latency_us_by_reason), by_reason);
    }

    /* Phase 7 cleanup — radio-touched-path latency long-pole probes.
       Each is a single uint32 (worst-case µs), not a histogram, because
       the binary question is: does the dispatch wrapper or the SPI BUSY-
       wait loop ever exceed the ~100 ms outlier seen in TR-2? Updated
       by lorawan_pump_observe_*_us() called from sx126x_board.c. */
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_dio1_dispatch_max_us),
                      MOD_LORAWAN_NEW_INT_U(s.dio1_dispatch_max_us));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_spi_busy_wait_max_us),
                      MOD_LORAWAN_NEW_INT_U(s.spi_busy_wait_max_us));

    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_mcps_indication_queued_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.mcps_indication_queued_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_mcps_indication_dropped_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.mcps_indication_dropped_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_mcps_indication_queue_high_water),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.mcps_indication_queue_high_water));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_event_cb_drain_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.event_cb_drain_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_event_cb_drain_reentry_skip_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.event_cb_drain_reentry_skip_count));

    /* Phase 6 instrumentation (p6-instrument-001) — per-reason pump_request
       counter (6 ints: idx 0 unused .. INTERNAL=5) for H4 verification, and
       TimerInit ordinal log (8 ints: callback pointers in source order) for
       H1 verification against objdump'd OnRx / OnAck / OnTxDelayed symbols. */
    {
        mp_obj_t reason_items[6];
        for (uint32_t i = 0; i < 6u; i++) {
            reason_items[i] =
                MOD_LORAWAN_NEW_INT_U(s.pump_request_by_reason[i]);
        }
        mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_pump_request_by_reason),
                          mp_obj_new_tuple(6, reason_items));

        uintptr_t ord_buf[8] = {0};
        (void)lorawan_timer_init_log_get(ord_buf, 8);
        mp_obj_t ord_items[8];
        for (uint32_t i = 0; i < 8u; i++) {
            ord_items[i] = MOD_LORAWAN_NEW_INT_U((mp_uint_t)ord_buf[i]);
        }
        mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_timer_init_ordinals),
                          mp_obj_new_tuple(8, ord_items));

        /* Phase 6 P3 (p6-instrument-002) — TimerStart-side and dispatch-side
           callback-pointer ring logs (8 slots each, physical order). QA
           compares each slot against the OnRxWindow1TimerEvent runtime
           address (objdump 0x40988 + 1 Thumb bit = 0x40989) to decide H5
           vs H6: presence in timerstart_log but absence in dispatch_log
           means RX1 was armed but never fired; absence in both means MAC
           never reached the RX1 TimerStart at all (LoRaMacProcess starvation). */
        uint32_t ts_buf[8] = {0};
        uint32_t disp_buf[8] = {0};
        lorawan_timer_logs_get(ts_buf, disp_buf);
        mp_obj_t ts_items[8];
        mp_obj_t disp_items[8];
        for (uint32_t i = 0; i < 8u; i++) {
            ts_items[i] = mp_obj_new_int_from_uint(ts_buf[i]);
            disp_items[i] = mp_obj_new_int_from_uint(disp_buf[i]);
        }
        mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_timerstart_log),
                          mp_obj_new_tuple(8, ts_items));
        mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_timer_dispatch_log),
                          mp_obj_new_tuple(8, disp_items));
    }

    /* Phase 7 — DIO1 hard-ISR -> pump-body race-detection pair. ISR seq
       is bumped on every DIO1 fire in sx126x_board.c; pump_seen_seq is
       snapshotted in lorawan_pump.c when the radio-IRQ pending flag is
       consumed. QA computes (dio1_isr_seq - dio1_pump_seen_seq) to
       detect deferred drains. */
    {
        uint32_t isr_seq = 0u;
        lorawan_dio1_state_get(NULL, &isr_seq);
        mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_dio1_isr_seq),
                          MOD_LORAWAN_NEW_INT_U(isr_seq));
        mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_dio1_pump_seen_seq),
                          MOD_LORAWAN_NEW_INT_U(
                              lorawan_dio1_pump_seen_seq_get()));
    }

    /* Phase 8 — anti-fake-pass + SetRx cache observation. The first key
       MUST read 0 across any TR-1 / TR-3 run: Phase 8 deletes the SPI-
       side s_rx_window_active=1u write, and any re-introduction routed
       through lorawan_pump_observe_rx_window_active_set_via_spi() will
       surface here. setrx_cmd_cache_count rises in lockstep with
       rx1_arm_to_setrx_us.count (both pulse on opcode 0x82 in the SPI
       path); setrx_cmd_cache_ready is the last-known buffer-valid flag. */
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_rx_window_active_set_via_spi_count),
                      MOD_LORAWAN_NEW_INT_U(s.rx_window_active_set_via_spi_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_setrx_cmd_cache_count),
                      MOD_LORAWAN_NEW_INT_U(s.setrx_cmd_cache_count));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_setrx_cmd_cache_ready),
                      MOD_LORAWAN_NEW_INT_U(s.setrx_cmd_cache_ready));

    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_pump_diag_obj, lorawan_mac_pump_diag);

static mp_obj_t lorawan_mac_set_min_rx_symbols(mp_obj_t self_in,
    mp_obj_t n_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    mp_int_t n = mp_obj_get_int(n_in);
    if (n < 6 || n > 100) {
        mp_raise_ValueError(MP_ERROR_TEXT("min_rx_symbols 6..100"));
    }
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_MIN_RX_SYMBOLS;
    mib.Param.MinRxSymbols = (uint16_t)n;
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_MIN_RX_SYMBOLS set failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_min_rx_symbols_obj,
    lorawan_mac_set_min_rx_symbols);

/* P3.0 — MIB_PUBLIC_NETWORK setter. ChirpStack and TTN gateways use the
   public LoRaWAN sync word (0x34); private networks use 0x12. Without
   PublicNetwork=true the chip transmits with the wrong sync word and the
   gateway never demodulates the join-request. LoRaMacInitialization()
   defaults to PublicNetwork=true (LoRaMac.c:4239), so this setter is for
   completeness / override; calling with True is a safe no-op. */
static mp_obj_t lorawan_mac_set_public_network(mp_obj_t self_in,
    mp_obj_t enable_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_PUBLIC_NETWORK;
    mib.Param.EnablePublicNetwork = mp_obj_is_true(enable_in);
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_PUBLIC_NETWORK set failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_public_network_obj,
    lorawan_mac_set_public_network);

/* P3.2 — apply the 6 MIB defaults that the Renesas reference sample
   (renesas_lora/.../LoRaSample/lorawan_proc.c:116-156) writes right after
   LoRaMacInitialization. Optional helper: each setting is also exposed
   as an individual setter so Python tests can override per-test.

   Defaults mirror Renesas + EU868 + ChirpStack public network:
     MIB_PUBLIC_NETWORK     = true       (LoRaWAN sync word 0x34)
     MIB_ADR                = true       (let server adjust DR)
     MIB_DEVICE_CLASS       = CLASS_A    (lowest power; uplink + RX1/RX2)
     MIB_SYSTEM_MAX_RX_ERROR = 10        (ms, per APP_SYSTEM_MAX_RX_ERROR)
     MIB_CHANNELS_DATARATE  = DR_0       (SF12BW125 — slowest, most robust)
     MIB_DUTY_CYCLE         = 1          (ETSI EU868 compliance)

   Returns None on success; raises OSError with the first failing MIB
   type/status code so the user sees exactly which write fell over. */
static mp_obj_t lorawan_mac_init_defaults(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    MibRequestConfirm_t mib;
    LoRaMacStatus_t st;

    #define _SET_MIB_OR_RAISE(_type, _field, _value) do { \
        memset(&mib, 0, sizeof(mib)); \
        mib.Type = (_type); \
        mib.Param._field = (_value); \
        st = LoRaMacMibSetRequestConfirm(&mib); \
        if (st != LORAMAC_STATUS_OK) { \
            mp_raise_msg_varg(&mp_type_OSError, \
                MP_ERROR_TEXT("MIB %d set failed: %d"), \
                (int)(_type), (int)st); \
        } \
    } while (0)

    _SET_MIB_OR_RAISE(MIB_PUBLIC_NETWORK,      EnablePublicNetwork, true);
    _SET_MIB_OR_RAISE(MIB_ADR,                 AdrEnable,           true);
    _SET_MIB_OR_RAISE(MIB_DEVICE_CLASS,        Class,               CLASS_A);
    _SET_MIB_OR_RAISE(MIB_SYSTEM_MAX_RX_ERROR, SystemMaxRxError,    10);
    _SET_MIB_OR_RAISE(MIB_CHANNELS_DATARATE,   ChannelsDatarate,    DR_0);
    _SET_MIB_OR_RAISE(MIB_DUTY_CYCLE,          DCycleEnabled,       1);

    #undef _SET_MIB_OR_RAISE
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_init_defaults_obj,
    lorawan_mac_init_defaults);

/* Phase 6a debug — explicit save/restore + size queries so Python
   tests can observe whether contexts are being persisted. */
static mp_obj_t lorawan_mac_nvm_store(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(NvmDataMgmtStore());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_nvm_store_obj,
    lorawan_mac_nvm_store);

static mp_obj_t lorawan_mac_nvm_restore(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(NvmDataMgmtRestore());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_nvm_restore_obj,
    lorawan_mac_nvm_restore);

static mp_obj_t lorawan_mac_nvm_factory_reset(mp_obj_t self_in) {
    (void)self_in;
    __atomic_store_n(&lorawan_rxc_diag.last_rx_rssi_dbm,    0, __ATOMIC_RELAXED);
    __atomic_store_n(&lorawan_rxc_diag.last_rx_snr_db,      0, __ATOMIC_RELAXED);
    __atomic_store_n(&lorawan_rxc_diag.last_rx_stats_valid, 0, __ATOMIC_RELAXED);
    return MOD_LORAWAN_NEW_BOOL(NvmDataMgmtFactoryReset());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_nvm_factory_reset_obj,
    lorawan_mac_nvm_factory_reset);

/* Phase 6a — read DevEUI/JoinEUI/AppKey from Data Flash Block 0 in
   the legacy "LWCR" format written by provision_credentials.py. On
   success returns a (deveui, joineui, appkey) tuple of bytes objects.
   On blank / invalid record returns None — caller falls back to .py
   constants (matching lorawan_app.py:_load_credentials behaviour). */
static mp_obj_t lorawan_mac_load_credentials(mp_obj_t self_in) {
    (void)self_in;
    uint8_t deveui[8], joineui[8], appkey[16];
    if (!dflash_load_credentials(deveui, joineui, appkey)) {
        return mp_const_none;
    }
    mp_obj_t items[3] = {
        MOD_LORAWAN_NEW_BYTES(deveui,  8),
        MOD_LORAWAN_NEW_BYTES(joineui, 8),
        MOD_LORAWAN_NEW_BYTES(appkey,  16),
    };
    return MOD_LORAWAN_NEW_TUPLE(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_load_credentials_obj,
    lorawan_mac_load_credentials);

static mp_obj_t lorawan_mac_nvm_diag(mp_obj_t self_in) {
    (void)self_in;
    /* Pull current contexts directly so Python can see per-module
       sizes (mac, region, crypto, se, cmds, classb, cq) and figure
       out which one is overflowing our static buffers. */
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NVM_CTXS;
    LoRaMacStatus_t st = LoRaMacMibGetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK || mib.Param.Contexts == NULL) {
        return MOD_LORAWAN_NEW_TUPLE(0, NULL);
    }
    LoRaMacCtxs_t *c = mib.Param.Contexts;
    mp_obj_t items[7] = {
        MP_OBJ_NEW_SMALL_INT((mp_int_t)c->MacNvmCtxSize),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)c->RegionNvmCtxSize),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)c->CryptoNvmCtxSize),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)c->SecureElementNvmCtxSize),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)c->CommandsNvmCtxSize),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)c->ClassBNvmCtxSize),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)c->ConfirmQueueNvmCtxSize),
    };
    return MOD_LORAWAN_NEW_TUPLE(7, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_nvm_diag_obj,
    lorawan_mac_nvm_diag);

#else  /* !LORAWAN_PHASE5_AVAILABLE — stubs. */

static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_join_obj, 1,
    lorawan_mac_phase5_unavailable);
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_is_joined_obj, 1,
    lorawan_mac_phase5_unavailable);

#endif  /* LORAWAN_PHASE5_AVAILABLE */

#if LORAWAN_PHASE5_AVAILABLE

/* Phase 5c — uplink + downlink.
 *
 * `mac.send(port, data, confirmed=False, datarate=5)` builds a McpsReq
 * and submits it to the MAC. Returns immediately with the LoRaMac
 * status code (0 = accepted, others = rejected, e.g. busy / duty-cycle).
 * Actual transmission completes asynchronously — the user's
 * event_callback fires on `mcps_confirm`.
 *
 * `mac.recv()` pops the most recent downlink (if any) into a
 * (port, payload_bytes) tuple. Non-blocking: returns None if no
 * downlink is queued. The MCPS indication callback writes into the
 * singleton's rx slot.
 */

/* TX buffer copied from Python — must outlive the LoRaMacMcpsRequest
   call (the stack reads from it during frame construction). 256 bytes
   covers the LoRaWAN max app payload at any DR. */
static uint8_t s_tx_buf[256];

static mp_obj_t lorawan_mac_send(size_t n_args, const mp_obj_t *args) {
    SBC(LWBC_S0);
    enum { ARG_self, ARG_port, ARG_data, ARG_confirmed, ARG_datarate };
    if (n_args < 3 || n_args > 5) {
        mp_raise_TypeError(MP_ERROR_TEXT(
            "send(port, data[, confirmed[, datarate]])"));
    }
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(args[ARG_self]);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }

    mp_int_t port = mp_obj_get_int(args[ARG_port]);
    if (port < 1 || port > 223) {
        mp_raise_ValueError(MP_ERROR_TEXT("port 1..223"));
    }
    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[ARG_data], &buf, MP_BUFFER_READ);
    if (buf.len > sizeof(s_tx_buf)) {
        mp_raise_ValueError(MP_ERROR_TEXT("data > 256 bytes"));
    }
    bool confirmed = (n_args >= 4) && mp_obj_is_true(args[ARG_confirmed]);
    int8_t datarate = 5;
    if (n_args == 5) {
        mp_int_t dr = mp_obj_get_int(args[ARG_datarate]);
        if (dr < 0 || dr > 5) {
            mp_raise_ValueError(MP_ERROR_TEXT("EU868 datarate 0..5"));
        }
        datarate = (int8_t)dr;
    }
    SBC(LWBC_S1);

    memcpy(s_tx_buf, buf.buf, buf.len);
    SBC(LWBC_S2);

    McpsReq_t req;
    memset(&req, 0, sizeof(req));
    if (confirmed) {
        req.Type = MCPS_CONFIRMED;
        req.Req.Confirmed.fPort = (uint8_t)port;
        req.Req.Confirmed.fBuffer = s_tx_buf;
        req.Req.Confirmed.fBufferSize = (uint16_t)buf.len;
        req.Req.Confirmed.NbTrials = 1;
        req.Req.Confirmed.Datarate = datarate;
    } else {
        req.Type = MCPS_UNCONFIRMED;
        req.Req.Unconfirmed.fPort = (uint8_t)port;
        req.Req.Unconfirmed.fBuffer = s_tx_buf;
        req.Req.Unconfirmed.fBufferSize = (uint16_t)buf.len;
        req.Req.Unconfirmed.Datarate = datarate;
    }
    SBC(LWBC_S3);
    SBC(LWBC_S4);
    LoRaMacStatus_t st = LoRaMacMcpsRequest(&req);
    SBC(LWBC_S5);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_send_obj, 3, lorawan_mac_send);

static mp_obj_t lorawan_mac_recv(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->rx_pending) {
        return mp_const_none;
    }
    /* Snapshot under PRIMASK to avoid race with mac_mcps_indication
       running from scheduler context. */
    mp_uint_t state = disable_irq();
    uint8_t  port = self->rx_port;
    uint16_t len  = self->rx_len;
    uint8_t  tmp[sizeof(self->rx_buf)];
    if (len > sizeof(tmp)) {
        len = sizeof(tmp);
    }
    memcpy(tmp, self->rx_buf, len);
    self->rx_pending = false;
    self->rx_port = 0;
    self->rx_len = 0;
    enable_irq(state);

    mp_obj_t items[2] = {
        MP_OBJ_NEW_SMALL_INT(port),
        MOD_LORAWAN_NEW_BYTES(tmp, len),
    };
    return MOD_LORAWAN_NEW_TUPLE(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_recv_obj, lorawan_mac_recv);

#else  /* !LORAWAN_PHASE5_AVAILABLE */

static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_send_obj, 1,
    lorawan_mac_phase5_unavailable);
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_recv_obj, 1,
    lorawan_mac_phase5_unavailable);

#endif  /* LORAWAN_PHASE5_AVAILABLE */

/* Phase 1 Step 12 — Python-facing observation surface.
 *
 * mac.stats() snapshots g_lorawan_stats into a freshly allocated
 * dict-of-dicts (5 groups, 32 leaves). Called from Python/scheduler
 * context only — heap allocation is safe here. No state mutation.
 *
 * spi_bytes_total uses the 64-bit seqlock reader from lorawan_stats.h
 * (see phase1_step3_atomicity.md §3) so the high half cannot tear vs.
 * a concurrent SPI write incrementing the low half through overflow.
 *
 * Counters are uint32_t — use mp_obj_new_int_from_uint to avoid
 * truncating values >2^30 that MP_OBJ_NEW_SMALL_INT cannot hold.
 * Opcodes (uint8_t 0..255) fit in a small int.
 */
static mp_obj_t lorawan_mac_stats(mp_obj_t self_in) {
    (void)self_in;

    mp_obj_t mac_group = MOD_LORAWAN_NEW_DICT(3);
    mp_obj_dict_store(mac_group, MP_ROM_QSTR(MP_QSTR_mac_process_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.mac_process_count));
    mp_obj_dict_store(mac_group, MP_ROM_QSTR(MP_QSTR_mac_process_last_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.mac_process_last_us));
    mp_obj_dict_store(mac_group, MP_ROM_QSTR(MP_QSTR_mac_process_max_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.mac_process_max_us));

    mp_obj_t spi_group = MOD_LORAWAN_NEW_DICT(9);
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_spi_xfer_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.spi_xfer_count));
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_spi_bytes_total),
                      MOD_LORAWAN_NEW_INT_ULL(stats_spi_bytes_read()));
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_spi_max_len),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.spi_max_len));
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_spi_one_byte_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.spi_one_byte_count));
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_spi_nested_reject_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.spi_nested_reject_count));
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_spi_stage_pre_busy_max_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.spi_stage_pre_busy_max_us));
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_spi_stage_dtc_max_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.spi_stage_dtc_max_us));
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_spi_stage_post_busy_max_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.spi_stage_post_busy_max_us));
    mp_obj_dict_store(spi_group, MP_ROM_QSTR(MP_QSTR_sx126x_wake_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.sx126x_wake_count));

    mp_obj_t busy_group = MOD_LORAWAN_NEW_DICT(6);
    mp_obj_dict_store(busy_group, MP_ROM_QSTR(MP_QSTR_busy_wait_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.busy_wait_count));
    mp_obj_dict_store(busy_group, MP_ROM_QSTR(MP_QSTR_busy_wait_last_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.busy_wait_last_us));
    mp_obj_dict_store(busy_group, MP_ROM_QSTR(MP_QSTR_busy_wait_max_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.busy_wait_max_us));
    mp_obj_dict_store(busy_group, MP_ROM_QSTR(MP_QSTR_busy_timeout_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.busy_timeout_count));
    mp_obj_dict_store(busy_group, MP_ROM_QSTR(MP_QSTR_busy_timeout_opcode),
                      MP_OBJ_NEW_SMALL_INT(g_lorawan_stats.busy_timeout_opcode));
    mp_obj_dict_store(busy_group, MP_ROM_QSTR(MP_QSTR_busy_last_opcode),
                      MP_OBJ_NEW_SMALL_INT(g_lorawan_stats.busy_last_opcode));

    /* AD5.2: isr group expanded from 12 → 16 leaves (4 new hard_isr_* counters
       for the DTC done-callback + BUSY-falling ICU augmentation). All 4 read 0
       in AD5.2 — the callback is defined but not yet hooked into the LoRa SPI
       path (that ships in AD5.3 / AD5.4). */
    mp_obj_t isr_group = MOD_LORAWAN_NEW_DICT(16);
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_dio1_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_dio1_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_agt4_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_agt4_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_agt5_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_agt5_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_queue_push_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_queue_push_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_queue_overflow_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_queue_overflow_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_dtc_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_dtc_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_dtc_cycles_max),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_dtc_cycles_max));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_busy_low_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_busy_low_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_busy_low_cycles_max),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_busy_low_cycles_max));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_dio1_cycles_max),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_dio1_cycles_max));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_agt4_cycles_max),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_agt4_cycles_max));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_agt5_cycles_max),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_agt5_cycles_max));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_dio1_reentry_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_dio1_reentry_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_agt4_reentry_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_agt4_reentry_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_hard_isr_agt5_reentry_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.hard_isr_agt5_reentry_count));
    mp_obj_dict_store(isr_group, MP_ROM_QSTR(MP_QSTR_dwt_available),
                      MOD_LORAWAN_NEW_BOOL(s_dwt_available));

    mp_obj_t nvm_group = MOD_LORAWAN_NEW_DICT(7);
    mp_obj_dict_store(nvm_group, MP_ROM_QSTR(MP_QSTR_nvm_save_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.nvm_save_count));
    mp_obj_dict_store(nvm_group, MP_ROM_QSTR(MP_QSTR_nvm_save_last_ms),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.nvm_save_last_ms));
    mp_obj_dict_store(nvm_group, MP_ROM_QSTR(MP_QSTR_nvm_save_max_ms),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.nvm_save_max_ms));
    mp_obj_dict_store(nvm_group, MP_ROM_QSTR(MP_QSTR_nvm_save_error_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.nvm_save_error_count));
    mp_obj_dict_store(nvm_group, MP_ROM_QSTR(MP_QSTR_nvm_save_call_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.nvm_save_call_us));
    mp_obj_dict_store(nvm_group, MP_ROM_QSTR(MP_QSTR_nvm_save_done_us),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.nvm_save_done_us));
    mp_obj_dict_store(nvm_group, MP_ROM_QSTR(MP_QSTR_nvm_save_in_rx_window_count),
                      MOD_LORAWAN_NEW_INT_U(g_lorawan_stats.nvm_save_in_rx_window_count));

    /* Z0 — heap allocation surveillance. See top-of-file Z0 block and
       ZERO_ALLOC_REMOVAL_PLAN_2026-05-13.md §Z0 for the bar.
       Under LORAWAN_OBSERVATION_DISABLE the leaves all read 0 (s_heap
       storage is absent) — preserve the same shape so test code does
       not branch on build mode. */
    mp_obj_t heap_group = MOD_LORAWAN_NEW_DICT(7);
    #ifdef LORAWAN_OBSERVATION_DISABLE
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_c_alloc_count),
                      MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_c_free_count),
                      MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_mp_alloc_count_post_init),
                      MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_mp_free_count_post_init),
                      MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_isr_alloc_count),
                      MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_init_baseline_count),
                      MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_event_drop_count),
                      MP_OBJ_NEW_SMALL_INT(0));
    #else
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_c_alloc_count),
                      MOD_LORAWAN_NEW_INT_U(s_heap.c_alloc_count));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_c_free_count),
                      MOD_LORAWAN_NEW_INT_U(s_heap.c_free_count));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_mp_alloc_count_post_init),
                      MOD_LORAWAN_NEW_INT_U(s_heap.mp_alloc_count_post_init));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_mp_free_count_post_init),
                      MOD_LORAWAN_NEW_INT_U(s_heap.mp_free_count_post_init));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_isr_alloc_count),
                      MOD_LORAWAN_NEW_INT_U(s_heap.isr_alloc_count));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_init_baseline_count),
                      MOD_LORAWAN_NEW_INT_U(s_heap.init_baseline_count));
    mp_obj_dict_store(heap_group, MP_ROM_QSTR(MP_QSTR_event_drop_count),
                      MOD_LORAWAN_NEW_INT_U(s_event_drop_count));
    #endif

    /* T-V3.1 — Class C RX probe leaves. Unconditional storage, see
     * glue/lorawan_rxc_diag.h. Read atomically via __atomic_load_n so
     * a concurrent ISR increment cannot tear a uint32. */
    mp_obj_t rxc_group = MOD_LORAWAN_NEW_DICT(14);
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_rxc_open_attempts),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint16_t *)&lorawan_rxc_diag.rxc_open_attempts,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_rxc_open_skipped_rf_rx),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint16_t *)&lorawan_rxc_diag.rxc_open_skipped_rf_rx,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_rxc_open_skipped_rf_tx),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint16_t *)&lorawan_rxc_diag.rxc_open_skipped_rf_tx,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_rxc_region_ok),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint16_t *)&lorawan_rxc_diag.rxc_region_ok,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_rxc_region_fail),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint16_t *)&lorawan_rxc_diag.rxc_region_fail,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_rxc_radio_rx_result),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint16_t *)&lorawan_rxc_diag.rxc_radio_rx_result,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_last_rxc_freq),
                      MOD_LORAWAN_NEW_INT_U(__atomic_load_n(
                          (uint32_t *)&lorawan_rxc_diag.last_rxc_freq,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_last_rxc_dr),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint8_t *)&lorawan_rxc_diag.last_rxc_dr,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_last_rxc_continuous),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint8_t *)&lorawan_rxc_diag.last_rxc_continuous,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_last_radio_rx_timeout_arg_caller),
                      MOD_LORAWAN_NEW_INT_U(__atomic_load_n(
                          (uint32_t *)&lorawan_rxc_diag.last_radio_rx_timeout_arg_caller,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_last_radio_rx_timeout_arg),
                      MOD_LORAWAN_NEW_INT_U(__atomic_load_n(
                          (uint32_t *)&lorawan_rxc_diag.last_radio_rx_timeout_arg,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_last_rx_done_slot),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint8_t *)&lorawan_rxc_diag.last_rx_done_slot,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_last_rx_timeout_slot),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint8_t *)&lorawan_rxc_diag.last_rx_timeout_slot,
                          __ATOMIC_RELAXED)));
    mp_obj_dict_store(rxc_group, MP_ROM_QSTR(MP_QSTR_last_rx_error_slot),
                      MP_OBJ_NEW_SMALL_INT(__atomic_load_n(
                          (uint8_t *)&lorawan_rxc_diag.last_rx_error_slot,
                          __ATOMIC_RELAXED)));

    mp_obj_t top = MOD_LORAWAN_NEW_DICT(7);
    mp_obj_dict_store(top, MP_ROM_QSTR(MP_QSTR_mac),  mac_group);
    mp_obj_dict_store(top, MP_ROM_QSTR(MP_QSTR_spi),  spi_group);
    mp_obj_dict_store(top, MP_ROM_QSTR(MP_QSTR_busy), busy_group);
    mp_obj_dict_store(top, MP_ROM_QSTR(MP_QSTR_isr),  isr_group);
    mp_obj_dict_store(top, MP_ROM_QSTR(MP_QSTR_nvm),  nvm_group);
    mp_obj_dict_store(top, MP_ROM_QSTR(MP_QSTR_heap), heap_group);
    mp_obj_dict_store(top, MP_ROM_QSTR(MP_QSTR_rxc),  rxc_group);
    return top;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_stats_obj, lorawan_mac_stats);

static mp_obj_t lorawan_mac_stats_reset(mp_obj_t self_in) {
    (void)self_in;
    lorawan_stats_reset();
    /* Z0 — also zero the heap-counter leaves, preserving observation_armed
       (post-init observation continues across stats_reset, the standard
       QA-MEM measurement-window pattern: arm → reset → start window).
       Z2 — likewise reset the event drop counter; there is no longer
       a ring head index (packed-int dispatch is stateless on the C
       side, see mac_post_event). */
    #ifndef LORAWAN_OBSERVATION_DISABLE
    uint8_t armed = __atomic_load_n((uint8_t *)&s_heap.observation_armed,
                                    __ATOMIC_RELAXED);
    memset((void *)&s_heap, 0, sizeof(s_heap));
    __atomic_store_n((uint8_t *)&s_heap.observation_armed, armed,
                     __ATOMIC_RELAXED);
    s_event_drop_count = 0;
    #endif
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_stats_reset_obj,
    lorawan_mac_stats_reset);

/* === Z3 — *_into accessors ============================================
 *
 * Zero-allocation companions to mac.stats(), mac.rx_diag(), mac.nvm_diag()
 * and mac.recv(). Each variant takes caller-pre-allocated storage and
 * mutates it in place, eliminating the per-call heap activity on the
 * diagnostic / downlink hot paths. The classic methods stay around for
 * backward-compat and ad-hoc use.
 *
 * Plan reference:
 *   LORAWAN_TESTS/coordination/ZERO_ALLOC_REMOVAL_PLAN_2026-05-13.md §Z3
 *   LORAWAN_TESTS/coordination/MEM_AUDIT_DESIGN_2026-05-13.md §2.6-2.8
 *
 * Caller contract — universal:
 *   - Pre-allocate the dict / tuple / bytearray ONCE at init.
 *   - Pre-populate every expected key with a placeholder value
 *     (e.g. 0). The *_into() impl does NOT add keys — it uses
 *     mp_map_lookup(MP_MAP_LOOKUP) and silently skips any missing key,
 *     which prevents the rehash-and-allocate path inside mp_obj_dict_store.
 *   - Counter values are written as SMALL_INT. Counters above
 *     MP_SMALL_INT_MAX (2^30-1 on 32-bit MicroPython, 2^62-1 on 64-bit)
 *     are TRUNCATED to that ceiling via z3_set_uint_leaf below; this
 *     keeps the path allocation-free at the cost of saturating the
 *     reported value. Audited list of "wide" counters today: all stay
 *     well under 2^30 in canonical workloads (longest soak is 17 h ×
 *     5 s = 12 240 uplinks; spi_bytes_total at ~32 B/xfer ≈ 4 × 10^5
 *     B/uplink → ~4.9 × 10^9 over 17 h; that DOES exceed 2^30. Caller
 *     wishing precise byte counts should keep using mac.stats() (which
 *     allocates a 64-bit boxed int) on a slow cadence; the *_into()
 *     hot-path readers are for delta-based observation where truncation
 *     does not matter.). Document by listing the contract in the
 *     method-level comment for the operator-facing API.
 */

#ifndef LORAWAN_OBSERVATION_DISABLE

/* Write `val` into dict slot for `key` if and only if the key already
 * exists. No allocation: only an existing map elem's value pointer is
 * mutated. Returns true iff the leaf existed (caller can panic on a
 * mis-shaped pre-allocated dict during dev-time, but production
 * intentionally skips). Caller passes the dict's map (mp_obj_dict_get_map)
 * so we do not re-resolve it for every leaf in a 30-leaf write loop. */
static inline bool z3_dict_set_in_place(mp_map_t *map, qstr key_qstr,
    mp_obj_t value) {
    mp_map_elem_t *elem = mp_map_lookup(map, MP_OBJ_NEW_QSTR(key_qstr),
        MP_MAP_LOOKUP);
    if (elem == NULL) {
        return false;
    }
    elem->value = value;
    return true;
}

/* Saturate-cast uint32 to SMALL_INT positive range, write into dict
 * slot in place. The SMALL_INT_MAX ceiling is 2^30-1 on 32-bit MP. See
 * z3 caller-contract block above for the truncation policy. */
static inline void z3_set_uint_leaf(mp_map_t *map, qstr key_qstr,
    uint32_t v) {
    mp_int_t sval = (v > (uint32_t)MP_SMALL_INT_MAX)
                    ? MP_SMALL_INT_MAX : (mp_int_t)v;
    (void)z3_dict_set_in_place(map, key_qstr, MP_OBJ_NEW_SMALL_INT(sval));
}

/* uint64 -> SMALL_INT saturating. spi_bytes_total is the only uint64
 * counter today; documented as saturating per the §Z3 contract. */
static inline void z3_set_uint64_leaf(mp_map_t *map, qstr key_qstr,
    uint64_t v) {
    mp_int_t sval = (v > (uint64_t)MP_SMALL_INT_MAX)
                    ? MP_SMALL_INT_MAX : (mp_int_t)v;
    (void)z3_dict_set_in_place(map, key_qstr, MP_OBJ_NEW_SMALL_INT(sval));
}

static inline void z3_set_bool_leaf(mp_map_t *map, qstr key_qstr, bool v) {
    (void)z3_dict_set_in_place(map, key_qstr,
        v ? mp_const_true : mp_const_false);
}

#endif  /* LORAWAN_OBSERVATION_DISABLE */

/* mac.stats_into(prealloc) — Z3 zero-alloc companion to mac.stats().
 *
 * Caller pre-allocates a nested dict with the same six-group shape and
 * every leaf key populated (any int placeholder, e.g. 0). The impl
 * fetches each group dict via mp_obj_dict_get (raises KeyError if a
 * group is missing — pre-allocation contract violation) and then mutates
 * each leaf in place. Wide counters are saturated to SMALL_INT range
 * (see truncation note in the §Z3 contract block).
 *
 * Returns mp_const_none — the prealloc dict is mutated, not replaced.
 */
static mp_obj_t lorawan_mac_stats_into(mp_obj_t self_in, mp_obj_t prealloc) {
    (void)self_in;
    #ifdef LORAWAN_OBSERVATION_DISABLE
    (void)prealloc;
    return mp_const_none;
    #else
    if (!mp_obj_is_dict_or_ordereddict(prealloc)) {
        mp_raise_TypeError(MP_ERROR_TEXT("stats_into requires a dict"));
    }

    mp_obj_t mac_group  = mp_obj_dict_get(prealloc, MP_OBJ_NEW_QSTR(MP_QSTR_mac));
    mp_obj_t spi_group  = mp_obj_dict_get(prealloc, MP_OBJ_NEW_QSTR(MP_QSTR_spi));
    mp_obj_t busy_group = mp_obj_dict_get(prealloc, MP_OBJ_NEW_QSTR(MP_QSTR_busy));
    mp_obj_t isr_group  = mp_obj_dict_get(prealloc, MP_OBJ_NEW_QSTR(MP_QSTR_isr));
    mp_obj_t nvm_group  = mp_obj_dict_get(prealloc, MP_OBJ_NEW_QSTR(MP_QSTR_nvm));
    mp_obj_t heap_group = mp_obj_dict_get(prealloc, MP_OBJ_NEW_QSTR(MP_QSTR_heap));

    mp_map_t *mac_map  = mp_obj_dict_get_map(mac_group);
    mp_map_t *spi_map  = mp_obj_dict_get_map(spi_group);
    mp_map_t *busy_map = mp_obj_dict_get_map(busy_group);
    mp_map_t *isr_map  = mp_obj_dict_get_map(isr_group);
    mp_map_t *nvm_map  = mp_obj_dict_get_map(nvm_group);
    mp_map_t *heap_map = mp_obj_dict_get_map(heap_group);

    z3_set_uint_leaf(mac_map, MP_QSTR_mac_process_count,   g_lorawan_stats.mac_process_count);
    z3_set_uint_leaf(mac_map, MP_QSTR_mac_process_last_us, g_lorawan_stats.mac_process_last_us);
    z3_set_uint_leaf(mac_map, MP_QSTR_mac_process_max_us,  g_lorawan_stats.mac_process_max_us);

    z3_set_uint_leaf(spi_map,   MP_QSTR_spi_xfer_count,                g_lorawan_stats.spi_xfer_count);
    z3_set_uint64_leaf(spi_map, MP_QSTR_spi_bytes_total,               stats_spi_bytes_read());
    z3_set_uint_leaf(spi_map,   MP_QSTR_spi_max_len,                   g_lorawan_stats.spi_max_len);
    z3_set_uint_leaf(spi_map,   MP_QSTR_spi_one_byte_count,            g_lorawan_stats.spi_one_byte_count);
    z3_set_uint_leaf(spi_map,   MP_QSTR_spi_nested_reject_count,       g_lorawan_stats.spi_nested_reject_count);
    z3_set_uint_leaf(spi_map,   MP_QSTR_spi_stage_pre_busy_max_us,     g_lorawan_stats.spi_stage_pre_busy_max_us);
    z3_set_uint_leaf(spi_map,   MP_QSTR_spi_stage_dtc_max_us,          g_lorawan_stats.spi_stage_dtc_max_us);
    z3_set_uint_leaf(spi_map,   MP_QSTR_spi_stage_post_busy_max_us,    g_lorawan_stats.spi_stage_post_busy_max_us);
    z3_set_uint_leaf(spi_map,   MP_QSTR_sx126x_wake_count,             g_lorawan_stats.sx126x_wake_count);

    z3_set_uint_leaf(busy_map, MP_QSTR_busy_wait_count,     g_lorawan_stats.busy_wait_count);
    z3_set_uint_leaf(busy_map, MP_QSTR_busy_wait_last_us,   g_lorawan_stats.busy_wait_last_us);
    z3_set_uint_leaf(busy_map, MP_QSTR_busy_wait_max_us,    g_lorawan_stats.busy_wait_max_us);
    z3_set_uint_leaf(busy_map, MP_QSTR_busy_timeout_count,  g_lorawan_stats.busy_timeout_count);
    z3_set_uint_leaf(busy_map, MP_QSTR_busy_timeout_opcode, g_lorawan_stats.busy_timeout_opcode);
    z3_set_uint_leaf(busy_map, MP_QSTR_busy_last_opcode,    g_lorawan_stats.busy_last_opcode);

    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_dio1_count,             g_lorawan_stats.hard_isr_dio1_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_agt4_count,             g_lorawan_stats.hard_isr_agt4_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_agt5_count,             g_lorawan_stats.hard_isr_agt5_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_queue_push_count,       g_lorawan_stats.hard_isr_queue_push_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_queue_overflow_count,   g_lorawan_stats.hard_isr_queue_overflow_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_dtc_count,              g_lorawan_stats.hard_isr_dtc_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_dtc_cycles_max,         g_lorawan_stats.hard_isr_dtc_cycles_max);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_busy_low_count,         g_lorawan_stats.hard_isr_busy_low_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_busy_low_cycles_max,    g_lorawan_stats.hard_isr_busy_low_cycles_max);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_dio1_cycles_max,        g_lorawan_stats.hard_isr_dio1_cycles_max);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_agt4_cycles_max,        g_lorawan_stats.hard_isr_agt4_cycles_max);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_agt5_cycles_max,        g_lorawan_stats.hard_isr_agt5_cycles_max);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_dio1_reentry_count,     g_lorawan_stats.hard_isr_dio1_reentry_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_agt4_reentry_count,     g_lorawan_stats.hard_isr_agt4_reentry_count);
    z3_set_uint_leaf(isr_map, MP_QSTR_hard_isr_agt5_reentry_count,     g_lorawan_stats.hard_isr_agt5_reentry_count);
    z3_set_bool_leaf(isr_map, MP_QSTR_dwt_available, s_dwt_available);

    z3_set_uint_leaf(nvm_map, MP_QSTR_nvm_save_count,                 g_lorawan_stats.nvm_save_count);
    z3_set_uint_leaf(nvm_map, MP_QSTR_nvm_save_last_ms,               g_lorawan_stats.nvm_save_last_ms);
    z3_set_uint_leaf(nvm_map, MP_QSTR_nvm_save_max_ms,                g_lorawan_stats.nvm_save_max_ms);
    z3_set_uint_leaf(nvm_map, MP_QSTR_nvm_save_error_count,           g_lorawan_stats.nvm_save_error_count);
    z3_set_uint_leaf(nvm_map, MP_QSTR_nvm_save_call_us,               g_lorawan_stats.nvm_save_call_us);
    z3_set_uint_leaf(nvm_map, MP_QSTR_nvm_save_done_us,               g_lorawan_stats.nvm_save_done_us);
    z3_set_uint_leaf(nvm_map, MP_QSTR_nvm_save_in_rx_window_count,    g_lorawan_stats.nvm_save_in_rx_window_count);

    z3_set_uint_leaf(heap_map, MP_QSTR_c_alloc_count,             s_heap.c_alloc_count);
    z3_set_uint_leaf(heap_map, MP_QSTR_c_free_count,              s_heap.c_free_count);
    z3_set_uint_leaf(heap_map, MP_QSTR_mp_alloc_count_post_init,  s_heap.mp_alloc_count_post_init);
    z3_set_uint_leaf(heap_map, MP_QSTR_mp_free_count_post_init,   s_heap.mp_free_count_post_init);
    z3_set_uint_leaf(heap_map, MP_QSTR_isr_alloc_count,           s_heap.isr_alloc_count);
    z3_set_uint_leaf(heap_map, MP_QSTR_init_baseline_count,       s_heap.init_baseline_count);
    z3_set_uint_leaf(heap_map, MP_QSTR_event_drop_count,          s_event_drop_count);

    return mp_const_none;
    #endif
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_stats_into_obj,
    lorawan_mac_stats_into);

#if LORAWAN_PHASE5_AVAILABLE

/* mac.rx_diag_into(prealloc) — Z3 zero-alloc companion to mac.rx_diag().
 * Same dict-mutate contract as stats_into. Returns mp_const_none. */
static mp_obj_t lorawan_mac_rx_diag_into(mp_obj_t self_in, mp_obj_t prealloc) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init first"));
    }
    if (!mp_obj_is_dict_or_ordereddict(prealloc)) {
        mp_raise_TypeError(MP_ERROR_TEXT("rx_diag_into requires a dict"));
    }
    mp_map_t *m = mp_obj_dict_get_map(prealloc);
    MibRequestConfirm_t mib;

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_PUBLIC_NETWORK;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        (void)z3_dict_set_in_place(m, MP_QSTR_public_network,
            mib.Param.EnablePublicNetwork ? mp_const_true : mp_const_false);
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_RX2_CHANNEL;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        (void)z3_dict_set_in_place(m, MP_QSTR_rx2_freq,
            MP_OBJ_NEW_SMALL_INT((mp_int_t)(mib.Param.Rx2Channel.Frequency
                & (uint32_t)MP_SMALL_INT_MAX)));
        (void)z3_dict_set_in_place(m, MP_QSTR_rx2_dr,
            MP_OBJ_NEW_SMALL_INT(mib.Param.Rx2Channel.Datarate));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_RX2_DEFAULT_CHANNEL;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        (void)z3_dict_set_in_place(m, MP_QSTR_rx2_default_freq,
            MP_OBJ_NEW_SMALL_INT((mp_int_t)(mib.Param.Rx2DefaultChannel.Frequency
                & (uint32_t)MP_SMALL_INT_MAX)));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_SYSTEM_MAX_RX_ERROR;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        (void)z3_dict_set_in_place(m, MP_QSTR_max_rx_error_ms,
            MP_OBJ_NEW_SMALL_INT((mp_int_t)mib.Param.SystemMaxRxError));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_MIN_RX_SYMBOLS;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        (void)z3_dict_set_in_place(m, MP_QSTR_min_rx_symbols,
            MP_OBJ_NEW_SMALL_INT(mib.Param.MinRxSymbols));
    }

    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NETWORK_ACTIVATION;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        (void)z3_dict_set_in_place(m, MP_QSTR_activation,
            MP_OBJ_NEW_SMALL_INT(mib.Param.NetworkActivation));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_rx_diag_into_obj,
    lorawan_mac_rx_diag_into);

/* mac.nvm_diag_into(prealloc_tuple) — Z3 zero-alloc companion.
 * Caller pre-allocates a 7-element tuple; the seven NVM context sizes
 * are written into items[0..6] in the same order as mac.nvm_diag().
 * Returns mp_const_none (or raises if the MIB read fails / tuple is
 * not 7-wide). */
static mp_obj_t lorawan_mac_nvm_diag_into(mp_obj_t self_in,
    mp_obj_t prealloc_tuple) {
    (void)self_in;
    if (!mp_obj_is_type(prealloc_tuple, &mp_type_tuple)) {
        mp_raise_TypeError(MP_ERROR_TEXT("nvm_diag_into requires a tuple"));
    }
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(prealloc_tuple);
    if (t->len != 7) {
        mp_raise_ValueError(MP_ERROR_TEXT("tuple must be length 7"));
    }
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NVM_CTXS;
    LoRaMacStatus_t st = LoRaMacMibGetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK || mib.Param.Contexts == NULL) {
        for (int i = 0; i < 7; i++) {
            t->items[i] = MP_OBJ_NEW_SMALL_INT(0);
        }
        return mp_const_none;
    }
    LoRaMacCtxs_t *c = mib.Param.Contexts;
    t->items[0] = MP_OBJ_NEW_SMALL_INT((mp_int_t)c->MacNvmCtxSize);
    t->items[1] = MP_OBJ_NEW_SMALL_INT((mp_int_t)c->RegionNvmCtxSize);
    t->items[2] = MP_OBJ_NEW_SMALL_INT((mp_int_t)c->CryptoNvmCtxSize);
    t->items[3] = MP_OBJ_NEW_SMALL_INT((mp_int_t)c->SecureElementNvmCtxSize);
    t->items[4] = MP_OBJ_NEW_SMALL_INT((mp_int_t)c->CommandsNvmCtxSize);
    t->items[5] = MP_OBJ_NEW_SMALL_INT((mp_int_t)c->ClassBNvmCtxSize);
    t->items[6] = MP_OBJ_NEW_SMALL_INT((mp_int_t)c->ConfirmQueueNvmCtxSize);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_nvm_diag_into_obj,
    lorawan_mac_nvm_diag_into);

/* mac.recv_into(buf, info) — Z3 zero-alloc companion to mac.recv().
 *
 * Drains a pending downlink into the caller's buffers:
 *   buf  : a writable buffer-protocol object (typically bytearray(256))
 *          large enough for the payload. The FRMPayload bytes are
 *          memcpy'd into buf[0..n) and any trailing bytes are left
 *          untouched.
 *   info : a writable buffer-protocol object exposing at least
 *          2 × sizeof(int32_t) = 8 bytes. Typically
 *              array.array('i', [0, 0])
 *          The impl writes:
 *              info[0] = fport (1..223)
 *              info[1] = byte length copied into buf
 *          using little-endian int32 stores (matches array.array('i')
 *          native layout on this port).
 *
 * Return:
 *   - byte length on a drained downlink (0..256)
 *   - 0 (and info[0]=info[1]=0) when no downlink is pending — caller
 *     polls again next pump tick. Distinguishable from a 0-byte downlink
 *     by reading info[0]: a real downlink always has fport >= 1.
 *
 * No allocation in either path. info_array doubles as the GC root for
 * the metadata since it is a long-lived Python local in the test loop.
 * Why this two-buffer shape? Returning (fport, length) as a 2-tuple
 * would allocate; passing a side-channel array keeps the call signature
 * compatible with `mac.recv()`'s "0 = nothing pending" semantic without
 * any heap activity. See §Z3.4 in the plan for the deliberated trade-off.
 */
static mp_obj_t lorawan_mac_recv_into(mp_obj_t self_in,
    mp_obj_t buf_obj, mp_obj_t info_obj) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_buffer_info_t info_buf;
    mp_get_buffer_raise(info_obj, &info_buf, MP_BUFFER_RW);
    if (info_buf.len < 2 * (int)sizeof(int32_t)) {
        mp_raise_ValueError(MP_ERROR_TEXT("info must hold 2 int32 slots"));
    }
    int32_t *info_words = (int32_t *)info_buf.buf;

    if (!self->rx_pending) {
        info_words[0] = 0;
        info_words[1] = 0;
        return MP_OBJ_NEW_SMALL_INT(0);
    }

    mp_buffer_info_t data_buf;
    mp_get_buffer_raise(buf_obj, &data_buf, MP_BUFFER_RW);

    /* Snapshot under PRIMASK so a concurrent mac_mcps_indication does
       not tear (port,len,buf,pending). Mirrors lorawan_mac_recv above. */
    mp_uint_t state = disable_irq();
    uint8_t  port = self->rx_port;
    uint16_t len  = self->rx_len;
    if ((size_t)len > (size_t)data_buf.len) {
        len = (uint16_t)data_buf.len;
    }
    memcpy(data_buf.buf, self->rx_buf, len);
    self->rx_pending = false;
    self->rx_port = 0;
    self->rx_len = 0;
    enable_irq(state);

    info_words[0] = (int32_t)port;
    info_words[1] = (int32_t)len;
    return MP_OBJ_NEW_SMALL_INT(len);
}
static MP_DEFINE_CONST_FUN_OBJ_3(lorawan_mac_recv_into_obj,
    lorawan_mac_recv_into);

#else  /* !LORAWAN_PHASE5_AVAILABLE — Z3 stubs */

static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_rx_diag_into_obj, 2,
    lorawan_mac_phase5_unavailable);
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_nvm_diag_into_obj, 2,
    lorawan_mac_phase5_unavailable);
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_recv_into_obj, 1,
    lorawan_mac_phase5_unavailable);

#endif  /* LORAWAN_PHASE5_AVAILABLE */

// ---- Class table ---------------------------------------------------------

static const mp_rom_map_elem_t lorawan_mac_locals_dict_table[] = {
    // Phase 1 — radio HAL.
    { MP_ROM_QSTR(MP_QSTR_radio_reset),        MP_ROM_PTR(&lorawan_mac_radio_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_radio_get_status),   MP_ROM_PTR(&lorawan_mac_radio_get_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_radio_read_reg),     MP_ROM_PTR(&lorawan_mac_radio_read_reg_obj) },
    { MP_ROM_QSTR(MP_QSTR_radio_write_reg),    MP_ROM_PTR(&lorawan_mac_radio_write_reg_obj) },
    { MP_ROM_QSTR(MP_QSTR_radio_busy),         MP_ROM_PTR(&lorawan_mac_radio_busy_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_dio1_callback),  MP_ROM_PTR(&lorawan_mac_set_dio1_callback_obj) },
    // Phase 2 — direct ICU + AGT4 timer service + runtime SPI tuning.
    { MP_ROM_QSTR(MP_QSTR_enable_busy_irq),    MP_ROM_PTR(&lorawan_mac_enable_busy_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable_busy_irq),   MP_ROM_PTR(&lorawan_mac_disable_busy_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_spi_interbyte_us), MP_ROM_PTR(&lorawan_mac_set_spi_interbyte_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_spi_interbyte_us), MP_ROM_PTR(&lorawan_mac_get_spi_interbyte_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_dbg_spi_id),         MP_ROM_PTR(&lorawan_mac_dbg_spi_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_dbg_xchg),           MP_ROM_PTR(&lorawan_mac_dbg_xchg_obj) },
    #if LORAWAN_PHASE5_AVAILABLE
    /* Scan helpers — raw chip access through Renesas Radio API. */
    { MP_ROM_QSTR(MP_QSTR_scan_standby),       MP_ROM_PTR(&lorawan_mac_scan_standby_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan_set_freq),      MP_ROM_PTR(&lorawan_mac_scan_set_freq_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan_set_lora_rx),   MP_ROM_PTR(&lorawan_mac_scan_set_lora_rx_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan_rx_continuous), MP_ROM_PTR(&lorawan_mac_scan_rx_continuous_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan_set_rx_raw),    MP_ROM_PTR(&lorawan_mac_scan_set_rx_raw_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan_rssi),          MP_ROM_PTR(&lorawan_mac_scan_rssi_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan_get_errors),    MP_ROM_PTR(&lorawan_mac_scan_get_errors_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable_dio1_irq),   MP_ROM_PTR(&lorawan_mac_disable_dio1_irq_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_now_ms),             MP_ROM_PTR(&lorawan_mac_now_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_arm_oneshot),        MP_ROM_PTR(&lorawan_mac_arm_oneshot_obj) },
    { MP_ROM_QSTR(MP_QSTR_arm_oneshot_us),     MP_ROM_PTR(&lorawan_mac_arm_oneshot_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),             MP_ROM_PTR(&lorawan_mac_deinit_obj) },
    // Phase 5a — LoRaMac initialization, key configuration, process tick.
    { MP_ROM_QSTR(MP_QSTR_lorawan_init),       MP_ROM_PTR(&lorawan_mac_lorawan_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_keys),           MP_ROM_PTR(&lorawan_mac_set_keys_obj) },
    { MP_ROM_QSTR(MP_QSTR_process),            MP_ROM_PTR(&lorawan_mac_process_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_event_callback), MP_ROM_PTR(&lorawan_mac_set_event_callback_obj) },
    // Phase 5b — OTAA join.
    { MP_ROM_QSTR(MP_QSTR_join),               MP_ROM_PTR(&lorawan_mac_join_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_joined),          MP_ROM_PTR(&lorawan_mac_is_joined_obj) },
    { MP_ROM_QSTR(MP_QSTR_link_check),         MP_ROM_PTR(&lorawan_mac_link_check_obj) },
    { MP_ROM_QSTR(MP_QSTR_last_link_check),    MP_ROM_PTR(&lorawan_mac_last_link_check_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_datarate),       MP_ROM_PTR(&lorawan_mac_get_datarate_obj) },
    { MP_ROM_QSTR(MP_QSTR_device_time_req),    MP_ROM_PTR(&lorawan_mac_device_time_req_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_sys_time),       MP_ROM_PTR(&lorawan_mac_get_sys_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_class),          MP_ROM_PTR(&lorawan_mac_set_class_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_class),          MP_ROM_PTR(&lorawan_mac_get_class_obj) },
    { MP_ROM_QSTR(MP_QSTR_beacon_acquisition), MP_ROM_PTR(&lorawan_mac_beacon_acquisition_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_ping_slot_periodicity),
                                               MP_ROM_PTR(&lorawan_mac_set_ping_slot_periodicity_obj) },
    // Phase 6b — Adaptive Data Rate.
    { MP_ROM_QSTR(MP_QSTR_set_adr),            MP_ROM_PTR(&lorawan_mac_set_adr_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_adr),            MP_ROM_PTR(&lorawan_mac_get_adr_obj) },
    // Phase 7 — RX window timing tunables.
    { MP_ROM_QSTR(MP_QSTR_set_max_rx_error),   MP_ROM_PTR(&lorawan_mac_set_max_rx_error_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_min_rx_symbols), MP_ROM_PTR(&lorawan_mac_set_min_rx_symbols_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_public_network), MP_ROM_PTR(&lorawan_mac_set_public_network_obj) },
    { MP_ROM_QSTR(MP_QSTR_init_defaults),      MP_ROM_PTR(&lorawan_mac_init_defaults_obj) },
    { MP_ROM_QSTR(MP_QSTR_rx_diag),            MP_ROM_PTR(&lorawan_mac_rx_diag_obj) },
    { MP_ROM_QSTR(MP_QSTR_last_rx_stats),      MP_ROM_PTR(&lorawan_mac_last_rx_stats_obj) },
    { MP_ROM_QSTR(MP_QSTR_rx_window_diag),     MP_ROM_PTR(&lorawan_mac_rx_window_diag_obj) },
    // Phase 4 — guarded C pump counter surface (operator decisions 5/9/10).
    { MP_ROM_QSTR(MP_QSTR_pump_diag),          MP_ROM_PTR(&lorawan_mac_pump_diag_obj) },
    // Phase 6a — NVM debug surface.
    { MP_ROM_QSTR(MP_QSTR_nvm_store),          MP_ROM_PTR(&lorawan_mac_nvm_store_obj) },
    { MP_ROM_QSTR(MP_QSTR_nvm_restore),        MP_ROM_PTR(&lorawan_mac_nvm_restore_obj) },
    { MP_ROM_QSTR(MP_QSTR_nvm_factory_reset),  MP_ROM_PTR(&lorawan_mac_nvm_factory_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_nvm_diag),           MP_ROM_PTR(&lorawan_mac_nvm_diag_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_credentials),   MP_ROM_PTR(&lorawan_mac_load_credentials_obj) },
    // Phase 5c — uplink + downlink.
    { MP_ROM_QSTR(MP_QSTR_send),               MP_ROM_PTR(&lorawan_mac_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv),               MP_ROM_PTR(&lorawan_mac_recv_obj) },
    // Phase 1 Observation — Python-facing counter snapshot + reset.
    { MP_ROM_QSTR(MP_QSTR_stats),              MP_ROM_PTR(&lorawan_mac_stats_obj) },
    { MP_ROM_QSTR(MP_QSTR_stats_reset),        MP_ROM_PTR(&lorawan_mac_stats_reset_obj) },
    // Z3 — zero-allocation *_into() variants of diagnostic/recv surfaces.
    { MP_ROM_QSTR(MP_QSTR_stats_into),         MP_ROM_PTR(&lorawan_mac_stats_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_rx_diag_into),       MP_ROM_PTR(&lorawan_mac_rx_diag_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_nvm_diag_into),      MP_ROM_PTR(&lorawan_mac_nvm_diag_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv_into),          MP_ROM_PTR(&lorawan_mac_recv_into_obj) },
};
static MP_DEFINE_CONST_DICT(lorawan_mac_locals_dict,
    lorawan_mac_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    lorawan_mac_type,
    MP_QSTR_Mac,
    MP_TYPE_FLAG_NONE,
    make_new, lorawan_mac_make_new,
    print, lorawan_mac_print,
    locals_dict, &lorawan_mac_locals_dict
);

// ---- Module-level globals -----------------------------------------------

/* HardFault breadcrumb readout. Returns (magic_ok, last_id). magic_ok
 * is True iff the .noinit magic word survived as expected (i.e. some
 * Mac() in this device's recent past wrote it). last_id is the raw
 * uint16_t enum value of the most recent SBC() — interpretation is
 * left to the Python side (LWBC_NONE=0 means "no breadcrumb yet"). */
static mp_obj_t lorawan_last_breadcrumb(void) {
    mp_obj_t items[2] = {
        mp_obj_new_bool(lorawan_bc_magic == LORAWAN_BC_MAGIC),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)lorawan_bc_last),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(lorawan_last_breadcrumb_obj,
    lorawan_last_breadcrumb);

static const mp_rom_map_elem_t lorawan_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),         MP_ROM_QSTR(MP_QSTR_lorawan) },
    { MP_ROM_QSTR(MP_QSTR_Mac),              MP_ROM_PTR(&lorawan_mac_type) },
    { MP_ROM_QSTR(MP_QSTR__PHASE),           MP_ROM_INT(LORAWAN_PHASE) },
    { MP_ROM_QSTR(MP_QSTR_EU868),            MP_ROM_QSTR(MP_QSTR_EU868) },
    { MP_ROM_QSTR(MP_QSTR__last_breadcrumb), MP_ROM_PTR(&lorawan_last_breadcrumb_obj) },
};
static MP_DEFINE_CONST_DICT(lorawan_module_globals,
    lorawan_module_globals_table);

const mp_obj_module_t mp_module_lorawan = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lorawan_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_lorawan, mp_module_lorawan);

/* GC roots for the three Mac callback slots — see HARDFAULT_ZALLOC_2026-05-13.md.
   Indices: 0=dio1, 1=event, 2=test_timer. Routed via MP_STATE_VM so the
   collector can see closures held only by the singleton (which lives in .bss
   and is otherwise invisible to gc_collect_root). */
MP_REGISTER_ROOT_POINTER(mp_obj_t lorawan_mac_root_callbacks[3]);  /* literal must match LORAWAN_CB_COUNT above */

#endif // MICROPY_HW_LORA_STACK_RENESAS
