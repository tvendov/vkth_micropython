/*
 * lorawan/glue/pinName-board.h
 *
 * Shim header. The upstream `system/gpio.h` declares
 *   typedef enum { MCU_PINS, IOE_PINS, NC } PinNames;
 * where `MCU_PINS` and `IOE_PINS` are macros expected to expand to a
 * comma-separated list of board-specific pin enumerators (PA_0, PA_1,
 * ..., MCU_PIN_0, ...). On VK_RA4M2 we drive every SX126x pin via the
 * MicroPython `mp_hal_pin_*` layer (see glue/sx126x_board.c), so the
 * imported tree never references a literal pin enumerator. We expand
 * both macros to empty so the upstream enum collapses to just `NC`.
 */

#ifndef LORAWAN_GLUE_PINNAME_BOARD_H
#define LORAWAN_GLUE_PINNAME_BOARD_H

/* Single dummy pin enumerators — board-specific pin literals are not
   used by the imported LoRaMac/sx126x sources, but the upstream
   `system/gpio.h` enum expects MCU_PINS and IOE_PINS to expand to a
   non-empty comma-separated identifier list. */
#define MCU_PINS    LORAWAN_MCU_PIN_DUMMY
#define IOE_PINS    LORAWAN_IOE_PIN_DUMMY

#endif /* LORAWAN_GLUE_PINNAME_BOARD_H */
