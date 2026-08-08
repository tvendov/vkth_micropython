/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAAPPCLOCKSYNCCONFIG_H__
#define __LORAAPPCLOCKSYNCCONFIG_H__

// Configuration for Clock Synchronization
#ifdef FUOTA_ENABLED
#include "LoRaFuotaConfig.h"  // configuration value is in it.
#define CLKSNC_CONFIG_INIT_SYNC_PERIOD_SEC      FUOTA_CONFIG_CLKSNC_INIT_SYNC_PERIOD_SEC

#else  // FUOTA_ENABLED
// configure here
#define CLKSNC_CONFIG_INIT_SYNC_PERIOD_SEC      60  // AppTimeReq request period (sec) until AppTime is synchronized [10-0x418390]

#endif  // FUOTA_ENABLED


#endif  /* __LORAAPPCLOCKSYNCCONFIG_H__ */
