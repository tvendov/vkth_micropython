/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __APP_FUOTA_AT_PROC_H__
#define __APP_FUOTA_AT_PROC_H__

#include "LoRaFuotaConfig.h"
#include "LoRaFuotaStatus.h"

extern void AppAtFuotaExtendCmdRegist( void );

extern void AppAtFuotaUpdateActResult( FuotaStatus_t result );
extern void AppAtFuotaUpdateActConfirm( FuotaStatus_t result );

// indication
extern void AppAtFuotaRmtMcSessionSetupIndication( DeviceClass_t sessionClass, 
                                                   uint8_t       mcGroupId, 
                                                   uint32_t      timeToStartSec,
                                                   uint32_t      timeoutSec );
extern void AppAtFuotaRmtMcSessionStartIndication( DeviceClass_t sessionClass, 
                                                   uint8_t       mcGroupId, 
                                                   uint32_t      timeoutSec );
extern void AppAtFuotaRmtMcSessionEndIndication( DeviceClass_t sessionClass, uint8_t mcGroupId );
extern void AppAtFuotaUpdateReadyIndication( void );
extern void AppAtFuotaUpdateErrorIndication( uint8_t result );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
extern void AppAtFuotaUpdateDeleteFwImageIndication( uint32_t fwImageVersion );
extern void AppAtFuotaUpdateTimeToRebootSecIndication( uint32_t rebootTimeSec );
extern void AppAtFuotaUpdateRebootTimingIndication( void );
#endif


#ifdef DEBUG_FUOTA
extern void AppAtFuotaDebugPrintUplink( uint8_t fport,  uint8_t *p_buffer, uint8_t length );
extern void AppAtFuotaDebugGetMcpsIndResult( uint8_t fuotaStatus );
#endif

#endif  // __APP_FUOTA_AT_PROC_H__

