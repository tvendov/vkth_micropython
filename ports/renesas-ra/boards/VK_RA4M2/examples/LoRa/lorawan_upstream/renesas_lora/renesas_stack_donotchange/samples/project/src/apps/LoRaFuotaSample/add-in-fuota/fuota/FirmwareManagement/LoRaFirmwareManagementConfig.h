/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef __LORAFIRMWAREMANAGEMENTCONFIG_H__
#define __LORAFIRMWAREMANAGEMENTCONFIG_H__

// Configuration for Firmware management protocol
#ifdef FUOTA_ENABLED
#include "LoRaFuotaConfig.h"  // configuration value is in it.

#else  // FUOTA_ENABLED
// configure here
// (nothing)
#endif  // FUOTA_ENABLED


#endif  /* __LORAFIRMWAREMANAGEMENTCONFIG_H__ */
