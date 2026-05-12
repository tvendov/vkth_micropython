/*
 * lorawan/glue/sx126x_board.c
 *
 * SX126x board glue — Phase 1 implementation.
 *
 * Replicates the SPI / GPIO / IRQ behaviour of the proven Python driver at
 *   ports/renesas-ra/boards/VK_RA4M2/examples/LoRa/_upstream/sx126x.py
 *
 * Pin map and SPI timing are identical (see sx126x_board.h header for the
 * full table). The Python driver was validated against TTN OTAA + uplink
 * on VK_RA4M2 (commit e11f4939c — switch SX1262 SPI from SoftSPI to HW
 * SPI(3) on SCI9), so this port keeps every micro-detail.
 *
 * Notable design constraints (carried over from the Python driver):
 *
 * 1. Byte-at-a-time SPI shifts. At 8 MHz the SX1262 cannot prepare its
 *    status byte if bytes flow back-to-back; we issue one
 *    ra_sci_spi_transfer per byte and rely on FSP setup overhead (~3 µs)
 *    as the inter-byte gap. This matches sx126x.py:1340-1397.
 *
 * 2. NSS = manual GPIO toggle, NOT hardware SSL. The MicroPython SCI-SPI
 *    backend on VK_RA4M2 does not drive a chip-select line; the Python
 *    driver toggles P206 directly. We do the same.
 *
 * 3. BUSY polling. Before every SPI transaction we wait BUSY=low (with
 *    timeout, calling mp_handle_pending so REPL stays responsive). After
 *    transactions that change radio state (Reset, SetTx, SetRx, etc.)
 *    we poll BUSY a second time after NSS deassert.
 *
 * 4. Reset sequence: HIGH → LOW → HIGH each separated by 150 µs, then
 *    poll standby() for up to 3 s. Bit-perfect copy of sx126x.py:241-268.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "py/mperrno.h"

#include "glue/sx126x_board.h"
#include "glue/lorawan_stats.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

// Pull in the renesas-ra-private headers AFTER the public glue header so
// the public surface (which only forwards `void *` pin handles) does not
// transitively expose pin.h to other compilation units.
#include "pin.h"
#include "extint.h"
#include "ra/ra_sci_spi.h"
#include "ra/ra_icu.h"
#include "ra/ra_gpio.h"

/* PWPR helpers — direct register pokes (ra_gpio.c marks pwpr_unprotect /
   pwpr_protect as `static`, so we cannot link to them and re-create the
   sequence locally). */
#define _PWPR_LOC (*(volatile uint8_t *)(R_PFS_BASE + 0x503))
static inline void pfs_pwpr_unprotect(void) { _PWPR_LOC = 0x00; _PWPR_LOC = 0x40; }
static inline void pfs_pwpr_protect(void)   { _PWPR_LOC = 0x80; }

// Pulls in RP_TCXO_STAB_TIME and other board-level constants used by
// TCXO setup (SX126xIoTcxoInit) and the LoRaMac-compat layer.
#include "glue/board.h"

// ---- SX126x opcodes / status bits (subset needed by Phase 1) ------------

#define SX126X_CMD_NOP                  (0x00)
#define SX126X_CMD_GET_STATUS           (0xC0)
#define SX126X_CMD_WRITE_REGISTER       (0x0D)
#define SX126X_CMD_READ_REGISTER        (0x1D)
#define SX126X_CMD_WRITE_BUFFER         (0x0E)
#define SX126X_CMD_READ_BUFFER          (0x1E)
#define SX126X_CMD_SET_STANDBY          (0x80)
#define SX126X_CMD_SET_SLEEP            (0x84)
#define SX126X_CMD_SET_RX               (0x82)
#define SX126X_CMD_SET_TX               (0x83)
#define SX126X_CMD_SET_FS               (0xC1)
#define SX126X_CMD_SET_CAD              (0xC5)
#define SX126X_CMD_CALIBRATE            (0x89)
#define SX126X_CMD_CALIBRATE_IMAGE      (0x98)

#define SX126X_STANDBY_RC               (0x00)

#define SX126X_STATUS_CMD_TIMEOUT       (0x06)
#define SX126X_STATUS_CMD_INVALID       (0x08)
#define SX126X_STATUS_CMD_FAILED        (0x0A)
#define SX126X_STATUS_SPI_FAILED        (0xFF)

/* P3.5 — opcode-aware BUSY timeout table.
 *
 * Pre-CS BUSY wait inside sx126x_spi_xfer knows the opcode. Replace the
 * uniform 5 s ceiling with per-opcode budgets sized to the actual SX1262
 * chip-protocol latencies (datasheet §13). A stuck-radio condition is now
 * surfaced ~25× faster on cheap opcodes (200 µs vs 5 s); SetTx ramp keeps
 * 50 ms; only truly unknown opcodes fall through to the 5 s safety net.
 *
 * Datasheet anchors (§13.1.x):
 *   GetStatus      ~ 1 µs   (no state change)
 *   ReadRegister   ~ µs     (just SPI read)
 *   WriteRegister  ~ µs
 *   ReadBuffer     ~ µs
 *   WriteBuffer    ~ µs
 *   SetSleep       ~ 1 ms   (transition; chip holds BUSY=HIGH until next NSS wake — wake handled by P3.0)
 *   SetStandby     ~ 1 ms   (RC mode; XOSC variant slower)
 *   SetFs          ~ 50 µs  (frequency-synthesis)
 *   SetCad         ~ 50 ms  (channel-activity-detect — RX-like)
 *   SetTx          ~ 50 ms  (PA ramp + packet-start)
 *   SetRx          ~ 50 ms  (LNA cal + RX start)
 *   Calibrate      ~ 10 ms
 *   CalibrateImage ~ 10 ms
 *
 * Budgets include 2-3× headroom for worst-case temperature / Vcc.
 * Post-CS path (SX126xWaitOnBusy without opcode context) keeps the 5 s
 * default; its callers (vendor sx126x.c, mid-SPI internal waits) own
 * the latency model.
 */
#define SX126X_BUSY_TIMEOUT_MS                (5000)  /* unknown / post-CS fallback */
#define SX126X_BUSY_TIMEOUT_CHEAP_MS          (5)     /* register/buffer/status: pure SPI */
#define SX126X_BUSY_TIMEOUT_MODE_TRANSITION_MS (20)   /* SetSleep, SetStandby, SetFs */
#define SX126X_BUSY_TIMEOUT_CALIBRATE_MS      (50)    /* Calibrate, CalibrateImage */
#define SX126X_BUSY_TIMEOUT_RADIO_OP_MS       (200)   /* SetTx, SetRx, SetCad ramp */

static inline uint32_t sx126x_busy_timeout_ms_for_opcode(uint8_t opcode) {
    switch (opcode) {
        case SX126X_CMD_GET_STATUS:
        case SX126X_CMD_READ_REGISTER:
        case SX126X_CMD_WRITE_REGISTER:
        case SX126X_CMD_READ_BUFFER:
        case SX126X_CMD_WRITE_BUFFER:
            return SX126X_BUSY_TIMEOUT_CHEAP_MS;
        case SX126X_CMD_SET_SLEEP:
        case SX126X_CMD_SET_STANDBY:
        case SX126X_CMD_SET_FS:
            return SX126X_BUSY_TIMEOUT_MODE_TRANSITION_MS;
        case SX126X_CMD_CALIBRATE:
        case SX126X_CMD_CALIBRATE_IMAGE:
            return SX126X_BUSY_TIMEOUT_CALIBRATE_MS;
        case SX126X_CMD_SET_TX:
        case SX126X_CMD_SET_RX:
        case SX126X_CMD_SET_CAD:
            return SX126X_BUSY_TIMEOUT_RADIO_OP_MS;
        default:
            return SX126X_BUSY_TIMEOUT_MS;
    }
}

// Inter-byte settle time default. Runtime-tunable via
// sx126x_board_set_interbyte_us(). 0 = rely on FSP/DTC setup overhead
// (~3-8 µs/call) alone — typically sufficient. Bump to 5-10 if status
// returns 0x00/0xFF.
#ifndef LORAWAN_SX126X_INTERBYTE_US_DEFAULT
#define LORAWAN_SX126X_INTERBYTE_US_DEFAULT (0)
#endif

// ---- Driver state --------------------------------------------------------

typedef struct {
    bool initialized;
    uint8_t spi_id;                       // SPI(3) on VK_RA4M2 → SCI9
    const machine_pin_obj_t *cs;
    const machine_pin_obj_t *rst;
    const machine_pin_obj_t *busy;
    const machine_pin_obj_t *dio1;
    const machine_pin_obj_t *rf_sw;       // may be NULL

    // DIO1 IRQ direct-ICU registration. Signature matches LoRaMac-node
    // `typedef void (DioIrqHandler)(void)` — handlers close over the
    // singleton SX126x global instead of taking a context argument.
    void (*dio1_c_handler)(void);
    bool  dio1_irq_active;
    uint8_t dio1_irq_no;
    mp_sched_node_t dio1_sched_node;

    // Optional BUSY pin falling-edge IRQ (Phase 2). AD5.6 folded the
    // legacy busy_low_seen flag into SPI_CMPL_BUSY_LOW — wait-on-flag is
    // the single mechanism now.
    bool busy_irq_active;
    uint8_t busy_irq_no;

    // Runtime-tunable inter-byte delay (Phase 2 / item 4).
    uint8_t interbyte_us;
} sx126x_state_t;

static sx126x_state_t s_state;

// Internal: assert/deassert NSS, abstracted so the SPI helper stays clean.
static inline void cs_low(void)  { mp_hal_pin_low((mp_hal_pin_obj_t)s_state.cs); }
static inline void cs_high(void) { mp_hal_pin_high((mp_hal_pin_obj_t)s_state.cs); }

// Internal: BUSY pin read (1 = busy, 0 = ready).
static inline bool busy_high(void) {
    return mp_hal_pin_read((mp_hal_pin_obj_t)s_state.busy) != 0;
}

// ---- Lifecycle -----------------------------------------------------------

bool sx126x_board_is_initialized(void) {
    return s_state.initialized;
}

void sx126x_board_init(const sx126x_board_cfg_t *cfg) {
    if (cfg == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("sx126x cfg required"));
    }
    if (cfg->cs_pin == NULL || cfg->reset_pin == NULL ||
        cfg->busy_pin == NULL || cfg->dio1_pin == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT(
            "cs/reset/busy/dio1 pins required"));
    }

    s_state.spi_id  = cfg->spi_id;
    s_state.cs      = (const machine_pin_obj_t *)cfg->cs_pin;
    s_state.rst     = (const machine_pin_obj_t *)cfg->reset_pin;
    s_state.busy    = (const machine_pin_obj_t *)cfg->busy_pin;
    s_state.dio1    = (const machine_pin_obj_t *)cfg->dio1_pin;
    s_state.rf_sw   = (const machine_pin_obj_t *)cfg->rf_sw_pin;
    s_state.dio1_c_handler = NULL;
    s_state.dio1_irq_active = false;
    s_state.dio1_irq_no = 0xFF;
    s_state.busy_irq_active = false;
    s_state.busy_irq_no = 0xFF;
    s_state.interbyte_us = LORAWAN_SX126X_INTERBYTE_US_DEFAULT;
    s_state.dio1_sched_node.next = NULL;
    s_state.dio1_sched_node.callback = NULL;

    // RF switch enable BEFORE SPI init (matches lorawan_app.py:1056).
    if (s_state.rf_sw != NULL) {
        mp_hal_pin_output((mp_hal_pin_obj_t)s_state.rf_sw);
        mp_hal_pin_high((mp_hal_pin_obj_t)s_state.rf_sw);
    }

    // SPI3 = SCI9 simple SPI: 8 MHz, MODE0, MSB-first, 8-bit.
    // ra_sci_spi_init signature: (ch, mosi_pin, miso_pin, sck_pin, baud,
    //                             polarity, phase, firstbit).
    // Pins are passed as MicroPython pin index (board pin number).
    // For VK_RA4M2 the user is expected to construct SPI(3, ...) once
    // before constructing lorawan.Mac() — that path runs the canonical
    // ra_sci_spi_init via machine_hard_spi_init. Here we re-init only if
    // not already active (defensive: covers raw lorawan.Mac() use).
    {
        // The Python user normally executes:
        //   spi = SPI(3, baudrate=8000000, polarity=0, phase=0)
        // before instantiating lorawan.Mac(). That call wires SCI9 via the
        // same ra_sci_spi_init backend we'd call here, so we trust it and
        // skip a redundant init. If init is needed we'd need MOSI/MISO/SCK
        // pin numbers — those are board-defined in mpconfigboard.h:132-136.
        (void)cfg->spi_baud_hz;
    }

    // CS high (idle), output.
    mp_hal_pin_output((mp_hal_pin_obj_t)s_state.cs);
    mp_hal_pin_high((mp_hal_pin_obj_t)s_state.cs);

    // RESET high (idle), output.
    mp_hal_pin_output((mp_hal_pin_obj_t)s_state.rst);
    mp_hal_pin_high((mp_hal_pin_obj_t)s_state.rst);

    // BUSY input (no pull — Wio-SX1262 drives it actively).
    mp_hal_pin_input((mp_hal_pin_obj_t)s_state.busy);

    // DIO1 input (rising-edge IRQ wired separately via SX126xIoIrqInit).
    mp_hal_pin_input((mp_hal_pin_obj_t)s_state.dio1);

    /* Bump SPI3 (SCI9) pin drive strength to HIGH on P109/P110/P111
       so MOSI/MISO/SCK rise/fall edges stay clean at 8 MHz under the
       Wio-SX1262 daughterboard's bus capacitance. Default after
       machine.SPI(3,...) init is GPIO_LOW_POWER (DSCR=00). */
    pfs_pwpr_unprotect();
    _PXXPFS(1, 9)  |= (DSCR1_MASK | DSCR_MASK);   // P109 = MOSI
    _PXXPFS(1, 10) |= (DSCR1_MASK | DSCR_MASK);   // P110 = MISO
    _PXXPFS(1, 11) |= (DSCR1_MASK | DSCR_MASK);   // P111 = SCK
    _PXXPFS(2, 6)  |= (DSCR1_MASK | DSCR_MASK);   // P206 = CS (manual GPIO)
    pfs_pwpr_protect();

    s_state.initialized = true;

    /* AD5.4: arm BUSY-falling ICU at boot so SPI_CMPL_BUSY_LOW counters and
       (later, AD5.5) the pre-CS BUSY yield loop are populated from the
       first SX1262 transaction. Falls back silently if the BUSY pin has
       no ICU vector — caller path keeps polling. */
    (void)SX126xBusyIrqEnable();
}

void sx126x_board_deinit(void) {
    if (!s_state.initialized) {
        return;
    }
    SX126xIoIrqDeinit();
    SX126xBusyIrqDisable();
    cs_high();
    s_state.initialized = false;
}

// ---- Reset (sx126x.py:241-256, exact replica) ----------------------------

void SX126xReset(void) {
    if (!s_state.initialized) {
        return;
    }
    mp_hal_pin_high((mp_hal_pin_obj_t)s_state.rst);
    mp_hal_delay_us(150);
    mp_hal_pin_low((mp_hal_pin_obj_t)s_state.rst);
    mp_hal_delay_us(150);
    mp_hal_pin_high((mp_hal_pin_obj_t)s_state.rst);
    mp_hal_delay_us(150);
}

// ---- BUSY-poll helper ---------------------------------------------------

/* SPI/BUSY completion-flag word — set by ISRs, polled by yielding spin
   in AD5.3+. Lives in .bss; single-byte aligned access is atomic on
   Armv8-M. Writers OR-in a bit; the foreground reader/clearer holds the
   s_spi_xfer_busy guard for the pre-CS path (AD5.5). The post-CS path
   (SX126xWaitOnBusy, AD5.6) is multi-call-site outside that guard, but
   still benefits from the same single-bit signaling because the only
   bit it touches is SPI_CMPL_BUSY_LOW and the ISR is the only writer
   from the other side. Hoisted above SX126xWaitOnBusy so both pre-CS
   (sx126x_spi_xfer) and post-CS (this) callers see the same symbols. */
static volatile uint8_t s_spi_completion_flag;

#define SPI_CMPL_DTC_DONE   (1u << 0)   /* DTC RX-end IRQ fired         */
#define SPI_CMPL_BUSY_LOW   (1u << 1)   /* BUSY-falling ICU IRQ fired   */
#define SPI_CMPL_TIMEOUT    (1u << 2)   /* Foreground timeout observed  */

void SX126xWaitOnBusy(void) {
    if (!s_state.initialized) {
        return;
    }

    /* AD5.6: wait-on-flag for post-CS BUSY low. Multi-call-site (the
       vendor radio layer in lorawan/radio/sx126x.c calls this outside
       sx126x_spi_xfer), so:
         - No opcode latching (no STATS_SET_OPCODE) — that work belongs to
           the pre-CS path in sx126x_spi_xfer where s_spi_xfer_busy holds
           the single-writer invariant (AD5.5 §8 invariant (a)/(c)).
         - No SPI_CMPL_TIMEOUT interaction — that diagnostic bit is owned
           by sx126x_spi_xfer's pre-CS path and the AD5.3 DTC yield loop.
         - No s_spi_xfer_busy lifecycle touch — caller (vendor) owns it.
       Clear SPI_CMPL_BUSY_LOW at entry so we observe only this wait's
       falling edge, then yield on the bit (set by busy_icu_isr). */
    s_spi_completion_flag &= ~SPI_CMPL_BUSY_LOW;

    STATS_INC(busy_wait_count);
    LORAWAN_BUSY_TIMING_BEGIN();
    if (busy_high()) {
        uint32_t start = mp_hal_ticks_ms();
        while (!(s_spi_completion_flag & SPI_CMPL_BUSY_LOW)) {
            /* R2 fallback: ICU may have fired before we cleared the bit,
               or the rising-falling cycle was shorter than one yield
               quantum. Re-read BUSY; if low, self-set and exit. */
            if (!busy_high()) {
                s_spi_completion_flag |= SPI_CMPL_BUSY_LOW;
                break;
            }
            if ((uint32_t)(mp_hal_ticks_ms() - start) > SX126X_BUSY_TIMEOUT_MS) {
                STATS_INC(busy_timeout_count);
                /* No STATS_SET_OPCODE(busy_timeout_opcode, ...) here:
                   SX126xWaitOnBusy is multi-call-site without opcode
                   context. The pre-CS path in sx126x_spi_xfer latches
                   busy_timeout_opcode under the s_spi_xfer_busy single-
                   writer invariant (AD5.5 §8 invariant (c)). */
#ifndef LORAWAN_OBSERVATION_DISABLE
                uint32_t _busy_dt = mp_hal_ticks_us() - _busy_t0;
                STATS_STORE(busy_wait_last_us, _busy_dt);
                STATS_UPDATE_MAX(busy_wait_max_us, _busy_dt);
                STATS_UPDATE_MAX(spi_stage_post_busy_max_us, _busy_dt);  /* AD5.7 */
#endif
                return;  /* vendor return contract: caller continues */
            }
            mp_handle_pending(true);
        }
    }
    LORAWAN_BUSY_TIMING_END(busy_wait_last_us, busy_wait_max_us);
#ifndef LORAWAN_OBSERVATION_DISABLE
    /* AD5.7 — post-CS stage delta. Reuses _busy_t0 from
       LORAWAN_BUSY_TIMING_BEGIN(); LORAWAN_BUSY_TIMING_END leaves it in
       scope (declared at the same block level). */
    {
        uint32_t _stage_dt = mp_hal_ticks_us() - _busy_t0;
        STATS_UPDATE_MAX(spi_stage_post_busy_max_us, _stage_dt);
    }
#endif
}

void SX126xWakeup(void) {
    // sx126x.py wakes the chip by toggling NSS — the chip's clock driver
    // sees the falling edge and exits sleep. A single GET_STATUS opcode
    // wrapped in NSS low/high is sufficient; the BUSY-poll inside the
    // exchange handles the wakeup latency.
    if (!s_state.initialized) {
        return;
    }
    /* P3.0 — count this wake pulse. SX126xCheckDeviceReady() in vendor
       sx126x.c only calls SX126xWakeup() when OperatingMode is SLEEP /
       RX_DC / COLD_SLEEP, so this counter reflects real wake events. */
    STATS_INC(sx126x_wake_count);
    cs_low();
    mp_hal_delay_us(30);  /* SX1262 §9.6: NSS-low > 20 µs to leave sleep. */
    cs_high();
    SX126xWaitOnBusy();
}

// ---- SPI exchange core — S-2 block-streamed (atomic single transfer) ----
//
// Returns 0 on success, negative on timeout / SPI failure.
// `cmd`     : opcode + (optional) address bytes; cmd_len = 1..3 typically.
// `data_out`: payload to write (may be NULL when reading).
// `data_in` : payload to read into (may be NULL when writing).
// `data_len`: payload length.
// `wait_for_busy`: poll BUSY=low after NSS-high.
//
// Per PORT_REQUIREMENTS AD-3 / S-2: one SPI transfer = one DTC burst.
// SX126x command body (opcode + payload OR opcode + status-NOP + read NOPs)
// assembled into a single TX buffer; ra_sci_spi_transfer streams the whole
// thing through DTC autonomously (TX/RX pair, full-duplex, 1 IRQ at end).
// CPU sleeps via WFI inside ra_sci_spi until DTC RX completion.

#define SX126X_SPI_MAX_XFER  (260u)  /* 256 FIFO + opcode + 3 address bytes */

/* Re-entrancy guard. Set true while sx126x_spi_xfer is mid-transfer.
   Prevents nested SPI calls from a deferred dio1_sched_dispatch (which
   runs from mp_handle_pending(true) inside our outer call's BUSY-poll)
   from clobbering chip state and our static buffers. The DIO1 dispatch
   re-schedules itself if guarded — work runs after outer SPI done. */
static volatile bool s_spi_xfer_busy = false;

/* s_spi_completion_flag + SPI_CMPL_* bit macros are hoisted above
   SX126xWaitOnBusy (AD5.6) so both pre-CS and post-CS waiters see the
   same symbols — see the definition there for full rationale. */

/* DTC done-callback for ra_sci_spi_submit. Hard-IRQ-safe: flag write +
   counter macros only. Defined in AD5.2 but NOT yet wired — AD5.3 will
   pass this to ra_sci_spi_submit. Until then, sx126x_spi_xfer keeps
   using the blocking ra_sci_spi_transfer wrapper and this body never
   runs (hard_isr_dtc_count stays 0). __attribute__((unused)) silences
   -Werror=unused-function for the AD5.2 build. */
static void sx126x_dtc_done_cb(uint32_t ch, void *user) __attribute__((unused));
static void sx126x_dtc_done_cb(uint32_t ch, void *user) {
    (void)ch;
    (void)user;
    LORAWAN_ISR_CYCLES_BEGIN();
    s_spi_completion_flag |= SPI_CMPL_DTC_DONE;
    STATS_INC(hard_isr_dtc_count);
    LORAWAN_ISR_CYCLES_END(hard_isr_dtc_cycles_max);
}

static int sx126x_spi_xfer(const uint8_t *cmd, uint16_t cmd_len,
    const uint8_t *data_out, uint8_t *data_in, uint16_t data_len,
    bool wait_for_busy) {

    if (!s_state.initialized) {
        return -MP_EIO;
    }

    /* Re-entrancy refused. Caller (deferred DIO1 dispatch) should bail and
       leave its work for the next scheduler tick — by then outer SPI is done. */
    if (s_spi_xfer_busy) {
        STATS_INC(sx126x_spi_busy_reject_count);
        return -MP_EBUSY;
    }
    s_spi_xfer_busy = true;

    /* Standard read layout — chip emits status during the byte
       immediately after the address, payload follows.
       Layout:  TX = [cmd..., NOP_status, NOP_data...]
                RX = [_,      STATUS,     DATA...]
       Per SX126x datasheet ch.13.4 (ReadRegister) and Python rssi_scanner
       reference. Chip MUST be ready (BUSY=low) BEFORE CS is asserted —
       see pre-CS busy poll below. */
    bool is_read = (data_in != NULL && data_len > 0);
    uint16_t status_offset = cmd_len;
    uint16_t payload_offset = is_read ? cmd_len + 1 : cmd_len;
    uint16_t total_len = is_read ? cmd_len + 1 + data_len : cmd_len + data_len;
    if (total_len > SX126X_SPI_MAX_XFER) {
        s_spi_xfer_busy = false;
        return -MP_EIO;
    }

    /* Static workspace — sx126x_spi_xfer is not re-entrant (one chip,
       serialized commands). DTC sees a stable BSS address. */
    static uint8_t s_tx_buf[SX126X_SPI_MAX_XFER];
    static uint8_t s_rx_buf[SX126X_SPI_MAX_XFER];

    /* Assemble TX buffer in one shot. */
    memcpy(s_tx_buf, cmd, cmd_len);
    if (data_out != NULL) {
        memcpy(s_tx_buf + cmd_len, data_out, data_len);
    } else if (is_read) {
        memset(s_tx_buf + cmd_len, SX126X_CMD_NOP, 1 + data_len);
    }

    /* AD5.5: pre-CS BUSY wait-on-flag. Replaces the AD5.4 poll-spin with
       a yielding spin on SPI_CMPL_BUSY_LOW (set by busy_icu_isr on the
       BUSY-pin falling edge). Architect §8 must-preserve invariants:
       (a) opcode latch is FIRST write inside the s_spi_xfer_busy-gated
           section; (b) busy_wait_count + BUSY_TIMING wrap the wait shape-
           identically; (c) timeout branch keeps busy_timeout_count +
           busy_timeout_opcode co-located, the #ifndef LORAWAN_OBSERVATION_
           DISABLE _busy_dt block, and clears s_spi_xfer_busy before
           returning -MP_ETIMEDOUT; (d) s_rx_window_active = 1 for opcode
           0x82 (SetRx) preserved verbatim. */
    {
        /* (a) Opcode latch — single-writer here (s_spi_xfer_busy == true). */
        STATS_SET_OPCODE(busy_last_opcode, s_tx_buf[0]);
        /* (d) SetRx (0x82) arms an RX window — see Phase 1 Step 10/10.5. */
        if (s_tx_buf[0] == 0x82u) {
            s_rx_window_active = 1u;
        }

        /* R4 dead-letter: clear stale SPI_CMPL_BUSY_LOW AND SPI_CMPL_TIMEOUT
           bits under the same single-writer protection. The AD5.3 DTC yield
           loop sets SPI_CMPL_TIMEOUT on its own timeout — that diagnostic
           bit must not bleed into this call's BUSY-wait observation. */
        s_spi_completion_flag &= ~(SPI_CMPL_BUSY_LOW | SPI_CMPL_TIMEOUT);

        /* (b) busy_wait_count + BUSY_TIMING wrap.
           P3.5 — per-opcode timeout via sx126x_busy_timeout_ms_for_opcode().
           Resolved once per call before the loop so the comparison cost
           stays at one register fetch. */
        const uint32_t pre_cs_budget_ms = sx126x_busy_timeout_ms_for_opcode(s_tx_buf[0]);
        STATS_INC(busy_wait_count);
        LORAWAN_BUSY_TIMING_BEGIN();
        if (busy_high()) {
            uint32_t start = mp_hal_ticks_ms();
            while (!(s_spi_completion_flag & SPI_CMPL_BUSY_LOW)) {
                /* R2 fallback: BUSY edge bounce / missed IRQ. If BUSY is
                   already low, the ICU may have fired before we cleared
                   the bit, or the rising-falling cycle was shorter than
                   one yield quantum. Self-set the bit and break. */
                if (!busy_high()) {
                    s_spi_completion_flag |= SPI_CMPL_BUSY_LOW;
                    break;
                }
                if ((uint32_t)(mp_hal_ticks_ms() - start) > pre_cs_budget_ms) {
                    /* (c) Timeout branch — co-located stats + dt + clear guard. */
                    STATS_INC(busy_timeout_count);
                    STATS_SET_OPCODE(busy_timeout_opcode, s_tx_buf[0]);
#ifndef LORAWAN_OBSERVATION_DISABLE
                    uint32_t _busy_dt = mp_hal_ticks_us() - _busy_t0;
                    STATS_STORE(busy_wait_last_us, _busy_dt);
                    STATS_UPDATE_MAX(busy_wait_max_us, _busy_dt);
                    STATS_UPDATE_MAX(spi_stage_pre_busy_max_us, _busy_dt);  /* AD5.7 */
#endif
                    s_spi_completion_flag |= SPI_CMPL_TIMEOUT;
                    s_spi_xfer_busy = false;
                    return -MP_ETIMEDOUT;
                }
                mp_handle_pending(true);
            }
        }
        LORAWAN_BUSY_TIMING_END(busy_wait_last_us, busy_wait_max_us);
#ifndef LORAWAN_OBSERVATION_DISABLE
        /* AD5.7 — pre-CS stage delta. Same _busy_t0 reuse pattern as
           SX126xWaitOnBusy. Inside the BUSY-poll outer { ... } block. */
        {
            uint32_t _stage_dt = mp_hal_ticks_us() - _busy_t0;
            STATS_UPDATE_MAX(spi_stage_pre_busy_max_us, _stage_dt);
        }
#endif
    }

    cs_low();

    /* AD5.3: yielding DTC continuation. The clear of SPI_CMPL_DTC_DONE is
       single-writer here (s_spi_xfer_busy == true gates re-entry); the bit
       set by sx126x_dtc_done_cb is the only ISR-side write. */
    s_spi_completion_flag &= ~SPI_CMPL_DTC_DONE;
    uint32_t _dtc_start_ms = mp_hal_ticks_ms();
#ifndef LORAWAN_OBSERVATION_DISABLE
    /* AD5.7 — DTC stage start. Captured under the s_spi_xfer_busy single-
       writer invariant; mp_hal_ticks_us() returns a free-running 32-bit µs
       counter so the subtract is wrap-safe up to ~71 min, well past
       SX126X_BUSY_TIMEOUT_MS. */
    uint32_t _dtc_start_us = mp_hal_ticks_us();
#endif
    (void)ra_sci_spi_submit(s_state.spi_id, s_tx_buf, s_rx_buf, total_len,
                            sx126x_dtc_done_cb, NULL);
    while (!(s_spi_completion_flag & SPI_CMPL_DTC_DONE)) {
        if ((uint32_t)(mp_hal_ticks_ms() - _dtc_start_ms) > SX126X_BUSY_TIMEOUT_MS) {
            s_spi_completion_flag |= SPI_CMPL_TIMEOUT;
            break;
        }
        mp_handle_pending(true);
    }
#ifndef LORAWAN_OBSERVATION_DISABLE
    /* AD5.7 — DTC stage delta. Both success and timeout paths produce a
       valid wall-time measurement; the timeout path is capped at
       SX126X_BUSY_TIMEOUT_MS by the loop guard. */
    {
        uint32_t _dtc_dt = mp_hal_ticks_us() - _dtc_start_us;
        STATS_UPDATE_MAX(spi_stage_dtc_max_us, _dtc_dt);
    }
#endif

    cs_high();

    /* Read RX buffer + validate status BEFORE WaitOnBusy. SX126xWaitOnBusy
       polls with mp_handle_pending(true) which yields to the MicroPython
       scheduler — any deferred LoRaMac timer callback then runs and may
       call sx126x_spi_xfer recursively, corrupting static s_rx_buf /
       s_tx_buf before we extract our values. Locking the read-out window
       to the pre-yield phase makes the buffers safe across nested calls. */
    int status_code = 0;
    if (data_len > 0 || is_read) {
        uint8_t sb = s_rx_buf[status_offset];
        if (data_out != NULL) {
            /* Write path: validate against documented status mask
               (sx126x.py:1370-1380). */
            uint8_t mask = sb & 0x0E;
            if (mask == SX126X_STATUS_CMD_TIMEOUT ||
                mask == SX126X_STATUS_CMD_INVALID ||
                mask == SX126X_STATUS_CMD_FAILED ||
                sb == 0x00 || sb == 0xFF) {
                status_code = (sb == 0x00 || sb == 0xFF)
                    ? SX126X_STATUS_SPI_FAILED : (int)mask;
            }
        } else if (is_read) {
            /* Read path: only the clearly-stuck-line sentinels — see
               original byte-loop comment block re: false positives on
               valid mode bits like 0x2A. */
            if (sb == 0x00 || sb == 0xFF) {
                status_code = SX126X_STATUS_SPI_FAILED;
            } else {
                /* Copy received payload (after the status-drain byte). */
                memcpy(data_in, s_rx_buf + payload_offset, data_len);
            }
        }
    }

    if (wait_for_busy) {
        mp_hal_delay_us(1);
        SX126xWaitOnBusy();
    }

    STATS_INC(spi_xfer_count);
    STATS_UPDATE_MAX(spi_max_len, total_len);
    stats_spi_bytes_add(total_len);
    if (total_len == 1) {
        STATS_INC(spi_one_byte_count);
    }

    s_spi_xfer_busy = false;
    return (status_code != 0) ? -MP_EIO : 0;
}

// ---- Public glue API: error-returning variants --------------------------

/* P3.0 — wake-if-sleeping gate at every public entrypoint.
   Vendor SX126xCheckDeviceReady() checks SX126xGetOperatingMode() and calls
   SX126xWakeup() when the chip is in MODE_SLEEP / MODE_RX_DC / MODE_COLD_SLEEP,
   then waits BUSY-low. Without this call the pre-CS BUSY poll inside
   sx126x_spi_xfer hits a dead-chip BUSY=HIGH and times out after 5 s.
   The 2026-05-06 post-CS skip-wait for SetSleep is the matching half-fix —
   together they bracket the sleep transition correctly. */

int SX126xWriteCommand_e(uint8_t opcode, const uint8_t *buffer, uint16_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[1] = { opcode };
    /* SetSleep (0x84) puts the chip in a state where BUSY is held HIGH
       until NSS goes low to wake it. Polling BUSY-low post-NSS-up will
       always time out (SX126X_BUSY_TIMEOUT_MS = 5000 ms) and stall the
       LoRaMac process loop right after every TX-done — RX1 / RX2 timer
       set-up arrives 5 s late and falls into the JoinAcceptDelay
       fail-safe path with rxWin*TimeVal = 0. Suppress the post-busy
       poll for SET_SLEEP only; every other opcode keeps it. */
    bool wait_busy = (opcode != SX126X_CMD_SET_SLEEP);
    return sx126x_spi_xfer(cmd, 1, buffer, NULL, size, wait_busy);
}

int SX126xReadCommand_e(uint8_t opcode, uint8_t *buffer, uint16_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[1] = { opcode };
    return sx126x_spi_xfer(cmd, 1, NULL, buffer, size, /*wait_busy=*/true);
}

int SX126xWriteRegisters_e(uint16_t address, const uint8_t *buffer, uint16_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[3] = {
        SX126X_CMD_WRITE_REGISTER,
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF),
    };
    return sx126x_spi_xfer(cmd, 3, buffer, NULL, size, /*wait_busy=*/true);
}

int SX126xReadRegisters_e(uint16_t address, uint8_t *buffer, uint16_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[3] = {
        SX126X_CMD_READ_REGISTER,
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF),
    };
    return sx126x_spi_xfer(cmd, 3, NULL, buffer, size, /*wait_busy=*/true);
}

int SX126xWriteBuffer_e(uint8_t offset, const uint8_t *buffer, uint8_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[2] = { SX126X_CMD_WRITE_BUFFER, offset };
    return sx126x_spi_xfer(cmd, 2, buffer, NULL, size, /*wait_busy=*/true);
}

int SX126xReadBuffer_e(uint8_t offset, uint8_t *buffer, uint8_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[2] = { SX126X_CMD_READ_BUFFER, offset };
    return sx126x_spi_xfer(cmd, 2, NULL, buffer, size, /*wait_busy=*/true);
}

// ---- LoRaMac-node compatible (void) wrappers ----------------------------
// Signatures intentionally match upstream `sx126x-board.h` exactly:
//   * SX126xWriteCommand / SX126xReadCommand take RadioCommands_t opcode
//     (=uint8_t-sized enum from radio/sx126x.h).
//   * Buffers are non-const pointers (we don't modify them, but C
//     prototypes must match definition signatures).

void SX126xWriteCommand(RadioCommands_t opcode, uint8_t *buffer, uint16_t size) {
    (void)SX126xWriteCommand_e((uint8_t)opcode, buffer, size);
}

uint8_t SX126xReadCommand(RadioCommands_t opcode, uint8_t *buffer, uint16_t size) {
    return (SX126xReadCommand_e((uint8_t)opcode, buffer, size) == 0) ? 0 : 0xFF;
}

void SX126xWriteRegisters(uint16_t address, uint8_t *buffer, uint16_t size) {
    (void)SX126xWriteRegisters_e(address, buffer, size);
}

void SX126xReadRegisters(uint16_t address, uint8_t *buffer, uint16_t size) {
    (void)SX126xReadRegisters_e(address, buffer, size);
}

void SX126xWriteRegister(uint16_t address, uint8_t value) {
    SX126xWriteRegisters(address, &value, 1);
}

uint8_t SX126xReadRegister(uint16_t address) {
    uint8_t v = 0;
    SX126xReadRegisters(address, &v, 1);
    return v;
}

void SX126xWriteBuffer(uint8_t offset, uint8_t *buffer, uint8_t size) {
    (void)SX126xWriteBuffer_e(offset, buffer, size);
}

void SX126xReadBuffer(uint8_t offset, uint8_t *buffer, uint8_t size) {
    (void)SX126xReadBuffer_e(offset, buffer, size);
}

void SX126xAntSwOn(void)  { /* RF switch held HIGH at init — NOP */ }
void SX126xAntSwOff(void) { /* board switch is hard-driven — NOP */ }

/* Per-board RF TX power setter. Wio-SX1262 uses the SX1262 high-power
   PA path — we delegate to upstream `SX126xSetTxParams`, which knows
   how to drive the SET_TX_PARAMS opcode with the correct ramp time
   for the chip variant. Power is clamped to the SX1262 valid range
   (-9..+22 dBm) inside SX126xSetTxParams. */
void SX126xSetRfTxPower(int8_t power) {
    SX126xSetTxParams(power, RADIO_RAMP_40_US);
}

/* Device ID query — Wio-SX1262 always reports as SX1262 (variant 2 in
   the upstream enum). The chip itself has no read-back ID register;
   the value is board-knowledge. */
uint8_t SX126xGetDeviceId(void) {
    return SX1262;
}

// ---- DIO1 IRQ wiring (Phase 2 — direct ICU registration) ----------------
//
// Path: DIO1 (P015) rising edge → ICU hard IRQ → dio1_icu_isr() (this
// file) sets a flag and posts mp_sched_schedule_node → MicroPython
// scheduler invokes dio1_sched_dispatch() between bytecodes →
// `s_state.dio1_c_handler()` runs in scheduler context.
//
// The hard IRQ does NOT call the user callback directly. Hard-IRQ context
// cannot allocate, cannot touch GC, cannot run Python. The scheduler-
// context dispatch lifts the boundary cleanly.

static void dio1_sched_dispatch(mp_sched_node_t *node) {
    (void)node;
    if (s_state.dio1_c_handler != NULL) {
        s_state.dio1_c_handler();
    }
}

/* ISR re-entry depth counter — written only by dio1_icu_isr itself. A
   non-zero hard_isr_dio1_reentry_count means a higher-priority IRQ
   pre-empted this ISR while it was running. We still run the dispatch
   on the re-entry path (do NOT drop the IRQ).

   Save-restore over a uint8_t: each level enters with prev = depth,
   stores prev+1, restores to prev on exit. Unlike the bool sentinel,
   an inner level completing does NOT clear the outer level's flag, so
   re-entry counting stays correct under deeper nesting. Same-NVIC-line
   ISRs tail-chain on ARMv8-M, and cross-line same-priority preemption
   is not possible, so plain volatile RMW is race-free here. */
static volatile uint8_t s_dio1_isr_depth;

static void dio1_icu_isr(void *param) {
    (void)param;
    uint8_t prev = s_dio1_isr_depth;
    if (prev > 0) {
        STATS_INC(hard_isr_dio1_reentry_count);
    }
    s_dio1_isr_depth = (uint8_t)(prev + 1);
    STATS_INC(hard_isr_dio1_count);
    LORAWAN_ISR_CYCLES_BEGIN();

    // Runs in hard IRQ context. Defer to scheduler.
    (void)mp_sched_schedule_node(&s_state.dio1_sched_node, dio1_sched_dispatch);

    LORAWAN_ISR_CYCLES_END(hard_isr_dio1_cycles_max);
    s_dio1_isr_depth = prev;
}

void SX126xIoIrqInit(void (*irq_handler)(void)) {
    if (!s_state.initialized) {
        return;
    }
    SX126xIoIrqDeinit();  // idempotent if not previously enabled
    s_state.dio1_c_handler = irq_handler;
    if (irq_handler == NULL) {
        return;
    }
    uint8_t irq_no;
    if (!ra_icu_find_irq_no((uint32_t)s_state.dio1->pin, &irq_no)) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("DIO1 pin %d has no ICU vector"),
            (uint32_t)s_state.dio1->pin);
    }
    s_state.dio1_irq_no = irq_no;
    ra_icu_set_callback(irq_no, dio1_icu_isr, NULL);
    ra_icu_set_pin((uint32_t)s_state.dio1->pin, /*irq_enable=*/true,
                   /*pullup=*/false);
    ra_icu_trigger_pin((uint32_t)s_state.dio1->pin, /*cond=rising*/1);
    ra_icu_enable_pin((uint32_t)s_state.dio1->pin);
    s_state.dio1_irq_active = true;
}

void SX126xIoIrqDeinit(void) {
    if (s_state.dio1_irq_active && s_state.dio1 != NULL) {
        ra_icu_disable_pin((uint32_t)s_state.dio1->pin);
        ra_icu_set_callback(s_state.dio1_irq_no, NULL, NULL);
    }
    s_state.dio1_c_handler = NULL;
    s_state.dio1_irq_active = false;
    s_state.dio1_irq_no = 0xFF;
}

/* ---- Stubs for upstream sx126x-board.h surface ----------------------
 *
 * These implement the additional functions that imported `radio/sx126x.c`
 * expects, beyond the core SPI/GPIO/IRQ glue. Most are NOPs because the
 * Wio-SX1262 daughterboard has its TCXO and RF switch hard-wired (no
 * MCU-controlled enable lines), and PA / clock select are deferred to
 * Phase 5+.
 */

void SX126xIoInit(void) {
    /* Pin / SPI init is performed by sx126x_board_init() called from
       lorawan.Mac() construction. NOP here keeps upstream SX126xInit()
       working without re-doing initialisation. */
}

void SX126xIoDeInit(void) { /* NOP */ }

/* Wio-SX1262 has its TCXO fed from the SX1262 DIO3 pin. The chip
   must be told to drive DIO3 as TCXO control with the right voltage
   and stabilization timeout, otherwise its internal calibrations
   fail (XOSC_START_ERR, PLL_CALIB_ERR) and SX126xGetDeviceErrors
   returns non-zero, which makes RadioInit() report RADIO_FAIL.
   Wio-SX1262 datasheet: TCXO is 1.8 V, ~5 ms startup. */
void SX126xIoTcxoInit(void) {
    /* SetRegulatorMode(LDO) MUST come before SetDio3AsTcxoCtrl.
       Python rssi_scanner reference (which works on this hardware)
       sets regulator mode first, then TCXO. Renesas's RadioInit calls
       SetRegulatorMode AFTER SX126xInit (which contains this TCXO
       setup) — but on Wio-SX1262, doing it after means the chip
       sees TCXO config during a regulator transition which can leave
       the XOSC startup logic misconfigured. Setting LDO explicitly
       BEFORE TCXO config matches the proven Python flow. */
    SX126xSetRegulatorMode(USE_LDO);
    /* RP_TCXO_STAB_TIME (320) is in 15.625 µs units → 5 ms. */
    SX126xSetDio3AsTcxoCtrl(TCXO_CTRL_1_8V, RP_TCXO_STAB_TIME);
    /* TCXO needs ~5ms to stabilize after enable; rssi_scanner.py reference
       sleeps 10ms here. Use mp_hal_delay_ms — safe in scheduler context. */
    mp_hal_delay_ms(10);
    /* After enabling TCXO, the chip retriggers its calibrations. Pass
       a CalibrationParams_t with all blocks selected (0x7F = 7 lower
       bits set) and wait for BUSY to drop. */
    CalibrationParams_t calib;
    calib.Value = 0x7F;
    SX126xCalibrate(calib);
    mp_hal_delay_ms(5);
    SX126xWaitOnBusy();
    /* Clear any TCXO/calibration error flags AFTER Calibrate completes —
       matches the Python rssi_scanner reference flow which is empirically
       known to work on this hardware. */
    SX126xClearDeviceErrors();
}
void SX126xIoRfSwitchInit(void) { /* RF switch held HIGH at init — NOP */ }
void SX126xIoDbgInit(void) { /* no debug pins */ }

bool SX126xCheckRfFrequency(uint32_t frequency) {
    /* Phase 4 v1 — accept any frequency. Region layer enforces channel
       compliance (EU868 only at the moment). */
    (void)frequency;
    return true;
}

uint32_t SX126xGetBoardTcxoWakeupTime(void) {
    /* Wio-SX1262 typical TCXO startup: 5 ms. */
    return 5;
}

uint32_t SX126xGetDio1PinState(void) {
    if (!s_state.initialized) {
        return 0;
    }
    return mp_hal_pin_read((mp_hal_pin_obj_t)s_state.dio1) != 0 ? 1u : 0u;
}

/* PA / clock select — board-level config maintained as static state.
   Defaults match Wio-SX1262: high-power PA, TCXO clock. */
static uint8_t s_pa_select    = 0x00;  /* SX1262 PA */
static uint8_t s_clock_select = 0x01;  /* TCXO */

void SX126xBoardConfigInit(void) {
    /* Defaults are already set; placeholder for future NVM read. */
}

uint8_t SX126xGetPaSelect(void)               { return s_pa_select; }
void    SX126xSetPaSelect(uint8_t paType)     { s_pa_select = paType; }
uint8_t SX126xGetClockSelect(void)            { return s_clock_select; }
void    SX126xSetClockSelect(uint8_t clkType) { s_clock_select = clkType; }

// ---- BUSY IRQ wiring (Phase 2 / item 3) ---------------------------------
//
// BUSY pin falling-edge ICU IRQ. When BUSY transitions high→low, the
// ISR sets SPI_CMPL_BUSY_LOW; both pre-CS (sx126x_spi_xfer, AD5.5) and
// post-CS (SX126xWaitOnBusy, AD5.6) waiters yield on that bit.

static void busy_icu_isr(void *param) {
    (void)param;
    LORAWAN_ISR_CYCLES_BEGIN();
    s_spi_completion_flag |= SPI_CMPL_BUSY_LOW;
    STATS_INC(hard_isr_busy_low_count);
    LORAWAN_ISR_CYCLES_END(hard_isr_busy_low_cycles_max);
}

bool SX126xBusyIrqEnable(void) {
    if (!s_state.initialized || s_state.busy_irq_active) {
        return s_state.busy_irq_active;
    }
    uint8_t irq_no;
    if (!ra_icu_find_irq_no((uint32_t)s_state.busy->pin, &irq_no)) {
        return false;  // no vector — caller falls back to polling
    }
    s_state.busy_irq_no = irq_no;
    ra_icu_set_callback(irq_no, busy_icu_isr, NULL);
    ra_icu_set_pin((uint32_t)s_state.busy->pin, /*irq_enable=*/true, false);
    ra_icu_trigger_pin((uint32_t)s_state.busy->pin, /*cond=falling*/0);
    ra_icu_enable_pin((uint32_t)s_state.busy->pin);
    s_state.busy_irq_active = true;
    return true;
}

void SX126xBusyIrqDisable(void) {
    if (!s_state.busy_irq_active) {
        return;
    }
    ra_icu_disable_pin((uint32_t)s_state.busy->pin);
    ra_icu_set_callback(s_state.busy_irq_no, NULL, NULL);
    s_state.busy_irq_active = false;
    s_state.busy_irq_no = 0xFF;
}

// ---- Runtime inter-byte tuning (item 4) ---------------------------------

void sx126x_board_set_interbyte_us(uint8_t us) {
    s_state.interbyte_us = us;
}

uint8_t sx126x_board_get_interbyte_us(void) {
    return s_state.interbyte_us;
}

/* Debug — return s_state.spi_id so Python can verify the C glue is
   actually configured for SPI(3). */
uint8_t sx126x_board_get_spi_id(void) {
    return s_state.spi_id;
}

/* Debug — perform a single 1-byte exchange via ra_sci_spi_transfer at
   the same level as the Python SPI.write_readinto path. Bypasses the
   sx126x_spi_xfer wrapper entirely so we can isolate whether the
   ra_sci_spi_transfer call itself is producing different results when
   invoked from this C module vs from the MicroPython binding. */
extern void ra_sci_spi_transfer(uint32_t ch, const uint8_t *src,
    uint8_t *dst, uint32_t count);

#ifdef LORAWAN_DEBUG_SPI_UNSAFE
uint8_t sx126x_board_debug_xchg(uint8_t tx_byte) {
    static volatile uint8_t dbg_tx;
    static volatile uint8_t dbg_rx;
    dbg_tx = tx_byte;
    dbg_rx = 0;
    ra_sci_spi_transfer(s_state.spi_id,
        (const uint8_t *)&dbg_tx, (uint8_t *)&dbg_rx, 1);
    return dbg_rx;
}
#else
/* Production stub: byte-by-byte SPI debug helper is unavailable. The
   bulk-only-SPI invariant (Step 15 grep gate) requires zero
   ra_sci_spi_transfer(..., 1) call sites in the default build. */
uint8_t sx126x_board_debug_xchg(uint8_t tx_byte) {
    (void)tx_byte;
    return 0xFF;
}
#endif

// ---- Phase 1 testing helpers --------------------------------------------

#ifdef LORAWAN_DEBUG_SPI_UNSAFE
uint8_t sx126x_phase1_get_status(void) {
    /* SX126x GET_STATUS protocol differs from other read commands:
       the chip emits the status byte in the FIRST NOP after the opcode,
       not as a separate "status + data" pair. The standard
       SX126xReadCommand_e helper would discard byte 1 (treating it as
       a status indicator) and return byte 2 (which is undefined for
       GET_STATUS). We therefore implement this single command inline.
     */
    if (!s_state.initialized) {
        return 0xFF;
    }
    static volatile uint8_t dbg_tx;
    static volatile uint8_t dbg_rx;
    cs_low();
    /* Pre-transaction BUSY poll. */
    {
        /* Debug helper — bypasses s_spi_xfer_busy guard, so single-writer
           invariant does not hold here. Counters only — no opcode latch. */
        STATS_INC(busy_wait_count);
        LORAWAN_BUSY_TIMING_BEGIN();
        uint32_t start = mp_hal_ticks_ms();
        while (busy_high()) {
            if ((uint32_t)(mp_hal_ticks_ms() - start) > SX126X_BUSY_TIMEOUT_MS) {
                STATS_INC(busy_timeout_count);
#ifndef LORAWAN_OBSERVATION_DISABLE
                uint32_t _busy_dt = mp_hal_ticks_us() - _busy_t0;
                STATS_STORE(busy_wait_last_us, _busy_dt);
                STATS_UPDATE_MAX(busy_wait_max_us, _busy_dt);
#endif
                cs_high();
                return 0xFF;
            }
            mp_handle_pending(true);
        }
        LORAWAN_BUSY_TIMING_END(busy_wait_last_us, busy_wait_max_us);
    }
    /* Byte 1: opcode 0xC0 — chip ignores rx, prepares status. */
    dbg_tx = SX126X_CMD_GET_STATUS;
    ra_sci_spi_transfer(s_state.spi_id,
        (const uint8_t *)&dbg_tx, (uint8_t *)&dbg_rx, 1);
    /* Byte 2: NOP — chip emits status byte on MISO. */
    dbg_tx = SX126X_CMD_NOP;
    ra_sci_spi_transfer(s_state.spi_id,
        (const uint8_t *)&dbg_tx, (uint8_t *)&dbg_rx, 1);
    uint8_t status = dbg_rx;
    cs_high();
    return status;
}
#else
/* Production stub: same rationale as sx126x_board_debug_xchg above.
   No SPI traffic, no BUSY wait, no s_spi_xfer_busy mutation. */
uint8_t sx126x_phase1_get_status(void) {
    return 0xFF;
}
#endif

uint8_t sx126x_phase1_read_register(uint16_t addr) {
    return SX126xReadRegister(addr);
}

void sx126x_phase1_write_register(uint16_t addr, uint8_t value) {
    SX126xWriteRegister(addr, value);
}

bool sx126x_phase1_busy_high(void) {
    if (!s_state.initialized) {
        return false;
    }
    return busy_high();
}

#endif // MICROPY_HW_LORA_STACK_RENESAS
