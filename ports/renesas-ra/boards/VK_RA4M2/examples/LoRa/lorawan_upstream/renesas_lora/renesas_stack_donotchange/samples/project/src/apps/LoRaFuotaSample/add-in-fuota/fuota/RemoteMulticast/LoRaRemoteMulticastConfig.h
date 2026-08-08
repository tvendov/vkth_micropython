/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAREMOTEMULTICASTCONFIG_H__
#define __LORAREMOTEMULTICASTCONFIG_H__

// Configuration for Remote Multicast Setup
#ifdef FUOTA_ENABLED
#include "LoRaFuotaConfig.h"  // configuration value is in it.
#define RMTMC_CONFIG_MAX_MC_CTX         FUOTA_CONFIG_RMTMC_MAX_MC_CTX

#else  // FUOTA_ENABLED
// configure here
#define RMTMC_CONFIG_MAX_MC_CTX         1       // Max number of multicast context [1-LORAMAC_MAX_MC_CTX]

#endif  // FUOTA_ENABLED

//-----------------------------------------------------------------------------
// (don't edit)
#if ( RMTMC_CONFIG_MAX_MC_CTX > LORAMAC_MAX_MC_CTX )
#error RMTMC_CONFIG_MAX_MC_CTX is greater than LORAMAC_MAX_MC_CTX.
#endif

#endif  /* __LORAREMOTEMULTICASTCONFIG_H__ */
