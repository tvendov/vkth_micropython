/*
 * Vector-number aliases for the pristine vendor LoRaWAN timer-board.c.
 *
 * The RA2L1EK source refers to VECTOR_NUMBER_AGT0_INT / _AGT1_INT /
 * _AGT1_COMPARE_A, which on VK_RA4M2 are reserved for the port's own
 * Timer(1..4) and must NOT be touched by the LoRaWAN stack. The
 * LoRaWAN free-run + sub-second alarm run on AGT4 and AGT5 instead
 * (per timer_1to1_vendor_reuse_design_audit.md).
 *
 * This header is force-included only when compiling the vendor
 * timer-board.c (see lorawan/lorawan.mk: -include flag on
 * timer-board.o rule). Including it from other translation units
 * (e.g. lorawan_hal_data.c) also works — the aliases are pure
 * preprocessor and resolve to the same IRQn_Type values as the
 * board's vector_data.h.
 *
 * Mapping:
 *   AGT0_INT       → AGT4_INT       (slot 50)
 *   AGT1_INT       → AGT5_INT       (slot 51)
 *   AGT1_COMPARE_A → AGT5_COMPARE_A (slot 62)
 */

#ifndef LORAWAN_VECTOR_ALIASES_H
#define LORAWAN_VECTOR_ALIASES_H

#include "vector_data.h"

#ifdef VECTOR_NUMBER_AGT0_INT
#undef VECTOR_NUMBER_AGT0_INT
#endif
#define VECTOR_NUMBER_AGT0_INT       VECTOR_NUMBER_AGT4_INT

#ifdef VECTOR_NUMBER_AGT1_INT
#undef VECTOR_NUMBER_AGT1_INT
#endif
#define VECTOR_NUMBER_AGT1_INT       VECTOR_NUMBER_AGT5_INT

#ifdef VECTOR_NUMBER_AGT1_COMPARE_A
#undef VECTOR_NUMBER_AGT1_COMPARE_A
#endif
#define VECTOR_NUMBER_AGT1_COMPARE_A VECTOR_NUMBER_AGT5_COMPARE_A

#endif /* LORAWAN_VECTOR_ALIASES_H */
