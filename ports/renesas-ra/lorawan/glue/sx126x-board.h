/*
 * lorawan/glue/sx126x-board.h
 *
 * Shim header that satisfies upstream `radio/sx126x.c`'s
 *   #include "sx126x-board.h"
 *
 * The actual prototypes are declared in our `glue/sx126x_board.h` (with
 * an underscore — modern naming). This shim re-exports the upstream-
 * convention names with hyphenated header so imported sources compile
 * unchanged. Both names share the same prototype signatures and same
 * implementations in `glue/sx126x_board.c`.
 *
 * The signatures intentionally match LoRaMac-node conventions:
 *   * `RadioCommands_t` opcode argument (= uint8_t enum) for
 *     SX126xWriteCommand / SX126xReadCommand.
 *   * `DioIrqHandler` (= `void (*)(void)`) for SX126xIoIrqInit.
 */

#ifndef __SX126x_BOARD_H__
#define __SX126x_BOARD_H__

#include <stdint.h>
#include <stdbool.h>

/* Pulls in `RadioCommands_t`, `RadioOperatingModes_t`, `SX126x_t`,
   `DioIrqHandler` typedefs from the imported radio driver header. */
#include "sx126x.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Lifecycle (NOPs on this port — handled by sx126x_board_init) -- */
void SX126xIoInit(void);
void SX126xIoDeInit(void);
void SX126xIoTcxoInit(void);
void SX126xIoRfSwitchInit(void);
void SX126xIoDbgInit(void);

/* ---- IRQ wiring ---------------------------------------------------- */
void SX126xIoIrqInit(DioIrqHandler dioIrq);

/* ---- Reset / busy / wakeup ----------------------------------------- */
void SX126xReset(void);
void SX126xWaitOnBusy(void);
void SX126xWakeup(void);

/* ---- SPI command + register / buffer ------------------------------- */
void    SX126xWriteCommand(RadioCommands_t opcode, uint8_t *buffer, uint16_t size);
uint8_t SX126xReadCommand(RadioCommands_t opcode, uint8_t *buffer, uint16_t size);
void    SX126xWriteRegister(uint16_t address, uint8_t value);
uint8_t SX126xReadRegister(uint16_t address);

/* ---- RF / status / queries ----------------------------------------- */
void                  SX126xSetRfTxPower(int8_t power);
uint8_t               SX126xGetDeviceId(void);
void                  SX126xAntSwOn(void);
void                  SX126xAntSwOff(void);
bool                  SX126xCheckRfFrequency(uint32_t frequency);
uint32_t              SX126xGetBoardTcxoWakeupTime(void);
uint32_t              SX126xGetDio1PinState(void);
RadioOperatingModes_t SX126xGetOperatingMode(void);
void                  SX126xSetOperatingMode(RadioOperatingModes_t mode);

/* ---- Singleton radio state (defined in radio/sx126x.c) ------------- */
extern SX126x_t SX126x;

/* ---- Board-level PA / clock select state --------------------------- */
void    SX126xBoardConfigInit(void);
uint8_t SX126xGetPaSelect(void);
void    SX126xSetPaSelect(uint8_t paType);
uint8_t SX126xGetClockSelect(void);
void    SX126xSetClockSelect(uint8_t clkType);

#ifdef __cplusplus
}
#endif

#endif /* __SX126x_BOARD_H__ */
