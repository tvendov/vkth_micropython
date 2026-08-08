/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __LORAMULTIPACKAGEACCESSCONFIG_H__
#define __LORAMULTIPACKAGEACCESSTCONFIG_H__

// Configuration for Multi Package Access
#ifdef FUOTA_ENABLED
#include "LoRaFuotaConfig.h"  // configuration value is in it.
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
// (nothing)
#endif

#else  // FUOTA_ENABLED
// configure here
// (nothing)

#endif  // FUOTA_ENABLED

#endif  /* __LORAMULTIPACKAGEACCESSCONFIG_H__ */
