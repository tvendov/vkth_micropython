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

#define SX126X_STANDBY_RC               (0x00)

#define SX126X_STATUS_CMD_TIMEOUT       (0x06)
#define SX126X_STATUS_CMD_INVALID       (0x08)
#define SX126X_STATUS_CMD_FAILED        (0x0A)
#define SX126X_STATUS_SPI_FAILED        (0xFF)

#define SX126X_BUSY_TIMEOUT_MS          (5000)

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

    // Optional BUSY pin falling-edge IRQ (Phase 2).
    bool busy_irq_active;
    uint8_t busy_irq_no;
    volatile bool busy_low_seen;

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
    s_state.busy_low_seen = false;
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
}

void sx126x_board_deinit(void) {
    if (!s_state.initialized) {
        return;
    }
    SX126xIoIrqDeinit();
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

void SX126xWaitOnBusy(void) {
    if (!s_state.initialized) {
        return;
    }
    // Fast path: BUSY-pin ICU IRQ already saw the falling edge while we
    // were doing other work. No poll needed.
    if (s_state.busy_irq_active && s_state.busy_low_seen) {
        s_state.busy_low_seen = false;
        return;
    }
    uint32_t start = mp_hal_ticks_ms();
    while (busy_high()) {
        if ((uint32_t)(mp_hal_ticks_ms() - start) > SX126X_BUSY_TIMEOUT_MS) {
            return;  // timeout — caller may decide to raise
        }
        mp_handle_pending(true);
    }
    // Clear any stale flag now that BUSY is observed low.
    s_state.busy_low_seen = false;
}

void SX126xWakeup(void) {
    // sx126x.py wakes the chip by toggling NSS — the chip's clock driver
    // sees the falling edge and exits sleep. A single GET_STATUS opcode
    // wrapped in NSS low/high is sufficient; the BUSY-poll inside the
    // exchange handles the wakeup latency.
    if (!s_state.initialized) {
        return;
    }
    cs_low();
    mp_hal_delay_us(2);
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

static int sx126x_spi_xfer(const uint8_t *cmd, uint16_t cmd_len,
    const uint8_t *data_out, uint8_t *data_in, uint16_t data_len,
    bool wait_for_busy) {

    if (!s_state.initialized) {
        return -MP_EIO;
    }

    /* Re-entrancy refused. Caller (deferred DIO1 dispatch) should bail and
       leave its work for the next scheduler tick — by then outer SPI is done. */
    if (s_spi_xfer_busy) {
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

    /* Pre-CS BUSY poll. Includes mp_handle_pending so the BUSY-pin IRQ
       that signals chip-ready can actually run (it's scheduler-deferred,
       not a tight HW interrupt). Without yield the BUSY bit can stay
       high indefinitely from this CPU's perspective even though the
       chip is actually ready. */
    {
        uint32_t start = mp_hal_ticks_ms();
        while (busy_high()) {
            if ((uint32_t)(mp_hal_ticks_ms() - start) > SX126X_BUSY_TIMEOUT_MS) {
                s_spi_xfer_busy = false;
                return -MP_ETIMEDOUT;
            }
            mp_handle_pending(true);
        }
    }

    cs_low();

    /* Single DTC-driven block transfer — full-duplex, NSS held low. */
    ra_sci_spi_transfer(s_state.spi_id, s_tx_buf, s_rx_buf, total_len);

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

    s_spi_xfer_busy = false;
    return (status_code != 0) ? -MP_EIO : 0;
}

// ---- Public glue API: error-returning variants --------------------------

int SX126xWriteCommand_e(uint8_t opcode, const uint8_t *buffer, uint16_t size) {
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
    uint8_t cmd[1] = { opcode };
    return sx126x_spi_xfer(cmd, 1, NULL, buffer, size, /*wait_busy=*/true);
}

int SX126xWriteRegisters_e(uint16_t address, const uint8_t *buffer, uint16_t size) {
    uint8_t cmd[3] = {
        SX126X_CMD_WRITE_REGISTER,
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF),
    };
    return sx126x_spi_xfer(cmd, 3, buffer, NULL, size, /*wait_busy=*/true);
}

int SX126xReadRegisters_e(uint16_t address, uint8_t *buffer, uint16_t size) {
    uint8_t cmd[3] = {
        SX126X_CMD_READ_REGISTER,
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF),
    };
    return sx126x_spi_xfer(cmd, 3, NULL, buffer, size, /*wait_busy=*/true);
}

int SX126xWriteBuffer_e(uint8_t offset, const uint8_t *buffer, uint8_t size) {
    uint8_t cmd[2] = { SX126X_CMD_WRITE_BUFFER, offset };
    return sx126x_spi_xfer(cmd, 2, buffer, NULL, size, /*wait_busy=*/true);
}

int SX126xReadBuffer_e(uint8_t offset, uint8_t *buffer, uint8_t size) {
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

static void dio1_icu_isr(void *param) {
    (void)param;
    // Runs in hard IRQ context. Defer to scheduler.
    (void)mp_sched_schedule_node(&s_state.dio1_sched_node, dio1_sched_dispatch);
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
// Optional fast-path: enable BUSY pin falling-edge ICU IRQ. When BUSY
// transitions high→low, the ISR sets `busy_low_seen`. SX126xWaitOnBusy
// can use that flag to short-circuit. Falls back to polling if the pin
// has no ICU vector.

static void busy_icu_isr(void *param) {
    (void)param;
    s_state.busy_low_seen = true;
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
    s_state.busy_low_seen = false;
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
    s_state.busy_low_seen = false;
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

uint8_t sx126x_board_debug_xchg(uint8_t tx_byte) {
    static volatile uint8_t dbg_tx;
    static volatile uint8_t dbg_rx;
    dbg_tx = tx_byte;
    dbg_rx = 0;
    ra_sci_spi_transfer(s_state.spi_id,
        (const uint8_t *)&dbg_tx, (uint8_t *)&dbg_rx, 1);
    return dbg_rx;
}

// ---- Phase 1 testing helpers --------------------------------------------

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
        uint32_t start = mp_hal_ticks_ms();
        while (busy_high()) {
            if ((uint32_t)(mp_hal_ticks_ms() - start) > SX126X_BUSY_TIMEOUT_MS) {
                cs_high();
                return 0xFF;
            }
            mp_handle_pending(true);
        }
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
