/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_sample.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef _PRIVATELORA_SAMPLE_H_
#define _PRIVATELORA_SAMPLE_H_

#include "PrivateLoRa.h"

#include "main.h"
#include "privatelora_proc.h"
#include "privatelora_at_proc.h"

/*-------*/
/* macro */

// version info
#define APP_PRVLORA_VERSION_FW    0x04900000  // 4Byte (Major-Minor-Patch-Rev)
#define APP_PRVLORA_VERSION_HW    0x01000000  // 4Byte (Major-Minor-Patch-Rev)

// status
#define APP_PRVLORA_STATUS_OK           0
#define APP_PRVLORA_STATUS_ERROR        1


/*----------------*/
/* typedef (enum) */

/*------------------------*/
/* typedef (struct/union) */

/*--------------------------*/
/* global variable (extern) */

// Version information
extern const Version_t appPrvLoRaFwVersion;
extern const Version_t appPrvLoRaHwVersion;

// default PrivateLoRa sample application setting
extern const AppPrvLoRaSettings_t appPrvLoraDefaultSettings;


/*-----------*/
/* Functions */ 

// (privatelora_main.c)
// init
extern void AppPrvLoRaMainInit( void );
extern uint8_t AppPrvLoRaMainStart( void );
extern uint8_t AppPrvLoRaMainStop( void );
extern bool AppPrvLoRaMainIsActive( void );
// parameter
extern void AppPrvLoRaMainResetParams( void );
extern void AppPrvLoRaMainFactoryResetParams( bool isFormatted );
extern void AppPrvLoRaMainLoadParams( void );
extern void AppPrvLoRaMainSaveParams( void );
// main
extern void AppPrvLoRaMainProcess( void );
extern uint8_t AppPrvLoRaMainLowPower( void );


// (privatelora_event.c)
// PrivateLoRa events
extern void AppPrvLoRaCallbackMcpsConfirm( PrvLoRaMcpsCfm_t *p_mcpsCfm );
extern void AppPrvLoRaCallbackMcpsIndication( PrvLoRaMcpsInd_t *p_mcpsInd );
extern void AppPrvLoRaCallbackMlmeConfirm( PrvLoRaMlmeCfm_t *p_mlmeCfm );
extern void AppPrvLoRaCallbackMlmeIndication( PrvLoRaMlmeInd_t *p_mlmeInd );
extern void AppPrvLoRaCallbackMacNotification( PrvLoRaNotification_t *p_notify );
// MCU/board events
extern bool AppPrvLoRaCallbackBoardIsLowPowerAllowed( void );
// Timer (for tx cycle)
extern void AppPrvLoRaCallbackTimerTxCycle( void );


#endif /* _PRIVATELORA_SAMPLE_H_ */
