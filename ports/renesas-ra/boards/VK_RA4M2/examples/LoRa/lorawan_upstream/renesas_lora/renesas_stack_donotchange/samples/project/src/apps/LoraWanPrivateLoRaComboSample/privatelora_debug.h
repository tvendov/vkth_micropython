/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_debug.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifdef DEBUG_PRVLORA

#ifndef __PRIVATELORA_DEBUG_H__
#define __PRIVATELORA_DEBUG_H__

#include "PrivateLoRa.h"

/*--------*/
/* define */
#define APP_PRVLORA_DEBUG_APP_PSEUDO_MCULOWPWR        0x00000100
#define APP_PRVLORA_DEBUG_APP_DISP_TXCYCLE            0x00001000
#define APP_PRVLORA_DEBUG_APP_DISP_MACNOTIFY_RMTDEV   0x00002000

/*-----------*/
/* Functions */

// Debug mode
extern void AppPrvLoRaDebugInit( void );
extern void AppPrvLoRaDebugSetMode( uint32_t debugMode );
extern uint32_t AppPrvLoRaDebugGetMode( void );

// Debug functions
extern bool AppPrvLoRaDebugIsPseudoLowPowerAllowed( void );
extern int16_t AppPrvLoRaDebugSetPseudoLowPower( void );
extern void AppPrvLoRaDebugDispTxCycle( PrvLoRaStatus_t status );
extern void AppPrvLoRaDebugDispMacNotifyRemoteDeviceInfo( PrvLoRaNotifyUpdatedRemoteDev_t *p_updtRemoteDev );


#endif  /* __PRIVATELORA_DEBUG_H__ */

#endif  // DEBUG_PRVLORA
