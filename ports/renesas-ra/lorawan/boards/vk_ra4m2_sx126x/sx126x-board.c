/*
 * lorawan/boards/vk_ra4m2_sx126x/sx126x-board.c
 *
 * SX126x active board layer for VK_RA4M2.
 *
 * Reuses the pin map, GPIO semantics, reset sequence, and IRQ ownership model
 * proven by the Python driver at
 *   ports/renesas-ra/boards/VK_RA4M2/examples/LoRa/_upstream/sx126x.py
 *
 * Pin map is identical (see sx126x-board.h for the full table). The Python
 * driver was validated against TTN OTAA + uplink
 * on VK_RA4M2 (commit e11f4939c вЂ” switch SX1262 SPI from SoftSPI to HW
 * SPI(3) on SCI9), so this port keeps the validated hardware contract while
 * using the MicroPython-owned SPI object for radio transfers.
 *
 * Notable design constraints (carried over from the Python driver):
 *
 * 1. Byte-by-byte SPI shifts. Production SX126x traffic follows the Renesas
 *    board sources: opcode/address/data bytes are shifted one at a time,
 *    with CS held low for the transaction.
 *
 * 2. NSS = manual GPIO toggle, NOT hardware SSL. The MicroPython SCI-SPI
 *    backend on VK_RA4M2 does not drive a chip-select line; the Python
 *    driver toggles P206 directly. We do the same.
 *
 * 3. BUSY polling. Before every SPI transaction we wait BUSY=low without
 *    timeout, matching the Renesas board layer without scheduler dispatch.
 *    After transactions that change radio state (Reset, SetTx, SetRx, etc.)
 *    we poll BUSY a second time after NSS deassert.
 *
 * 4. Reset sequence: LOW for 20 ms, bracketed by 10 ms settle delays,
 *    matching the Renesas SX126x board sources.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "extmod/modmachine.h"

#include "sx126x-board.h"
#include "lorawan_stats.h"
#include "lorawan_pump.h"

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

// Pull in the renesas-ra-private headers AFTER the public active board layer header so
// the public surface (which only forwards `void *` pin handles) does not
// transitively expose pin.h to other compilation units.
#include "pin.h"
#include "extint.h"
#include "ra/ra_gpio.h"

/* PWPR helpers вЂ” direct register pokes (ra_gpio.c marks pwpr_unprotect /
   pwpr_protect as `static`, so we cannot link to them and re-create the
   sequence locally). */
#define _PWPR_LOC (*(volatile uint8_t *)(R_PFS_BASE + 0x503))
static inline void pfs_pwpr_unprotect(void) { _PWPR_LOC = 0x00; _PWPR_LOC = 0x40; }
static inline void pfs_pwpr_protect(void)   { _PWPR_LOC = 0x80; }

// Pulls in RP_TCXO_STAB_TIME and other board-level constants used by
// TCXO setup (SX126xIoTcxoInit) and the LoRaMac adapter layer.
#include "board.h"

#include "sx126x_board_cfg.h"

bool sx126x_board_is_initialized(void);
int SX126xWriteCommand_e(uint8_t opcode, const uint8_t *buffer, uint16_t size);
int SX126xReadCommand_e(uint8_t opcode, uint8_t *buffer, uint16_t size);
int SX126xWriteRegisters_e(uint16_t address, const uint8_t *buffer, uint16_t size);
int SX126xReadRegisters_e(uint16_t address, uint8_t *buffer, uint16_t size);
int SX126xWriteBuffer_e(uint8_t offset, const uint8_t *buffer, uint8_t size);
int SX126xReadBuffer_e(uint8_t offset, uint8_t *buffer, uint8_t size);
void SX126xIoIrqDeinit(void);
bool sx126x_board_spi_busy(void);
static void sx126x_board_dispatch_dio1_irq(void);

// ---- SX126x opcodes / status bits ---------------------------------------

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
/* BUSY is deliberately Renesas/Semtech 1:1 here: poll until the radio
   releases BUSY, with no wrapper-side timeout or scheduler dispatch. */

// Inter-byte settle time default. 0 = rely on the MicroPython SPI backend
// transfer overhead alone.
#ifndef LORAWAN_SX126X_INTERBYTE_US_DEFAULT
#define LORAWAN_SX126X_INTERBYTE_US_DEFAULT (0)
#endif

// ---- Driver state --------------------------------------------------------

typedef struct {
    bool initialized;
    uint8_t spi_id;                       // SPI(3) on VK_RA4M2 в†’ SCI9
    mp_obj_t spi_obj;
    const machine_pin_obj_t *cs;
    const machine_pin_obj_t *rst;
    const machine_pin_obj_t *busy;
    const machine_pin_obj_t *dio1;
    const machine_pin_obj_t *rf_sw;       // may be NULL

    // DIO1 handler stored for public Pin.irq/extint dispatch. Signature matches LoRaMac-node
    // `typedef void (DioIrqHandler)(void)` вЂ” handlers close over the
    // singleton SX126x global instead of taking a context argument.
    void (*dio1_c_handler)(void);
    bool  dio1_irq_active;

    // Optional compile-time inter-byte delay.
    uint8_t interbyte_us;
} sx126x_state_t;

static sx126x_state_t s_state;

/* Reentrancy guard for SX126xWaitOnBusy(). BUSY polling follows the
   Renesas board shape: no MicroPython scheduler dispatch while waiting.
   This flag still exposes the wait window to the guarded pump so a pending
   DIO1/timer drain can defer instead of entering Radio.IrqProcess while the
   radio is not ready. Single-writer (SX126xWaitOnBusy in foreground);
   ISR-side reads happen via sx126x_board_spi_busy(). */
volatile bool sx126x_in_wait_busy = false;

/* Re-entrancy guard. Set true while the board layer holds CS or polls BUSY
   for a radio SPI operation. */
static volatile bool s_spi_xfer_busy = false;

/* Canonical board-layer state lives here so vk_ra4m2_sx126x has a single
   SX126x source file. */
static uint8_t devicePowerSelect = DEFAULT_POWER_SELECT;
static uint8_t deviceClockSelect = DEFAULT_CLOCK_SELECT;

// Internal: assert/deassert NSS, abstracted so the SPI helper stays clean.
static inline void cs_low(void)  { mp_hal_pin_low((mp_hal_pin_obj_t)s_state.cs); }
static inline void cs_high(void) { mp_hal_pin_high((mp_hal_pin_obj_t)s_state.cs); }

static inline void sx126x_spi_transfer_one(uint8_t tx, uint8_t *rx) {
    const mp_machine_spi_p_t *spi_p =
        MP_OBJ_TYPE_GET_SLOT(&machine_spi_type, protocol);
    spi_p->transfer(MP_OBJ_TO_PTR(s_state.spi_obj), 1, &tx, rx);
}

void BoardRadioDio1Callback(external_irq_callback_args_t *p_args);

static mp_obj_t sx126x_dio1_pin_irq_callback(mp_obj_t pin_in) {
    (void)pin_in;
    BoardRadioDio1Callback(NULL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sx126x_dio1_pin_irq_callback_obj,
    sx126x_dio1_pin_irq_callback);

// Internal: BUSY pin read (1 = busy, 0 = ready).
static inline bool busy_high(void) {
    return mp_hal_pin_read((mp_hal_pin_obj_t)s_state.busy) != 0;
}

// ---- Lifecycle -----------------------------------------------------------

bool sx126x_board_is_initialized(void) {
    return s_state.initialized;
}

void SX126xBoardConfigInit(void) {
    SX126xSetPaSelect(DEFAULT_POWER_SELECT);
    SX126xSetClockSelect(DEFAULT_CLOCK_SELECT);
}

void SX126xIoInit(void) {
    /* Pins are configured by sx126x_board_init(), which receives the active
       Python-owned pin objects. Keep this canonical hook idempotent. */
    SX126xAntSwOn();
}

void SX126xIoIrqInit(DioIrqHandler dioIrq) {
    s_state.dio1_c_handler = dioIrq;

    if (s_state.dio1 == NULL) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("DIO1 pin not configured"));
    }

    extint_register_pin(s_state.dio1, MP_HAL_PIN_TRIGGER_RISING,
        true, false, MP_OBJ_FROM_PTR(&sx126x_dio1_pin_irq_callback_obj));

    s_state.dio1_irq_active = true;
}

void BoardRadioDio1Callback(external_irq_callback_args_t *p_args) {
    (void)p_args;
    BoardRadioIrqPreprocess();
    sx126x_board_dispatch_dio1_irq();
    lorawan_pump_request_dio1();
}

void SX126xIoTcxoInit(void) {
    if (SX126xGetClockSelect() == RADIO_CLOCK_TCXO_SEL) {
        CalibrationParams_t calibParam;
        SX126xSetDio3AsTcxoCtrl(RP_TCXO_CTRL_VOLTAGE, RP_TCXO_STAB_TIME);
        DelayMs(5);
        SX126xWaitOnBusy();
        memset(&calibParam, 0, sizeof(calibParam));
        calibParam.Value = 0x7F;
        SX126xCalibrate(calibParam);
        SX126xWaitOnBusy();
        SX126xClearDeviceErrors();
    }
}

void SX126xReset(void) {
    if (s_state.rst == NULL) {
        return;
    }
    DelayMs(10);
    mp_hal_pin_low((mp_hal_pin_obj_t)s_state.rst);
    DelayMs(20);
    mp_hal_pin_high((mp_hal_pin_obj_t)s_state.rst);
    DelayMs(10);
}

void SX126xWaitOnBusy(void) {
    if (!s_state.initialized || !busy_high()) {
        return;
    }

    sx126x_in_wait_busy = true;
    while (busy_high()) {
    }
    sx126x_in_wait_busy = false;
}

void SX126xWakeup(void) {
    if (!s_state.initialized) {
        return;
    }
    if (s_spi_xfer_busy) {
        STATS_INC(spi_nested_reject_count);
        return;
    }
    s_spi_xfer_busy = true;
    CRITICAL_SECTION_BEGIN();
    cs_low();
    uint8_t tx = SX126X_CMD_GET_STATUS;
    uint8_t rx = 0u;
    sx126x_spi_transfer_one(tx, &rx);
    tx = SX126X_CMD_NOP;
    sx126x_spi_transfer_one(tx, &rx);
    cs_high();
    SX126xWaitOnBusy();
    CRITICAL_SECTION_END();
    s_spi_xfer_busy = false;
}

void sx126x_board_init(const sx126x_board_cfg_t *cfg) {
    if (cfg == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("sx126x cfg required"));
    }
    if (cfg->spi_obj == MP_OBJ_NULL || cfg->spi_obj == mp_const_none ||
        cfg->cs_pin == NULL || cfg->rst_pin == NULL ||
        cfg->gpio_busy_pin == NULL || cfg->irq_pin == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT(
            "spi/cs/rst/gpio_busy/irq pins required"));
    }

    s_state.spi_id  = cfg->spi_bus;
    s_state.spi_obj = cfg->spi_obj;
    s_state.cs      = (const machine_pin_obj_t *)cfg->cs_pin;
    s_state.rst     = (const machine_pin_obj_t *)cfg->rst_pin;
    s_state.busy    = (const machine_pin_obj_t *)cfg->gpio_busy_pin;
    s_state.dio1    = (const machine_pin_obj_t *)cfg->irq_pin;
    s_state.rf_sw   = (const machine_pin_obj_t *)cfg->rf_sw_pin;
    s_state.dio1_c_handler = NULL;
    s_state.dio1_irq_active = false;
    s_state.interbyte_us = LORAWAN_SX126X_INTERBYTE_US_DEFAULT;

    // RF switch enable before first radio use (matches lorawan_app.py:1056).
    if (s_state.rf_sw != NULL) {
        mp_hal_pin_output((mp_hal_pin_obj_t)s_state.rf_sw);
        mp_hal_pin_high((mp_hal_pin_obj_t)s_state.rf_sw);
    }

    /* cfg->spi_obj is the Python-owned machine.SPI rooted by mod_lorawan.c
     * (lorawan_spi_obj_root). Reject if caller didn't supply one — the dup
     * sentinel `lorawan_spi_pinned` is gone. */
    if (cfg->spi_obj == mp_const_none || cfg->spi_obj == MP_OBJ_NULL) {
        mp_raise_OSError(MP_EIO);
    }
    (void)cfg->spi_baud_hz;  /* requested via constructor/SPI object; not re-validated here */

    // CS high (idle), output.
    mp_hal_pin_output((mp_hal_pin_obj_t)s_state.cs);
    mp_hal_pin_high((mp_hal_pin_obj_t)s_state.cs);

    // RESET high (idle), output.
    mp_hal_pin_output((mp_hal_pin_obj_t)s_state.rst);
    mp_hal_pin_high((mp_hal_pin_obj_t)s_state.rst);

    // BUSY input (no pull вЂ” Wio-SX1262 drives it actively).
    mp_hal_pin_input((mp_hal_pin_obj_t)s_state.busy);

    // DIO1 input. SX126xIoIrqInit() arms the Python-owned pin through the
    // public Pin.irq/extint path with hard=True and rising edge trigger.
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

    /* BUSY is a status/poll signal in the LoRaWAN authority contract. Do not
       claim or arm an ICU vector here; the SPI path polls P002 and self-marks
       BUSY-low when the pin reads ready. */
}

void sx126x_board_deinit(void) {
    if (!s_state.initialized) {
        return;
    }
    SX126xIoIrqDeinit();
    cs_high();
    s_state.spi_obj = MP_OBJ_NULL;
    s_state.initialized = false;
}

// ---- SPI exchange core: Renesas-compatible byte-by-byte transfer ---------
//
// Returns 0 on success, negative on SPI failure.
// `cmd`     : opcode + optional address bytes.
// `data_out`: payload to write; `data_in`: payload to read.
// `wait_for_busy`: poll BUSY=low after NSS-high.
// `status_out`: optional first status byte returned by SX126x read commands.

static int sx126x_spi_xfer(const uint8_t *cmd, uint16_t cmd_len,
    const uint8_t *data_out, uint8_t *data_in, uint16_t data_len,
    bool wait_for_busy, uint8_t *status_out) {

    if (!s_state.initialized || cmd == NULL || cmd_len == 0u) {
        return -MP_EIO;
    }
    if (s_spi_xfer_busy) {
        STATS_INC(spi_nested_reject_count);
        return -MP_EBUSY;
    }
    s_spi_xfer_busy = true;

    const uint8_t opcode = cmd[0];
    STATS_SET_OPCODE(busy_last_opcode, opcode);

    STATS_INC(busy_wait_count);
    LORAWAN_BUSY_TIMING_BEGIN();
    if (busy_high()) {
        while (busy_high()) {
        }
    }
    LORAWAN_BUSY_TIMING_END(busy_wait_last_us, busy_wait_max_us);
#ifndef LORAWAN_OBSERVATION_DISABLE
    {
        uint32_t _stage_dt = mp_hal_ticks_us() - _busy_t0;
        STATS_UPDATE_MAX(spi_stage_pre_busy_max_us, _stage_dt);
    }
#endif

#ifndef LORAWAN_OBSERVATION_DISABLE
    uint32_t _xfer_start_us = mp_hal_ticks_us();
#endif
    CRITICAL_SECTION_BEGIN();
    cs_low();

    uint8_t rx = 0u;
    uint16_t total_len = 0u;
    for (uint16_t i = 0; i < cmd_len; i++) {
        uint8_t tx = cmd[i];
        sx126x_spi_transfer_one(tx, &rx);
        total_len++;
        if (s_state.interbyte_us != 0u) {
            mp_hal_delay_us(s_state.interbyte_us);
        }
    }

    if (status_out != NULL || (data_in != NULL && data_len > 0u)) {
        uint8_t nop = SX126X_CMD_NOP;
        uint8_t status = 0u;
        sx126x_spi_transfer_one(nop, &status);
        total_len++;
        if (s_state.interbyte_us != 0u) {
            mp_hal_delay_us(s_state.interbyte_us);
        }
        if (status_out != NULL) {
            *status_out = status;
        }
    }

    if (data_in != NULL && data_len > 0u) {
        for (uint16_t i = 0; i < data_len; i++) {
            uint8_t tx = SX126X_CMD_NOP;
            sx126x_spi_transfer_one(tx, &data_in[i]);
            total_len++;
            if (s_state.interbyte_us != 0u) {
                mp_hal_delay_us(s_state.interbyte_us);
            }
        }
    } else if (data_out != NULL && data_len > 0u) {
        for (uint16_t i = 0; i < data_len; i++) {
            uint8_t tx = data_out[i];
            sx126x_spi_transfer_one(tx, &rx);
            total_len++;
            if (s_state.interbyte_us != 0u) {
                mp_hal_delay_us(s_state.interbyte_us);
            }
        }
    }

    cs_high();
    CRITICAL_SECTION_END();
#ifndef LORAWAN_OBSERVATION_DISABLE
    {
        uint32_t _xfer_dt = mp_hal_ticks_us() - _xfer_start_us;
        STATS_UPDATE_MAX(spi_stage_byte_xfer_max_us, _xfer_dt);
    }
#endif

    if (wait_for_busy) {
        mp_hal_delay_us(1);
        SX126xWaitOnBusy();
    }

    STATS_INC(spi_xfer_count);
    STATS_UPDATE_MAX(spi_max_len, total_len);
    stats_spi_bytes_add(total_len);
    if (total_len == 1u) {
        STATS_INC(spi_one_byte_count);
    }

    s_spi_xfer_busy = false;
    return 0;

}

// ---- Public active board layer API: error-returning variants --------------------------

/* Wake-if-sleeping gate at every public entrypoint.
   Vendor SX126xCheckDeviceReady() checks SX126xGetOperatingMode() and calls
   SX126xWakeup() when the chip is in MODE_SLEEP / MODE_RX_DC / MODE_COLD_SLEEP,
   then waits BUSY-low. Without this call the pre-CS BUSY poll inside
   sx126x_spi_xfer can wait forever on a sleeping radio. The post-CS
   skip-wait for SetSleep is the matching half-fix; together they bracket
   the sleep transition correctly. */

int SX126xWriteCommand_e(uint8_t opcode, const uint8_t *buffer, uint16_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[1] = { opcode };
    /* SetSleep (0x84) can leave BUSY high until the next NSS-low wake.
       The Renesas board sources also skip the post-command BUSY wait for
       RADIO_SET_SLEEP; every other opcode keeps it. */
    bool wait_busy = (opcode != SX126X_CMD_SET_SLEEP);
    return sx126x_spi_xfer(cmd, 1, buffer, NULL, size, wait_busy, NULL);
}

int SX126xReadCommand_e(uint8_t opcode, uint8_t *buffer, uint16_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[1] = { opcode };
    return sx126x_spi_xfer(cmd, 1, NULL, buffer, size, /*wait_busy=*/true, NULL);
}

int SX126xWriteRegisters_e(uint16_t address, const uint8_t *buffer, uint16_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[3] = {
        SX126X_CMD_WRITE_REGISTER,
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF),
    };
    return sx126x_spi_xfer(cmd, 3, buffer, NULL, size, /*wait_busy=*/true, NULL);
}

int SX126xReadRegisters_e(uint16_t address, uint8_t *buffer, uint16_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[3] = {
        SX126X_CMD_READ_REGISTER,
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF),
    };
    return sx126x_spi_xfer(cmd, 3, NULL, buffer, size, /*wait_busy=*/true, NULL);
}

int SX126xWriteBuffer_e(uint8_t offset, const uint8_t *buffer, uint8_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[2] = { SX126X_CMD_WRITE_BUFFER, offset };
    return sx126x_spi_xfer(cmd, 2, buffer, NULL, size, /*wait_busy=*/true, NULL);
}

int SX126xReadBuffer_e(uint8_t offset, uint8_t *buffer, uint8_t size) {
    SX126xCheckDeviceReady();
    uint8_t cmd[2] = { SX126X_CMD_READ_BUFFER, offset };
    return sx126x_spi_xfer(cmd, 2, NULL, buffer, size, /*wait_busy=*/true, NULL);
}

/* Canonical SX126x* SPI helpers routed through the Renesas byte-by-byte path.
 * Vendor radio/sx126x/sx126x.c calls these names (declared in
 * radio/sx126x/sx126x.h). These wrappers are the only definitions linked.
 * The _e helpers return int for local callers; upstream void wrappers keep
 * the original Renesas/Semtech surface.
 */
/* Canonical wrappers are always built; active transport is byte-by-byte. */

void SX126xWriteCommand(RadioCommands_t command, uint8_t *buffer, uint16_t size) {
    (void)SX126xWriteCommand_e((uint8_t)command, buffer, size);
}

uint8_t SX126xReadCommand(RadioCommands_t command, uint8_t *buffer, uint16_t size) {
    /* Match the Renesas SX126x signature: every read command clocks one
       status byte immediately after the opcode and returns that status to
       the caller. RADIO_GET_STATUS is just the zero-payload instance of
       the same shape. */
    uint8_t status = 0u;
    uint8_t cmd[1] = { (uint8_t)command };
    SX126xCheckDeviceReady();
    int e = sx126x_spi_xfer(cmd, 1, NULL, buffer, size,
        /*wait_busy=*/true, &status);
    if ((uint8_t)command == SX126X_CMD_GET_STATUS) {
        if (status == 0x00u) {
            e = -MP_EIO;
        } else if (status == 0xFFu) {
            e = -MP_EIO;
        }
    }
    if (e != 0) {
        return 0xFFu;
    }
    return status;
}

void SX126xWriteRegisters(uint16_t address, uint8_t *buffer, uint16_t size) {
    (void)SX126xWriteRegisters_e(address, buffer, size);
}

void SX126xReadRegisters(uint16_t address, uint8_t *buffer, uint16_t size) {
    (void)SX126xReadRegisters_e(address, buffer, size);
}

void SX126xWriteBuffer(uint8_t offset, uint8_t *buffer, uint8_t size) {
    (void)SX126xWriteBuffer_e(offset, buffer, size);
}

void SX126xReadBuffer(uint8_t offset, uint8_t *buffer, uint8_t size) {
    (void)SX126xReadBuffer_e(offset, buffer, size);
}


void SX126xWriteRegister(uint16_t address, uint8_t value) {
    SX126xWriteRegisters(address, &value, 1);
}

uint8_t SX126xReadRegister(uint16_t address) {
    uint8_t data = 0xFFu;
    SX126xReadRegisters(address, &data, 1);
    return data;
}

void SX126xSetRfTxPower(int8_t power) {
    SX126xSetTxParams(power, RADIO_RAMP_40_US);
}

uint8_t SX126xGetPaSelect(void) {
    return devicePowerSelect;
}

void SX126xSetPaSelect(uint8_t paType) {
    if (paType == RADIO_LOPOWER_SEL || paType == RADIO_HIPOWER_SEL) {
        devicePowerSelect = paType;
    }
}

uint8_t SX126xGetClockSelect(void) {
    return deviceClockSelect;
}

void SX126xSetClockSelect(uint8_t clkType) {
    if (clkType == RADIO_CLOCK_XTAL_SEL || clkType == RADIO_CLOCK_TCXO_SEL) {
        deviceClockSelect = clkType;
    }
}

void SX126xAntSwOn(void) {
    if (s_state.rf_sw != NULL) {
        mp_hal_pin_high((mp_hal_pin_obj_t)s_state.rf_sw);
    }
}

void SX126xAntSwOff(void) {
    if (s_state.rf_sw != NULL) {
        mp_hal_pin_low((mp_hal_pin_obj_t)s_state.rf_sw);
    }
}

bool SX126xCheckRfFrequency(uint32_t frequency) {
    (void)frequency;
    return true;
}

uint32_t SX126xGetDio1PinState(void) {
    if (s_state.dio1 == NULL) {
        return 0u;
    }
    return (uint32_t)mp_hal_pin_read((mp_hal_pin_obj_t)s_state.dio1);
}

void SX126xIoDeInit(void) {
    SX126xAntSwOff();
}

void SX126xIoRfSwitchInit(void) {
    SX126xAntSwOn();
}

void SX126xIoDbgInit(void) {
}

uint32_t SX126xGetBoardTcxoWakeupTime(void) {
    return 5U;
}

uint8_t SX126xGetDeviceId(void) {
    return SX1262;
}

static void sx126x_board_dispatch_dio1_irq(void) {
    /* DIO1 notification wrapper. With the normal Semtech handler this only
       sets IrqFired=true; the register/SPI work stays in RadioIrqProcess()
       when the pump later runs LoRaMacProcess(). */
    if (s_state.dio1_c_handler != NULL) {
        s_state.dio1_c_handler();
    }
}

// ---- DIO1 IRQ wiring -----------------------------------------------------
//
// The board callback records DIO1 edges; foreground mac.process() later
// drains the pump and dispatches radio work.
// This helper tears down the public Pin.irq/extint registration made by
// SX126xIoIrqInit(); the pin object itself remains Python-owned.

void SX126xIoIrqDeinit(void) {
    if (s_state.dio1_irq_active && s_state.dio1 != NULL) {
        extint_register_pin(s_state.dio1, MP_HAL_PIN_TRIGGER_RISING,
            true, false, mp_const_none);
    }
    s_state.dio1_c_handler = NULL;
    s_state.dio1_irq_active = false;
}

/* Deferred-pump busy probe. Exposes
   the s_spi_xfer_busy re-entry flag so the pump can skip a Radio.IrqProcess
   pass that would just hit the spi_nested_reject_count gate anyway.
   Also OR in sx126x_in_wait_busy so the pump defers while
   SX126xWaitOnBusy() is holding the synchronous Renesas-style BUSY wait
   window. */
bool sx126x_board_spi_busy(void) {
    return s_spi_xfer_busy || sx126x_in_wait_busy;
}

#endif // MICROPY_HW_LORA_STACK_RENESAS
