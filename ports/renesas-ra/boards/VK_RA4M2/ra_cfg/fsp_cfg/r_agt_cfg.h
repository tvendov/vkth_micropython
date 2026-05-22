/*
 * AGT Configuration for VK_RA4M2 (LoRaWAN renesas stack timer path).
 * Param-checking on; pulse-output / event-input branches disabled because the
 * LoRaWAN timer-board.c only uses periodic-mode + compare-match A.
 */

#ifndef R_AGT_CFG_H
#define R_AGT_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#define AGT_CFG_PARAM_CHECKING_ENABLE       (1)
#define AGT_CFG_OUTPUT_SUPPORT_ENABLE       (0)
#define AGT_CFG_INPUT_SUPPORT_ENABLE        (0)

#ifdef __cplusplus
}
#endif

#endif /* R_AGT_CFG_H */
