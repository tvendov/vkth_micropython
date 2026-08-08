/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacRemoteDev.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef __PRVLORAMACREMOTEDEV_H__
#define __PRVLORAMACREMOTEDEV_H__

#include "PrivateLoRa.h"


/*--------*/
/* define */


/*----------------*/
/* typedef (enum) */


/*------------------------*/
/* typedef (struct/union) */

/*-----------*/
/* Functions */
extern void PrivateLoRaRemoteDevInit( void );

extern PrvLoRaStatus_t PrivateLoRaRemoteDevRegister( uint8_t  *p_remoteAddr, 
                                                     uint8_t  *p_psk,
                                                     uint8_t  *p_sessionKey,
                                                     uint32_t initialFrameCounterTx,
                                                     uint32_t initialFrameCounterRx );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevUnregister( uint8_t *p_remoteAddr );
extern void PrivateLoRaRemoteDevUnregisterAll( void );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevSearchDevice( uint8_t *p_remoteAddr );
extern uint8_t PrivateLoRaRemoteDevGetNumRegistered( void );
extern uint8_t PrivateLoRaRemoteDevGetRegisteredDeviceList( uint8_t *p_remoteList[] );

extern PrvLoRaStatus_t PrivateLoRaRemoteDevGetPSK( uint8_t *p_remoteAddr, uint8_t *p_psk );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevGetSessionKey( uint8_t *p_remoteAddr, uint8_t *p_sessionKey );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevSetSessionKey( uint8_t *p_remoteAddr, uint8_t *p_sessionKey );

extern PrvLoRaStatus_t PrivateLoRaRemoteDevGetFrameCounterTx( uint8_t *p_remoteAddr, uint32_t *p_frameCounterTx );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevSetFrameCounterTx( uint8_t *p_remoteAddr, uint32_t frameCounterTx );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevGetFrameCounterRx( uint8_t *p_remoteAddr, uint32_t *p_frameCounterRx );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevSetFrameCounterRx( uint8_t *p_remoteAddr, uint32_t frameCounterRx );

extern PrvLoRaStatus_t PrivateLoRaRemoteDevGetInitiatorNonce( uint8_t *p_remoteAddr, uint8_t *p_initiatorNonce );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevSetInitiatorNonce( uint8_t *p_remoteAddr, uint8_t *p_initiatorNonce );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevGetResponderNonce( uint8_t *p_remoteAddr, uint8_t *p_responderNonce );
extern PrvLoRaStatus_t PrivateLoRaRemoteDevSetResponderNonce( uint8_t *p_remoteAddr, uint8_t *p_responderNonce );

extern PrvLoRaStatus_t PrivateLoRaRemoteDevGetUpdatedElement( PrvLoRaRemoteDevUpdated_t *p_updtRemoteDev );

#endif  // __PRVLORAMACREMOTEDEV_H__
