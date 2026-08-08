/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacIndirectTx.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef __PRVLORAMACINDIRECTTX_H__
#define __PRVLORAMACINDIRECTTX_H__

#include "PrivateLoRa.h"

/*--------*/
/* define */

/*----------------*/
/* typedef (enum) */

/*------------------------*/
/* typedef (struct/union) */

/*-----------*/
/* Functions */
extern PrvLoRaStatus_t PrivateLoRaIndTxInit( void );

extern PrvLoRaStatus_t PrivateLoRaIndTxRegisterRemoteDevice( uint8_t *p_remoteAddr );
extern PrvLoRaStatus_t PrivateLoRaIndTxUnregisterRemoteDevice( uint8_t *p_remoteAddr );

extern PrvLoRaStatus_t PrivateLoRaIndTxEnqueueMcpsReq( PrvLoRaMcpsReq_t *p_mcpsReq );
extern PrvLoRaStatus_t PrivateLoRaIndTxEnqueueMlmeReq( PrvLoRaMlmeReq_t *p_mlmeReq );
extern PrvLoRaStatus_t PrivateLoRaIndTxDequeueAndSend( uint8_t *p_remoteAddr );

#endif  // __PRVLORAMACINDIRECTTX_H__
