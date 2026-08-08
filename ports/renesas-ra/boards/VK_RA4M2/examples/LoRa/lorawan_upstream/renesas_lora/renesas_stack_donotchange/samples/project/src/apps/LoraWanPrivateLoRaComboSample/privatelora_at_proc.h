/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_at_proc.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef _PRIVATELORA_AT_PROC_H_
#define _PRIVATELORA_AT_PROC_H_

#include "PrivateLoRa.h"
#include "at_proc.h"

/*--------*/
/* define */
#define APP_PRVLORA_MCPSHANDLE_ATSEND       0x4000
#define APP_PRVLORA_MCPSHANDLE_ATSENDHEX    0x8000

/*----------------*/
/* typedef (enum) */

// to store command index
typedef enum _AppPrvLoRaAtCmdIndex_t
{
    APP_PRVLORA_ATCMD_INDEX_SEND = 0,
    APP_PRVLORA_ATCMD_INDEX_SENDHEX,
    APP_PRVLORA_ATCMD_INDEX_KEYREQ,
    APP_PRVLORA_ATCMD_INDEX_DEVINFO,
    APP_PRVLORA_ATCMD_INDEX_TXCYCLE,
    /*---*/
    MAXNUM_APP_PRVLORA_ATCMD_INDEX
} AppPrvLoRaAtCmdIndex_t;


/*--------------------------*/
/* global variable (extern) */
extern uint16_t appPrvLoRaAtCmdIndex[ MAXNUM_APP_PRVLORA_ATCMD_INDEX ];

/*-----------*/
/* Functions */

// Init
extern void AppAtPrvLoRaAtInit( uint8_t *p_appAtOctBuff, int16_t appAtOctBuffSize );

// Output
extern void AppAtPrvLoRaStatusResult( PrvLoRaStatus_t status );
extern void AppAtPrvLoRaEventResult( PrvLoRaEventInfoStatus_t status );

// AT commands

// (AT+RESET)
extern AtResultCode_t AppAtPrvLoRaResetAct( void *p );
// (AT+VER)
extern AtResultCode_t AppAtPrvLoRaVerRead( void *p );
// (AT+SAVE)
extern AtResultCode_t AppAtPrvLoRaVerSaveAct( void *p );
// (AT+LOAD)
extern AtResultCode_t AppAtPrvLoRaVerLoadAct( void *p );
// (AT+REGION)
extern AtResultCode_t AppAtPrvLoRaRegionSet( void *p );
extern AtResultCode_t AppAtPrvLoRaRegionRead( void *p );
// (AT+DEVEUI)
extern AtResultCode_t AppAtPrvLoRaMacAddrSet( void *p );
extern AtResultCode_t AppAtPrvLoRaMacAddrRead( void *p );
// (AT+CHID)
extern AtResultCode_t AppAtPrvLoRaChannelIDSet( void *p );
extern AtResultCode_t AppAtPrvLoRaChannelIDRead( void *p );
// (AT+DR)
extern AtResultCode_t AppAtPrvLoRaDRSet( void *p );
extern AtResultCode_t AppAtPrvLoRaDRRead( void *p );
// (AT+TXPOWER)
extern AtResultCode_t AppAtPrvLoRaTxPowerSet( void *p );
extern AtResultCode_t AppAtPrvLoRaTxPowerRead( void *p );
// (AT+RXON)
extern AtResultCode_t AppAtPrvLoRaRxOnWhenIdleSet( void *p );
extern AtResultCode_t AppAtPrvLoRaRxOnWhenIdleRead( void *p );
// (AT+RMTDEV)
extern AtResultCode_t AppAtPrvLoRaRemoveDevSet( void *p );
// (AT+KEYREQ)
extern AtResultCode_t AppAtPrvLoRaKeyReqAct( void *p );
// (AT+KEYRES)
extern AtResultCode_t AppAtPrvLoRaKeyResSet( void *p );
extern AtResultCode_t AppAtPrvLoRaKeyResRead( void *p );
// (AT+TXOPT)
extern AtResultCode_t AppAtPrvLoRaTxOptionsSet( void *p );
extern AtResultCode_t AppAtPrvLoRaTxOptionsRead( void *p );
// (AT+SEND)
extern AtResultCode_t AppAtPrvLoRaSendAct( void *p );
// (AT+SENDHEX)
extern AtResultCode_t AppAtPrvLoRaSendHexAct( void *p );
// (AT+DEVINFO)
extern AtResultCode_t AppAtPrvLoRaDevInfoAct( void *p );
// (AT+TXCYCLE)
extern AtResultCode_t AppAtPrvLoRaTxCycleAct( void *p );
// (AT+RSSI)
extern AtResultCode_t AppAtPrvLoRaRssiSet( void *p );
extern AtResultCode_t AppAtPrvLoRaRssiRead( void *p );
#if defined(DEBUG_PRVLORA)
// (AT+DEBUG)
extern AtResultCode_t AppAtPrvLoRaDebugSet( void *p );
extern AtResultCode_t AppAtPrvLoRaDebugRead( void *p );
#endif
#if defined(LORACOMBO_ENABLED)
// (AT+LORAMODE)
extern AtResultCode_t AppAtLoRaModeSet( void *p );
extern AtResultCode_t AppAtLoRaModeRead( void *p );
#endif

#endif /* _PRIVATELORA_AT_PROC_H_ */
