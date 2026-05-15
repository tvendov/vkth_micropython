/*
 * lorawan/glue/sx126x_board.h
 *
 * SX126x board-glue API surface for the Renesas LoRaWAN C-stack.
 *
 * Phase 1 — real SPI / GPIO / DIO1-IRQ implementation. Pin defaults and SPI
 * timing are taken verbatim from the proven Python driver
 *   ports/renesas-ra/boards/VK_RA4M2/examples/LoRa/_upstream/sx126x.py
 * which has been bench-tested against TTN OTAA join + uplink/downlink.
 *
 * VK_RA4M2 + Wio-SX1262 daughterboard pin map (lorawan_app.py:1056-1059):
 *   SCK   = P111  (SCI9 SCK)        — set via SPI(3) MicroPython object
 *   MOSI  = P109  (SCI9 TXD9)
 *   MISO  = P110  (SCI9 RXD9)
 *   NSS   = P206  (manual GPIO)     — NOT hardware SSL
 *   DIO1  = P015  (rising-edge IRQ) — radio interrupt line
 *   RESET = P001  (active-low)
 *   BUSY  = P002  (polled before each SPI transaction)
 *   RF_SW = P100  (driven HIGH at init)
 *
 * SPI parameters: 8 MHz, MODE0 (CPOL=0, CPHA=0), MSB first, 8-bit.
 *
 * Critical timing note (replicated from sx126x.py:1326-1337):
 *   At 8 MHz SCI9 clock, SX126x cannot prepare its status byte if bytes
 *   flow back-to-back. We issue ONE `ra_sci_spi_transfer` per byte and
 *   rely on FSP/DTC setup overhead (~3-8 µs/call) to give the chip
 *   inter-byte settle time. If the chip returns 0x00/0xFF status, the
 *   gap is too short — add `mp_hal_delay_us(5)` between bytes.
 */

#ifndef LORAWAN_GLUE_SX126X_BOARD_H
#define LORAWAN_GLUE_SX126X_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "py/obj.h"
#include "py/mphal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Pin / SPI configuration ---------------------------------------------

typedef struct {
    uint8_t spi_id;        // 3 — SPI(3) = SCI9 simple SPI on VK_RA4M2
    uint32_t spi_baud_hz;  // typical 8_000_000

    // Pins as MicroPython pin objects (machine_pin_obj_t *). Stored as void *
    // so this header does not pull in pin.h transitively.
    void *cs_pin;
    void *reset_pin;
    void *busy_pin;
    void *dio1_pin;
    void *rf_sw_pin;       // optional — pass NULL if hard-wired
} sx126x_board_cfg_t;

void sx126x_board_init(const sx126x_board_cfg_t *cfg);
void sx126x_board_deinit(void);
bool sx126x_board_is_initialized(void);

// ---- Canonical SX126x* prototypes ----------------------------------------
// Pulled in from glue/sx126x-board.h (which includes radio/sx126x.h for
// the RadioCommands_t / RadioOperatingModes_t / SX126x_t / DioIrqHandler
// typedefs). This single include is the authoritative declaration for
// SX126xReset/WaitOnBusy/Wakeup/Write*/Read*/IoIrqInit/IoIrqDeinit/
// AntSwOn/AntSwOff/etc. — exactly matching upstream radio/sx126x.c
// expectations. Our impls in glue/sx126x_board.c link to the same
// symbols.
#include "sx126x-board.h"

// ---- Internal int-returning variants -------------------------------------
// Used by mod_lorawan.c testing helpers when the caller wants to know
// the SX126x status code. Each wraps the void variant and returns
// 0 on success, negative on timeout / SPI failure.

int  SX126xWriteCommand_e(uint8_t opcode, const uint8_t *buffer, uint16_t size);
int  SX126xReadCommand_e(uint8_t opcode, uint8_t *buffer, uint16_t size);
int  SX126xWriteRegisters_e(uint16_t address, const uint8_t *buffer, uint16_t size);
int  SX126xReadRegisters_e(uint16_t address, uint8_t *buffer, uint16_t size);
int  SX126xWriteBuffer_e(uint8_t offset, const uint8_t *buffer, uint8_t size);
int  SX126xReadBuffer_e(uint8_t offset, uint8_t *buffer, uint8_t size);

// Tear-down counterpart to upstream `SX126xIoIrqInit`. Releases the
// ICU vector and clears the registered handler. Not present in the
// upstream API surface (LoRaMac never deinits) — provided here for
// `lorawan.Mac.deinit()` cleanup.
void SX126xIoIrqDeinit(void);

// Optional BUSY-pin falling-edge IRQ. When enabled, BUSY=high→low fires a
// flag that `SX126xWaitOnBusy()` checks in fast-path. If the BUSY pin has
// no available ICU vector (board-dependent), this returns false and the
// driver falls back to the polling implementation.
bool SX126xBusyIrqEnable(void);
void SX126xBusyIrqDisable(void);

// Runtime tuning of the inter-byte delay used in the byte-at-a-time SPI
// pattern. Default = 0 µs (FSP/DTC setup overhead alone is normally
// enough). Bump to 5–10 µs only if `radio_get_status()` returns 0x00 or
// 0xFF, which indicates the SX126x missed status preparation.
void sx126x_board_set_interbyte_us(uint8_t us);
uint8_t sx126x_board_get_interbyte_us(void);

// Debug accessors — surface internal C state to mod_lorawan testing
// methods so we can diagnose ra_sci_spi_transfer behaviour directly.
uint8_t sx126x_board_get_spi_id(void);

/* Phase 4 — defer probe for the guarded C pump. Returns the current
   value of the file-private s_spi_xfer_busy re-entry guard. Allows
   lorawan_pump.c to defer pump_run() if a SPI transaction is in
   progress in scheduler/Python context. */
bool sx126x_board_spi_busy(void);

/* Phase 7 — invoke the DIO1 C handler registered via SX126xIoIrqInit()
   (i.e. radio.c's RadioOnDioIrq). Called from the guarded-pump body in
   safe (scheduler) context after s_radio_irq_pending is consumed. No-op
   if no handler is registered. Keeps s_state encapsulated in
   sx126x_board.c — pump.c does not link directly against RadioOnDioIrq. */
void sx126x_board_dispatch_dio1_irq(void);

/*
 * Byte-by-byte SPI debug helpers. Real implementation is gated behind
 * LORAWAN_DEBUG_SPI_UNSAFE. In production builds (default), these
 * return 0xFF sentinels without touching SPI. Production binary must
 * have spi_one_byte_count == 0 — Step 15 grep gate enforces this.
 */
uint8_t sx126x_board_debug_xchg(uint8_t tx_byte);
uint8_t sx126x_phase1_get_status(void);

// ---- Phase 1 testing surface — direct exposure to mod_lorawan ------------
// These let us validate the radio HAL from Python before LoRaMac lands.


uint8_t sx126x_phase1_read_register(uint16_t addr);
void    sx126x_phase1_write_register(uint16_t addr, uint8_t value);
bool    sx126x_phase1_busy_high(void);

#ifdef __cplusplus
}
#endif

#endif // LORAWAN_GLUE_SX126X_BOARD_H
