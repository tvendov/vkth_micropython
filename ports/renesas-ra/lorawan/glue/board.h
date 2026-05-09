/*
 * lorawan/glue/board.h
 *
 * Minimal board-glue header for the imported Renesas LoRaMac C tree.
 * The upstream `boards/<board>/board.h` is FSP-6.2-bound and we deliberately
 * do NOT use it. This file provides only the symbols that imported `mac/`,
 * `radio/`, and `peripherals/soft-se/` actually reference.
 *
 * Implementation in `glue/utilities.c` (and other glue source files).
 */

#ifndef LORAWAN_GLUE_BOARD_H
#define LORAWAN_GLUE_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Length of the unique-ID buffer that BoardGetUniqueId() fills. */
#define BOARD_UNIQUE_ID_LEN  (8u)

/* SX126x clock-source selectors — referenced by LoRaMac.c when arming
   the RX window so it can compensate for TCXO startup time. Wio-SX1262
   uses an external TCXO controlled via DIO3. */
#define RADIO_CLOCK_XTAL_SEL    (0u)
#define RADIO_CLOCK_TCXO_SEL    (1u)

/* TCXO stabilization time in 15.625 µs ticks (per SX1262
   SetDIO3AsTCXOCtrl spec). 320 ticks = 5 ms — matches Wio-SX1262
   datasheet startup characteristic. */
#define RP_TCXO_STAB_TIME       (320u)

/* Clock-error budget used by LoRaMacClassB / RX-window math. SX1262 with
   external TCXO is tight (~10 ppm steady-state); _MAX is the post-PoR
   transient before sync (LoRaMac-node default 100 ppm). */
#define BOARD_CLOCK_ERROR_PPM       (10)
#define BOARD_CLOCK_ERROR_PPM_MAX   (100)

/* SX1262 XTAL trim caps. 0x12 is the chip reset default — the
   datasheet recommended value for typical 32 MHz crystal layouts. */
#define RP_XTAL_XTA_TRIM        (0x12u)
#define RP_XTAL_XTB_TRIM        (0x12u)

/* PA-select sentinel used by `SX126xSetTxParams` to dispatch between
   the SX1261 low-power and SX1262 high-power code paths:
     SX126xGetPaSelect() == RADIO_LOPOWER_SEL  → SX1261 LP path (max +14 dBm)
     SX126xGetPaSelect() != RADIO_LOPOWER_SEL  → SX1262 HP path (max +22 dBm)
   Wio-SX1262 uses the SX1262 HP path, so we set the sentinel to a
   value the default `s_pa_select` (= SX1262 in glue/sx126x_board.c)
   never matches. */
#define RADIO_LOPOWER_SEL       (0xFFu)

/* Time-On-Air calibration offsets (ms). Renesas tuned these for the
   RA0/RA2 reference platforms; on RA4M2 with 100 MHz core the SPI
   command latency is lower, so 0 is a safe baseline. Refine per
   measurement only if a gateway flags timing drift. */
#define RP_TOA_OFFSET_FSK_7KBPS    (0u)
#define RP_TOA_OFFSET_FSK_3KBPS    (0u)
#define RP_TOA_OFFSET_FSK_2KBPS    (0u)
#define RP_TOA_OFFSET_FSK_1KBPS    (0u)
#define RP_TOA_OFFSET_FSK_600BPS   (0u)
#define RP_TOA_OFFSET_LORA         (0u)

/* Maximum CCA listen window in ms — used by Private LoRa (we don't
   use it on TTN LoRaWAN, but the constant is referenced in radio.c
   unconditionally). */
#define RP_CCA_TIMER_MAX_TIME_MS   (1000u)

/* Pulls in SX126xGetClockSelect / SX126xGetPaSelect / etc. so that
   LoRaMac.c (which only includes "board.h") sees them. */
#include "sx126x-board.h"

/* Critical-section helpers used by `boards/utilities.h`'s
   CRITICAL_SECTION_BEGIN/END macros. The mask saves the previous
   PRIMASK; nesting count is in `g_critical_sectioin_count`. */
void BoardCriticalSectionBegin(uint32_t *mask);
void BoardCriticalSectionEnd(uint32_t *mask);

/* Optional hooks the upstream macros invoke around the AGT IRQ. We
   route them to the AGT4 lock — but since LoRaMac's protected sections
   are short (<10 µs), nesting just toggles PRIMASK; AGT-IRQ stays
   masked anyway. Implemented as NOPs in glue/utilities.c. */
void BoardTimerDisableIrq(void);
void BoardTimerEnableIrqNoClear(void);

/* Used by soft-se to derive default DevEUI / AppEUI from the silicon
   unique ID register. RA4M2 has a 16-byte unique ID at 0x01001C00; we
   return the low 8 bytes. */
void BoardGetUniqueId(uint8_t *id);

/* LoRaMac calls this when seeding the secure-element. We return a
   sentinel (255 = unknown). soft-se does not actually require it. */
uint8_t BoardGetBatteryLevel(void);

/* Random seed for srand1() — derived from the unique ID + a runtime
   counter so each boot produces a different sequence. */
uint32_t BoardGetRandomSeed(void);

/* Global IRQ mask helpers used by `LoRaMacSetLowPower()`. Implemented
   in glue/utilities.c via the Cortex-M PRIMASK. NOP on this port —
   MicroPython manages its own sleep entry; we just satisfy the symbol. */
void BoardDisableAllIrq(void);
void BoardEnableAllIrq(void);

/* MCU sleep entry. NOP on this port — `machine.lightsleep()` is the
   user-facing knob; LoRaMac.c only calls this when it has decided the
   stack is idle, but we leave the actual WFI to the MicroPython
   power-management layer. */
void SetLowPower(void);

/* Board-level radio IRQ preprocessor — called by upstream radio.c
   before dispatching the SX126x DIO1 IRQ. Used by Renesas Combo /
   Private LoRa to multiplex the IRQ; on plain LoRaWAN it is a NOP. */
void BoardRadioIrqPreprocess(void);

/* CCA (Private-LoRa Channel Activity Assessment) timer hooks. We do
   not run Private LoRa on TTN LoRaWAN, but radio.c references these
   unconditionally. Backed by AGT4 ms tick (16-bit truncation). */
uint16_t RpMcuCcaGetCurrentCount(void);
uint16_t RpMcuCcaDiffTime(uint16_t countStart);

/* Critical-section nesting counter referenced by the
   CRITICAL_SECTION_BEGIN/END macros in `boards/utilities.h`. */
extern volatile uint8_t g_critical_sectioin_count;

#ifdef __cplusplus
}
#endif

#endif /* LORAWAN_GLUE_BOARD_H */
