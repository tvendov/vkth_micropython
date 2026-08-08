/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORAFRAGMENTCONFIG_H__
#define __LORAFRAGMENTCONFIG_H__

// Configuration for Fragmented Data Block Transport
#ifdef FUOTA_ENABLED
#include "LoRaFuotaConfig.h"  // configuration value is in it.
#define FRGMNT_CONFIG_MAX_FRAG_INDEX    FUOTA_CONFIG_FRGMNT_MAX_FRAG_INDEX
#define FRGMNT_CONFIG_MAX_DATABLK_SIZE  FUOTA_CONFIG_FRGMNT_MAX_DATABLK_SIZE
#define FRGMNT_CONFIG_MAX_NBFRAG        FUOTA_CONFIG_FRGMNT_MAX_NBFRAG
#define FRGMNT_CONFIG_MAX_NBFRAG_LOST   FUOTA_CONFIG_FRGMNT_MAX_NBFRAG_LOST

#else  // FUOTA_ENABLED
// configure here
#define FRGMNT_CONFIG_MAX_FRAG_INDEX    1       // Max number of FragIndex (fragment session) [1-4]
#define FRGMNT_CONFIG_MAX_DATABLK_SIZE  16384   // Max buffer size to receive fragment data block
#define FRGMNT_CONFIG_MAX_NBFRAG        200     // Max number of uncoded fragment
#define FRGMNT_CONFIG_MAX_NBFRAG_LOST   50      // Max number of dropped uncoded fragment 

#endif  // FUOTA_ENABLED


#endif  /* __LORAFRAGMENTCONFIG_H__ */
