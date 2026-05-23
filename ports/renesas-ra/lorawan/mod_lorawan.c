/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
 *
 * Renesas LoRaWAN C-stack — MicroPython binding.
 *
 * The binding exposes the LoRaMac stack to MicroPython while keeping the
 * RA4M2 board contract explicit: Python owns SPI and Pin objects; AGT4/AGT5
 * are boot-reserved (MICROPY_HW_AGT_RESERVED_MASK) and opened directly by
 * the vendor C board layer via timer_board_init(). Python does not pass
 * Timer objects to the constructor. The C board layer performs SX126x
 * reset, BUSY polling, DIO1 dispatch, and SPI transport through
 * Python-owned SPI/Pin resources.
 *
 * Pin defaults come from the bench-tested Python driver
 *   ports/renesas-ra/boards/VK_RA4M2/examples/LoRa/lorawan_upstream/
 *      lorawan_app.py:1056-1059
 * and are kept as compile-time defaults so
 *   >>> import lorawan
 *   >>> mac = lorawan.Mac()
 *   >>> mac.lorawan_init()
 * does the right thing on VK_RA4M2 + Wio-SX1262. AGT4/AGT5 are
 * boot-reserved via MICROPY_HW_AGT_RESERVED_MASK (mpconfigboard.h)
 * so no Python Timer objects are needed at the constructor.
 */

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/smallint.h"
#include "extmod/modmachine.h"

#include "mod_lorawan.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

#include "pin.h"
#include "extint.h"
#include "sx126x-board.h"
#include "nvm_board.h"
#include "dflash_lwnvm.h"
#include "lorawan_stats.h"
#include "lorawan_pump.h"
#include "board.h"

#include "boards/vk_ra4m2_sx126x/sx126x_board_cfg.h"
void SX126xIoIrqDeinit(void);
bool sx126x_board_spi_busy(void);

void timer_board_init(void);
void timer_board_deinit(void);

/* Radio.SetPublicNetwork — defined in lorawan/radio/sx126x/radio.c. LoRaMac.c does
 * NOT propagate MIB_PUBLIC_NETWORK to the radio layer (vendor comments
 * "// not call Radio.SetPublicNetwork() here" at LoRaMac.c:4301 + 5231), so
 * the binding must call it explicitly to apply the LoRaWAN public syncword
 * (0x34/0x44) to the SX126x — otherwise the chip listens with private
 * syncword (0x14/0x24) and never demodulates JoinAccept. */
void RadioSetPublicNetwork(bool enable);

#include "LoRaMac.h"

// ---- Pin defaults (mpconfigboard.h-compatible literals) -----------------
//
// Resolved at Mac.__init__ via machine_pin_find() so the Python user can
// override any of them. Defaults match the proven Python LoRaWAN app.

#define LORAWAN_DEFAULT_SPI_ID      (3)
#define LORAWAN_DEFAULT_SPI_BAUD    (8000000)
#define LORAWAN_DEFAULT_CLK_QSTR    MP_QSTR_P111
#define LORAWAN_DEFAULT_MOSI_QSTR   MP_QSTR_P109
#define LORAWAN_DEFAULT_MISO_QSTR   MP_QSTR_P110
#define LORAWAN_DEFAULT_CS_QSTR     MP_QSTR_P206
#define LORAWAN_DEFAULT_RST_QSTR    MP_QSTR_P001
#define LORAWAN_DEFAULT_GPIO_BUSY_QSTR MP_QSTR_P002
#define LORAWAN_DEFAULT_IRQ_QSTR    MP_QSTR_P015
#define LORAWAN_DEFAULT_RF_SW_QSTR  MP_QSTR_P100

// ---- Mac object ----------------------------------------------------------

typedef struct _lorawan_mac_obj_t {
    mp_obj_base_t base;
    qstr region;
    bool radio_initialized;
    bool timer_initialized;
    /* Binding projection only. The fields below describe what this
       MicroPython object has done or observed; they are not a second LoRaWAN
       protocol FSM. The protocol authority remains MacCtx.MacState inside
       mac/LoRaMac.c. */
    bool stack_initialized;        // LoRaMacInitialization succeeded
    bool keys_set;                  // DEVEUI/JOINEUI/APPKEY loaded
    bool joined;                    // OTAA join completed
    bool join_confirm_seen;         // MLME_JOIN confirm observed by binding
    int join_confirm_status;        // LoRaMacEventInfoStatus_t value

    // RX queue. Indications arrive asynchronously from the
    // MCPS layer; we copy them here for Python `mac.recv()` polling.
    // Single-slot for now — LoRaMac never delivers two indications at
    // once on a single end-device, so a full ring is overkill.
    bool     rx_pending;
    uint8_t  rx_port;
    uint16_t rx_len;
    uint8_t  rx_buf[256];
    /* Callback lives in MP_STATE_VM(lorawan_mac_root_callbacks)[0] so the
       GC sees it during root scan. The singleton is in .bss, which
       gc_collect_root() does not walk. */

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

    /* AGT4/AGT5 are boot-reserved via MICROPY_HW_AGT_RESERVED_MASK in
       mpconfigboard.h. The C board layer (vendor timer-board.c) opens
       g_timer0_ctrl / g_timer1_ctrl directly through R_AGT_Open, so no
       Python Timer objects participate in the LoRaWAN path. */
} lorawan_mac_obj_t;

static lorawan_mac_obj_t lorawan_mac_singleton;

/* GC root slots for Python callbacks owned by the binding. Routed via
   MP_STATE_VM so gc_collect_root() can see them; the singleton itself lives
   in .bss and would otherwise hide closure cells from the collector. */
enum {
    LORAWAN_CB_EVENT = 0,
    LORAWAN_CB_COUNT = 1,  /* KEEP IN SYNC with literal "1" in MP_REGISTER_ROOT_POINTER below; the qstr/root-pointer collector parses this file before enums are visible. */
};
#define LORAWAN_CB_SLOT(i)  (MP_STATE_VM(lorawan_mac_root_callbacks)[(i)])

/* Single owner of the internal counter storage. 0xFF means "no BUSY opcode
 * observed yet". */
volatile lorawan_stats_t g_lorawan_stats = {
    .busy_last_opcode = 0xFFu,
};

/* Idempotent DWT CYCCNT enable for the ISR cycle-max metrics. DWT may
 * already be enabled by mphalport.c:141 (mp_hal_ticks_cpu_enable) on
 * other code paths; the guarded write makes this safe to call from any
 * LoRa init entry. If the bit cannot be set, CYCCNT stays 0 and the
 * cycle-max metrics remain unavailable. DWT/CoreDebug macros come via
 * py/mphal.h -> mphalport.h -> RA_HAL_H CMSIS chain.
 *
 * After enabling we run a short probe (a few __NOP() + __DSB()) and
 * compare CYCCNT before/after. s_dwt_available reflects whether CYCCNT
 * actually advanced. false means cycle-max readings are unavailable. */
volatile bool s_dwt_available = false;

/* Foreground service coordinator.
 *
 * Keep it with the binding because it is not a board driver and not a second
 * LoRaWAN state machine: it only latches service requests from
 * DIO1/timer/notify/Python and runs upstream LoRaMacProcess() from foreground
 * mac.process().
 */
static volatile uint8_t s_process_running;
static volatile uint8_t s_process_pending;
static volatile uint8_t s_pump_scheduled;
static volatile uint8_t s_pump_initialized;
static volatile uint32_t s_event_drop_count;

#define LORAWAN_JOIN_DEFAULT_TIMEOUT_MS (15000u)

static void lorawan_pump_init(void) {
    if (s_pump_initialized) {
        return;
    }
    s_process_running = 0;
    s_process_pending = 0;
    s_pump_scheduled = 0;
    s_event_drop_count = 0;
    s_pump_initialized = 1;
}

static void lorawan_pump_deinit(void) {
    s_pump_initialized = 0;
    s_process_running = 0;
    s_process_pending = 0;
    s_pump_scheduled = 0;
    s_event_drop_count = 0;
}

static void lorawan_driver_request_pump(void) {
    if (!s_pump_initialized) {
        return;
    }
    mp_uint_t irq_state = disable_irq();

    if (s_pump_scheduled || s_process_running) {
        s_process_pending = 1u;
        enable_irq(irq_state);
        return;
    }
    s_pump_scheduled = 1u;
    s_process_pending = 1u;
    enable_irq(irq_state);
}

static void lorawan_driver_pump_run(void) {
    for (;;) {
        mp_uint_t irq_state = disable_irq();
        if (s_pump_scheduled == 0u && s_process_pending == 0u) {
            enable_irq(irq_state);
            return;
        }
        if (s_process_running) {
            s_process_pending = 1u;
            enable_irq(irq_state);
            return;
        }

        s_process_pending = 0u;
        s_process_running = 1u;
        s_pump_scheduled = 1u;
        enable_irq(irq_state);

        if (sx126x_board_spi_busy()) {
            irq_state = disable_irq();
            s_process_running = 0u;
            s_pump_scheduled = 1u;
            s_process_pending = 1u;
            enable_irq(irq_state);
            return;
        }

        #ifndef LORAWAN_OBSERVATION_DISABLE
        uint32_t process_t0 = mp_hal_ticks_us();
        #endif
        LoRaMacProcess();
        #ifndef LORAWAN_OBSERVATION_DISABLE
        uint32_t process_dt = mp_hal_ticks_us() - process_t0;
        STATS_INC(mac_process_count);
        STATS_STORE(mac_process_last_us, process_dt);
        STATS_UPDATE_MAX(mac_process_max_us, process_dt);
        #endif

        irq_state = disable_irq();
        s_process_running = 0u;
        if (s_process_pending == 0u) {
            s_pump_scheduled = 0u;
            enable_irq(irq_state);
            return;
        }
        enable_irq(irq_state);
    }
}

void lorawan_pump_request_dio1(void) {
    lorawan_driver_request_pump();
}

/* Packed-SMALL_INT event dispatch.
 * Events are represented as one tagged immediate value so callback delivery
 * does not allocate on the LoRaMac foreground path.
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
 */

#ifndef LORAWAN_OBSERVATION_DISABLE
void lorawan_stats_dwt_init(void) {
    #if __CORTEX_M >= 4 && __CORTEX_M != 23
    /* Robust enable + read-back verify. Some reset/debug paths leave TRCENA
       low on the first write, so accept the first observed CYCCNT advance. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();
    __ISB();
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    bool ok = false;
    for (int attempt = 0; attempt < 8 && !ok; attempt++) {
        /* If TRCENA latch propagation lags, tight-looping may read the same
           dead value; give each probe a fresh barrier and a few NOPs. */
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

static const machine_pin_obj_t *resolve_pin(mp_obj_t user_obj, qstr default_qstr) {
    if (user_obj == mp_const_none) {
        return machine_pin_find(MP_OBJ_NEW_QSTR(default_qstr));
    }
    return machine_pin_find(user_obj);
}

static inline mp_obj_t resolve_obj(mp_obj_t user_obj, qstr default_qstr) {
    return user_obj == mp_const_none ? MP_OBJ_NEW_QSTR(default_qstr) : user_obj;
}

static mp_obj_t lorawan_create_spi_obj(mp_int_t spi_bus, mp_int_t baudrate,
                                       mp_obj_t clk_obj, mp_obj_t mosi_obj,
                                       mp_obj_t miso_obj) {
    mp_obj_t spi_args[] = {
        MP_OBJ_NEW_SMALL_INT(spi_bus),
        MP_OBJ_NEW_QSTR(MP_QSTR_baudrate), MP_OBJ_NEW_SMALL_INT(baudrate),
        MP_OBJ_NEW_QSTR(MP_QSTR_polarity), MP_OBJ_NEW_SMALL_INT(0),
        MP_OBJ_NEW_QSTR(MP_QSTR_phase),    MP_OBJ_NEW_SMALL_INT(0),
        MP_OBJ_NEW_QSTR(MP_QSTR_sck),      clk_obj,
        MP_OBJ_NEW_QSTR(MP_QSTR_mosi),     mosi_obj,
        MP_OBJ_NEW_QSTR(MP_QSTR_miso),     miso_obj,
    };
    return MP_OBJ_TYPE_GET_SLOT(&machine_spi_type, make_new)(
        &machine_spi_type, 1, 6, spi_args);
}

/* status() is a binding snapshot, not a second LoRaMac state machine.
   The real protocol FSM lives in mac/LoRaMac.c (MacCtx.MacState). */
static mp_obj_t lorawan_mac_status(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t d = mp_obj_new_dict(10);
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_region),
        MP_OBJ_NEW_QSTR(self->region));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_radio_initialized),
        mp_obj_new_bool(self->radio_initialized));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_timer_initialized),
        mp_obj_new_bool(self->timer_initialized));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_stack_initialized),
        mp_obj_new_bool(self->stack_initialized));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_keys_set),
        mp_obj_new_bool(self->keys_set));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_joined),
        mp_obj_new_bool(self->joined));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_rx_pending),
        mp_obj_new_bool(self->rx_pending));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_rx_len),
        MP_OBJ_NEW_SMALL_INT(self->rx_len));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_mac_busy),
        mp_obj_new_bool(LoRaMacIsBusy()));
    mp_obj_dict_store(d, MP_ROM_QSTR(MP_QSTR_event_drop_count),
        mp_obj_new_int_from_uint(s_event_drop_count));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_status_obj, lorawan_mac_status);

static mp_obj_t lorawan_mac_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_region, ARG_spi_bus, ARG_baudrate,
           ARG_clk, ARG_mosi, ARG_miso,
           ARG_cs, ARG_irq, ARG_rst, ARG_gpio_busy, ARG_rf_sw,
           ARG_spi };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_region,   MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(MP_QSTR_EU868) } },
        { MP_QSTR_spi_bus,  MP_ARG_KW_ONLY | MP_ARG_INT,
            { .u_int = LORAWAN_DEFAULT_SPI_ID } },
        { MP_QSTR_baudrate, MP_ARG_KW_ONLY | MP_ARG_INT,
            { .u_int = LORAWAN_DEFAULT_SPI_BAUD } },
        { MP_QSTR_clk,      MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(LORAWAN_DEFAULT_CLK_QSTR) } },
        { MP_QSTR_mosi,     MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(LORAWAN_DEFAULT_MOSI_QSTR) } },
        { MP_QSTR_miso,     MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(LORAWAN_DEFAULT_MISO_QSTR) } },
        { MP_QSTR_cs,       MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(LORAWAN_DEFAULT_CS_QSTR) } },
        { MP_QSTR_irq,      MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(LORAWAN_DEFAULT_IRQ_QSTR) } },
        { MP_QSTR_rst,      MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(LORAWAN_DEFAULT_RST_QSTR) } },
        { MP_QSTR_gpio_busy, MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(LORAWAN_DEFAULT_GPIO_BUSY_QSTR) } },
        { MP_QSTR_rf_sw,    MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_QSTR(LORAWAN_DEFAULT_RF_SW_QSTR) } },
        /* Optional shortcut for callers that already constructed
           machine.SPI(3, ...). If omitted, this constructor creates the
           MicroPython SPI object from spi_bus/clk/mosi/miso. */
        { MP_QSTR_spi,      MP_ARG_KW_ONLY | MP_ARG_OBJ,
            { .u_rom_obj = MP_ROM_NONE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    qstr region_qstr = mp_obj_str_get_qstr(args[ARG_region].u_obj);
    if (region_qstr != MP_QSTR_EU868) {
        mp_raise_ValueError(MP_ERROR_TEXT("only EU868 supported"));
    }

    lorawan_mac_obj_t *self = &lorawan_mac_singleton;
    /* P1 — re-entry guard. The singleton is global; a second lorawan.Mac()
     * call without an intervening deinit() would silently overwrite live
     * state and leak the open radio/timer/stack. Fail fast so caller must
     * deinit() before re-constructing. */
    if (self->stack_initialized || self->radio_initialized ||
        self->timer_initialized) {
        mp_raise_msg(&mp_type_OSError,
            MP_ERROR_TEXT("LoRaWAN already constructed; call deinit() first"));
    }
    self->base.type = type;
    self->region = region_qstr;
    self->radio_initialized = false;
    self->timer_initialized = false;
    self->stack_initialized = false;
    self->keys_set = false;
    self->joined = false;
    self->join_confirm_seen = false;
    self->join_confirm_status = (int)LORAMAC_EVENT_INFO_STATUS_ERROR;
    self->rx_pending = false;
    self->rx_port = 0;
    self->rx_len = 0;
    LORAWAN_CB_SLOT(LORAWAN_CB_EVENT) = mp_const_none;
    memset(self->deveui, 0, sizeof(self->deveui));
    memset(self->joineui, 0, sizeof(self->joineui));
    memset(self->appkey, 0, sizeof(self->appkey));

    // Resolve pins now so a wrong pin name fails fast at construction.
    const machine_pin_obj_t *cs_pin =
        resolve_pin(args[ARG_cs].u_obj,    LORAWAN_DEFAULT_CS_QSTR);
    const machine_pin_obj_t *rst_pin =
        resolve_pin(args[ARG_rst].u_obj,   LORAWAN_DEFAULT_RST_QSTR);
    const machine_pin_obj_t *gpio_busy_pin =
        resolve_pin(args[ARG_gpio_busy].u_obj, LORAWAN_DEFAULT_GPIO_BUSY_QSTR);
    const machine_pin_obj_t *irq_pin =
        resolve_pin(args[ARG_irq].u_obj,   LORAWAN_DEFAULT_IRQ_QSTR);
    const machine_pin_obj_t *rf_sw_pin =
        resolve_pin(args[ARG_rf_sw].u_obj, LORAWAN_DEFAULT_RF_SW_QSTR);

    /* Python/MicroPython-owned SPI object. Caller may pass spi=machine.SPI(...)
       or let this constructor build it from the micropySX126X-style pin
       template:
         spi_bus=3, clk="P111", mosi="P109", miso="P110",
         cs="P206", irq="P015", rst="P001", gpio_busy="P002".
       The object is still a normal MicroPython machine.SPI instance. LoRaWAN
       keeps it rooted for lifetime but does not own deinit after construction. */
    mp_obj_t spi_arg = args[ARG_spi].u_obj;
    if (spi_arg == mp_const_none || spi_arg == MP_OBJ_NULL) {
        mp_obj_t clk_obj = resolve_obj(args[ARG_clk].u_obj, LORAWAN_DEFAULT_CLK_QSTR);
        mp_obj_t mosi_obj = resolve_obj(args[ARG_mosi].u_obj, LORAWAN_DEFAULT_MOSI_QSTR);
        mp_obj_t miso_obj = resolve_obj(args[ARG_miso].u_obj, LORAWAN_DEFAULT_MISO_QSTR);
        spi_arg = lorawan_create_spi_obj(args[ARG_spi_bus].u_int,
            args[ARG_baudrate].u_int, clk_obj, mosi_obj, miso_obj);
    }
    if (!mp_obj_is_type(spi_arg, &machine_spi_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("spi must be machine.SPI when provided"));
    }

    sx126x_board_cfg_t cfg = {
        .spi_bus     = (uint8_t)args[ARG_spi_bus].u_int,
        .spi_baud_hz = (uint32_t)args[ARG_baudrate].u_int,
        .spi_obj     = spi_arg,
        .cs_pin      = (void *)cs_pin,
        .rst_pin     = (void *)rst_pin,
        .gpio_busy_pin = (void *)gpio_busy_pin,
        .irq_pin     = (void *)irq_pin,
        .rf_sw_pin   = (void *)rf_sw_pin,
    };
    /* DWT CYCCNT is used only by optional observation counters. */
    lorawan_stats_dwt_init();

    MP_STATE_PORT(lorawan_spi_obj_root) = spi_arg;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        lorawan_pump_init();

        sx126x_board_init(&cfg);
        self->radio_initialized = true;

        // AGT4/AGT5 are boot-reserved (MICROPY_HW_AGT_RESERVED_MASK); the
        // vendor C board layer opens g_timer0/g_timer1 directly.
        timer_board_init();
        self->timer_initialized = true;

        // Open data flash + restore any persisted blob from the previous
        // boot's session into our RAM cache.
        nvm_board_init();
        nlr_pop();
    } else {
        if (self->timer_initialized) {
            timer_board_deinit();
            self->timer_initialized = false;
        }
        if (self->radio_initialized) {
            SX126xIoIrqDeinit();
            sx126x_board_deinit();
            self->radio_initialized = false;
        }
        lorawan_pump_deinit();
        MP_STATE_PORT(lorawan_spi_obj_root) = mp_const_none;
        nlr_jump(nlr.ret_val);
    }

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t lorawan_mac_deinit(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    /* P1 — stop LoRaMac state machine BEFORE tearing down board/timer.
     * LoRaMacStop() returns LORAMAC_STATUS_BUSY if a TX/RX/timer operation
     * is mid-flight; raising lets the caller retry rather than ripping
     * hardware out from under the MAC. Only call if the stack was actually
     * initialized — Mac() ctor success without lorawan_init() is legal. */
    if (self->stack_initialized) {
        LoRaMacStatus_t st = LoRaMacStop();
        if (st == LORAMAC_STATUS_BUSY) {
            mp_raise_msg(&mp_type_OSError,
                MP_ERROR_TEXT("LoRaMac busy; retry deinit()"));
        }
    }
    SX126xIoIrqDeinit();
    sx126x_board_deinit();
    self->radio_initialized = false;
    if (self->timer_initialized) {
        timer_board_deinit();
        self->timer_initialized = false;
    }
    /* Drop pump scheduling state so a subsequent Mac() ctor
       starts from clean counters. */
    lorawan_pump_deinit();
    /* P1 — fully reset binding-projection state so post-deinit calls to
     * send()/join()/process() see the stack as torn down instead of mid-
     * flight stale data. */
    self->stack_initialized = false;
    self->keys_set = false;
    self->joined = false;
    self->join_confirm_seen = false;
    self->join_confirm_status = (int)LORAMAC_EVENT_INFO_STATUS_ERROR;
    self->rx_pending = false;
    self->rx_port = 0;
    self->rx_len = 0;
    LORAWAN_CB_SLOT(LORAWAN_CB_EVENT) = mp_const_none;
    /* Release LoRaWAN-side references only. Python remains the owner of the
       machine.SPI object, so deinit() does not call spi.deinit(). */
    MP_STATE_PORT(lorawan_spi_obj_root) = mp_const_none;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_deinit_obj, lorawan_mac_deinit);

// ---- LoRaMac stack binding ----------------------------------------------

/* LoRaMac primitive callbacks. Called from `LoRaMacProcess()` in foreground
   mac.process() context. These callbacks may schedule the user's Python event
   callback after LoRaMacProcess() has run; that is separate from the
   DIO1/timer/notify path, which must not use MicroPython scheduler to reach
   the LoRaMac state machine. We do not pass the C struct pointer through to
   Python because the data is copied/released by LoRaMac after the callback
   returns; instead we forward a small QSTR tag so Python can switch on event
   type. */

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
   Runs in foreground mac.process() context (LoRaMacProcess is serialised
   by the foreground service coordinator); mp_sched_schedule queues only the
   user's Python event callback, not the LoRaMac service. */
static void mac_count_event_drop(void) {
    if (s_event_drop_count != UINT32_MAX) {
        ++s_event_drop_count;
    }
}

static void mac_post_event(qstr tag, int status) {
    mp_obj_t cb = LORAWAN_CB_SLOT(LORAWAN_CB_EVENT);
    if (cb == MP_OBJ_NULL || cb == mp_const_none) {
        mac_count_event_drop();
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
        mac_count_event_drop();
    }
}

static void mac_mcps_confirm(McpsConfirm_t *cnf) {
    mac_post_event(MP_QSTR_mcps_confirm, (int)cnf->Status);
}

static void mac_mcps_indication(McpsIndication_t *ind) {
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
    /* On OTAA join completion (MLME_JOIN), update the cached join state
       so Python `mac.is_joined()` becomes true without an extra MIB
       round-trip. Other MLME types (LINK_CHECK, TXCW, etc.) just pass
       through to the user's event sink. */
    if (cnf->MlmeRequest == MLME_JOIN &&
        cnf->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
        lorawan_mac_singleton.joined = true;
    }
    if (cnf->MlmeRequest == MLME_JOIN) {
        lorawan_mac_singleton.join_confirm_seen = true;
        lorawan_mac_singleton.join_confirm_status = (int)cnf->Status;
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

static void mac_nvm_context_change(uint32_t notifyMibFlags) {
    (void)notifyMibFlags;
    /* LoRaMac flagged a persistent-state change (DevNonce,
       FCnt, session keys, ...). Snapshot the contexts into RAM-backed
       NVM; the call sequence stays the same. */
    (void)NvmDataMgmtStore();
}

/* MacProcessNotify requests foreground LoRaMac service, but does NOT schedule
   MicroPython work. It only marks C-side pending state. The explicit
   foreground mac.process() call drains the service coordinator, preserving the "no
   MicroPython scheduler in the DIO1/timer/notify path" contract while still
   deferring against sx126x_board_spi_busy() and flash-busy gates. */

static void mac_process_notify(void) {
    lorawan_driver_request_pump();
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
    .GetTemperatureLevel = NULL,    /* vendor stack tolerates NULL; no sensor wired */
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
    if (self->stack_initialized) {
        return MP_OBJ_NEW_SMALL_INT((int)LORAMAC_STATUS_OK);
    }
    LoRaMacStatus_t st = LoRaMacInitialization(&s_primitives, &s_callbacks,
        LORAMAC_REGION_EU868);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("LoRaMacInitialization failed: %d"), (int)st);
    }
    /* Restore persisted contexts (DevNonce, FCnt, session
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
    /* Initialise the foreground service coordinator. Idempotent: a previous
       deinit() left storage stale but the init zeros everything. */
    lorawan_pump_init();
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NETWORK_ACTIVATION;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        self->joined = (mib.Param.NetworkActivation != ACTIVATION_TYPE_NONE);
    } else {
        self->joined = false;
    }
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

static mp_obj_t lorawan_mac_process(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->stack_initialized) {
        return mp_const_none;
    }
    lorawan_driver_request_pump();
    lorawan_driver_pump_run();
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

/* EU868 datarates: 0=SF12/125kHz ... 5=SF7/125kHz. Default 5
 * matches typical TTN OTAA practice. */
static int8_t lorawan_mac_join_parse_datarate(size_t n_args,
    const mp_obj_t *args) {
    enum { ARG_self, ARG_datarate };
    if (n_args < 1 || n_args > 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("join([datarate])"));
    }
    int8_t datarate = 5;
    if (n_args == 2) {
        mp_int_t dr = mp_obj_get_int(args[ARG_datarate]);
        if (dr < 0 || dr > 5) {
            mp_raise_ValueError(MP_ERROR_TEXT("EU868 datarate 0..5"));
        }
        datarate = (int8_t)dr;
    }
    return datarate;
}

static LoRaMacStatus_t lorawan_mac_join_start(lorawan_mac_obj_t *self,
    int8_t datarate) {
    if (!self->stack_initialized || !self->keys_set) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("call lorawan_init + set_keys first"));
    }

    /* Reset cached join state — will be set true again from
       mac_mlme_confirm when the JoinAccept arrives. */
    self->joined = false;
    self->join_confirm_seen = false;
    self->join_confirm_status = (int)LORAMAC_EVENT_INFO_STATUS_ERROR;

    MlmeReq_t req;
    memset(&req, 0, sizeof(req));
    req.Type = MLME_JOIN;
    req.Req.Join.Datarate = datarate;
    return LoRaMacMlmeRequest(&req);
}

/* OTAA join, asynchronous variant.
 *
 * `mac.join_async(datarate=5)` preserves the old binding behaviour: it
 * submits MLME_JOIN and returns the immediate LoRaMac request status
 * (0 = accepted). The caller must keep calling `mac.process()` until the
 * `mlme_confirm` event arrives.
 */
static mp_obj_t lorawan_mac_join_async(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self };
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(args[ARG_self]);
    int8_t datarate = lorawan_mac_join_parse_datarate(n_args, args);
    LoRaMacStatus_t st = lorawan_mac_join_start(self, datarate);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_join_async_obj, 1,
    lorawan_mac_join_async);

/* OTAA join, Renesas-style blocking default.
 *
 * Renesas LoRaMac callbacks only set pending flags from IRQ/timer context;
 * LoRaMacProcess() must be serviced in foreground to open RX windows and
 * consume DIO1 radio IRQs. `mac.join()` therefore holds control and drains
 * the foreground pump until the MLME_JOIN confirm is observed, so Python
 * prints/sleeps cannot delay the critical RX1/RX2 setup.
 *
 * Returns the MLME confirm status after an accepted request (0 = joined,
 * 4 = RX2_TIMEOUT, etc.). If the request is rejected immediately, returns
 * that LoRaMacStatus_t value as before. A local guard timeout returns
 * -ETIMEDOUT; normal no-JoinAccept cases should complete as RX2_TIMEOUT.
 */
static mp_obj_t lorawan_mac_join(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self };
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(args[ARG_self]);
    int8_t datarate = lorawan_mac_join_parse_datarate(n_args, args);
    LoRaMacStatus_t st = lorawan_mac_join_start(self, datarate);
    if (st != LORAMAC_STATUS_OK) {
        return MP_OBJ_NEW_SMALL_INT((int)st);
    }

    uint32_t t0 = mp_hal_ticks_ms();
    while (!self->join_confirm_seen) {
        lorawan_driver_request_pump();
        lorawan_driver_pump_run();
        if ((uint32_t)(mp_hal_ticks_ms() - t0) >
            LORAWAN_JOIN_DEFAULT_TIMEOUT_MS) {
            return MP_OBJ_NEW_SMALL_INT(-MP_ETIMEDOUT);
        }
    }
    return MP_OBJ_NEW_SMALL_INT(self->join_confirm_status);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_join_obj, 1, lorawan_mac_join);

static mp_obj_t lorawan_mac_is_joined(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(self->joined);
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
    return mp_obj_new_tuple(2, items);
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

static mp_obj_t lorawan_mac_set_datarate(mp_obj_t self_in, mp_obj_t dr_in) {
    (void)self_in;
    mp_int_t dr = mp_obj_get_int(dr_in);
    if (dr < 0 || dr > 15) {
        mp_raise_ValueError(MP_ERROR_TEXT("dr must be 0..15"));
    }
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_CHANNELS_DATARATE;
    mib.Param.ChannelsDatarate = (int8_t)dr;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("MIB_CHANNELS_DATARATE set failed"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_datarate_obj,
    lorawan_mac_set_datarate);

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
        mp_obj_new_int_from_uint((uint32_t)t.Seconds),
        MP_OBJ_NEW_SMALL_INT(t.SubSeconds),
    };
    return mp_obj_new_tuple(2, items);
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
    return mp_obj_new_str(&c, 1);
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

/* Adaptive Data Rate.
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
    return mp_obj_new_bool(mib.Param.AdrEnable);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_get_adr_obj,
    lorawan_mac_get_adr);

/* RX window timing tunables.
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

/* MIB_PUBLIC_NETWORK setter. ChirpStack and TTN gateways use the
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
    bool enable = mp_obj_is_true(enable_in);
    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_PUBLIC_NETWORK;
    mib.Param.EnablePublicNetwork = enable;
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("MIB_PUBLIC_NETWORK set failed: %d"), (int)st);
    }
    /* Vendor LoRaMac.c deliberately omits the Radio.SetPublicNetwork() call
     * when MIB_PUBLIC_NETWORK is set (LoRaMac.c:5231). Propagate to the
     * radio layer explicitly so the SX126x LoRa syncword register matches
     * what the gateway transmits. */
    RadioSetPublicNetwork(enable);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(lorawan_mac_set_public_network_obj,
    lorawan_mac_set_public_network);

/* Apply the MIB defaults that the Renesas reference sample writes right after
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
    /* Propagate to radio layer — see RadioSetPublicNetwork forward decl
     * comment near top of file. LoRaMac.c:5231 deliberately skips this. */
    RadioSetPublicNetwork(true);
    _SET_MIB_OR_RAISE(MIB_ADR,                 AdrEnable,           true);
    _SET_MIB_OR_RAISE(MIB_DEVICE_CLASS,        Class,               CLASS_A);
    _SET_MIB_OR_RAISE(MIB_SYSTEM_MAX_RX_ERROR, SystemMaxRxError,    10);
    /* Board policy MinRxSymbols=24 (upstream default 6 = ~6 ms net window
     * at SF7, too tight on VK_RA4M2 for accumulated Python pump latency +
     * AGT 244 µs quantization + SOSC cold-boot drift + gateway TX jitter).
     * HIL 2026-05-23: 6 → RX2_TIMEOUT; 24 → join OK at +5.20 s. */
    _SET_MIB_OR_RAISE(MIB_MIN_RX_SYMBOLS,      MinRxSymbols,        24);
    _SET_MIB_OR_RAISE(MIB_CHANNELS_DATARATE,   ChannelsDatarate,    DR_0);
    _SET_MIB_OR_RAISE(MIB_DUTY_CYCLE,          DCycleEnabled,       1);

    #undef _SET_MIB_OR_RAISE
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_init_defaults_obj,
    lorawan_mac_init_defaults);

/* Explicit save/restore hooks for persisted LoRaMac contexts. */
static mp_obj_t lorawan_mac_nvm_store(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(NvmDataMgmtStore());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_nvm_store_obj,
    lorawan_mac_nvm_store);

static mp_obj_t lorawan_mac_nvm_restore(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint16_t restored = NvmDataMgmtRestore();

    MibRequestConfirm_t mib;
    memset(&mib, 0, sizeof(mib));
    mib.Type = MIB_NETWORK_ACTIVATION;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        self->joined = (mib.Param.NetworkActivation != ACTIVATION_TYPE_NONE);
    }
    return MP_OBJ_NEW_SMALL_INT(restored);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_nvm_restore_obj,
    lorawan_mac_nvm_restore);

static mp_obj_t lorawan_mac_nvm_factory_reset(mp_obj_t self_in) {
    (void)self_in;
    return mp_obj_new_bool(NvmDataMgmtFactoryReset());
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_nvm_factory_reset_obj,
    lorawan_mac_nvm_factory_reset);

/* Uplink + downlink.
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
    memcpy(s_tx_buf, buf.buf, buf.len);

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
    LoRaMacStatus_t st = LoRaMacMcpsRequest(&req);
    return MP_OBJ_NEW_SMALL_INT((int)st);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(lorawan_mac_send_obj, 3, lorawan_mac_send);

static mp_obj_t lorawan_mac_recv(mp_obj_t self_in) {
    lorawan_mac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->rx_pending) {
        return mp_const_none;
    }
    /* Snapshot under PRIMASK to avoid race with mac_mcps_indication
       running from foreground mac.process() context. */
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
        mp_obj_new_bytes(tmp, len),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(lorawan_mac_recv_obj, lorawan_mac_recv);

// ---- Class table ---------------------------------------------------------

static const mp_rom_map_elem_t lorawan_mac_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_deinit),             MP_ROM_PTR(&lorawan_mac_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_lorawan_init),       MP_ROM_PTR(&lorawan_mac_lorawan_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_keys),           MP_ROM_PTR(&lorawan_mac_set_keys_obj) },
    { MP_ROM_QSTR(MP_QSTR_process),            MP_ROM_PTR(&lorawan_mac_process_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_event_callback), MP_ROM_PTR(&lorawan_mac_set_event_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_status),             MP_ROM_PTR(&lorawan_mac_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_join),               MP_ROM_PTR(&lorawan_mac_join_obj) },
    { MP_ROM_QSTR(MP_QSTR_join_async),         MP_ROM_PTR(&lorawan_mac_join_async_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_joined),          MP_ROM_PTR(&lorawan_mac_is_joined_obj) },
    { MP_ROM_QSTR(MP_QSTR_link_check),         MP_ROM_PTR(&lorawan_mac_link_check_obj) },
    { MP_ROM_QSTR(MP_QSTR_last_link_check),    MP_ROM_PTR(&lorawan_mac_last_link_check_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_datarate),       MP_ROM_PTR(&lorawan_mac_get_datarate_obj) },
    { MP_ROM_QSTR(MP_QSTR_device_time_req),    MP_ROM_PTR(&lorawan_mac_device_time_req_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_sys_time),       MP_ROM_PTR(&lorawan_mac_get_sys_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_class),          MP_ROM_PTR(&lorawan_mac_set_class_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_class),          MP_ROM_PTR(&lorawan_mac_get_class_obj) },
    { MP_ROM_QSTR(MP_QSTR_beacon_acquisition), MP_ROM_PTR(&lorawan_mac_beacon_acquisition_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_ping_slot_periodicity), MP_ROM_PTR(&lorawan_mac_set_ping_slot_periodicity_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_adr),            MP_ROM_PTR(&lorawan_mac_set_adr_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_adr),            MP_ROM_PTR(&lorawan_mac_get_adr_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_max_rx_error),   MP_ROM_PTR(&lorawan_mac_set_max_rx_error_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_min_rx_symbols), MP_ROM_PTR(&lorawan_mac_set_min_rx_symbols_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_public_network), MP_ROM_PTR(&lorawan_mac_set_public_network_obj) },
    { MP_ROM_QSTR(MP_QSTR_init_defaults),      MP_ROM_PTR(&lorawan_mac_init_defaults_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_datarate),       MP_ROM_PTR(&lorawan_mac_set_datarate_obj) },
    { MP_ROM_QSTR(MP_QSTR_nvm_store),          MP_ROM_PTR(&lorawan_mac_nvm_store_obj) },
    { MP_ROM_QSTR(MP_QSTR_nvm_restore),        MP_ROM_PTR(&lorawan_mac_nvm_restore_obj) },
    { MP_ROM_QSTR(MP_QSTR_nvm_factory_reset),  MP_ROM_PTR(&lorawan_mac_nvm_factory_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_send),               MP_ROM_PTR(&lorawan_mac_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv),               MP_ROM_PTR(&lorawan_mac_recv_obj) },
};
static MP_DEFINE_CONST_DICT(lorawan_mac_locals_dict,
    lorawan_mac_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    lorawan_mac_type,
    MP_QSTR_Mac,
    MP_TYPE_FLAG_NONE,
    make_new, lorawan_mac_make_new,
    locals_dict, &lorawan_mac_locals_dict
);

// ---- Module-level globals -----------------------------------------------

static const mp_rom_map_elem_t lorawan_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),         MP_ROM_QSTR(MP_QSTR_lorawan) },
    { MP_ROM_QSTR(MP_QSTR_Mac),              MP_ROM_PTR(&lorawan_mac_type) },
    { MP_ROM_QSTR(MP_QSTR_LoRaWAN),          MP_ROM_PTR(&lorawan_mac_type) },
    { MP_ROM_QSTR(MP_QSTR_EU868),            MP_ROM_QSTR(MP_QSTR_EU868) },
};
static MP_DEFINE_CONST_DICT(lorawan_module_globals,
    lorawan_module_globals_table);

const mp_obj_module_t mp_module_lorawan = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lorawan_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_lorawan, mp_module_lorawan);

/* GC root for the Mac event callback. */
MP_REGISTER_ROOT_POINTER(mp_obj_t lorawan_mac_root_callbacks[1]);  /* literal must match LORAWAN_CB_COUNT above */

/* GC root for the Python-owned machine.SPI object — the Mac singleton lives
   in .bss which gc_collect_root() does not walk, so the SPI ref must live
   in MP_STATE_PORT. User may still call spi.deinit(); LoRaWAN stops using
   it after deinit(). */
MP_REGISTER_ROOT_POINTER(mp_obj_t lorawan_spi_obj_root);

#endif // MICROPY_HW_LORA_STACK_RENESAS
