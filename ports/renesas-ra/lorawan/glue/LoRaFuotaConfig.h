/*
 * lorawan/glue/LoRaFuotaConfig.h
 *
 * Stub configuration header. The imported soft-se.c includes this when
 * the FUOTA sample app is enabled — we keep FUOTA explicitly disabled.
 */

#ifndef LORAWAN_GLUE_LORA_FUOTA_CONFIG_H
#define LORAWAN_GLUE_LORA_FUOTA_CONFIG_H

/* FUOTA is explicitly disabled. All blocks gated by this flag are
   compiled out. */
#define LORA_FUOTA_ENABLE        (0)
#define USE_RM_TINYCRYPT         (0)

#endif /* LORAWAN_GLUE_LORA_FUOTA_CONFIG_H */
