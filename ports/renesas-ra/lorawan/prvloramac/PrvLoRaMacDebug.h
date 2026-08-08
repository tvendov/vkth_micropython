/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacDebug.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/
#ifdef DEBUG_PRVLORA

#ifndef __PRVLORAMACDEBUG_H__
#define __PRVLORAMACDEBUG_H__

/*--------*/
/* define */

// PrivateLoRa debug Mode
#define PRVLORA_DEBUGMODE_MAC_DISP_RXFRAME      0x80000000
#define PRVLORA_DEBUGMODE_MAC_DISP_RADIOEVT     0x40000000
#define PRVLORA_DEBUGMODE_MAC_DISP_RADIOPRM_RX  0x08000000
#define PRVLORA_DEBUGMODE_MAC_DISP_RADIOPRM_TX  0x04000000

// PrivateLoRa debug On/Off
#define PRVLORA_DEBUGSWITCH_OFF   false
#define PRVLORA_DEBUGSWITCH_ON    true

/*-----------*/
/* Functions */

// PrivateLoRa debug mode
extern void PrivateLoRaDebugInit( void );
extern void PrivateLoRaDebugSetMode( uint32_t debugMode );
extern uint32_t PrivateLoRaDebugGetMode( void );

// PrivateLoRa debug On/Off
extern void PrivateLoRaDebugSetOnOff( bool debugOn );
extern bool PrivateLoRaDebugGetOnOff( void );

// PrivateLoRa debug function
extern void PrivateLoRaDebugDispRxFrame( void    *vp_rxFrameCtrl,
                                         uint8_t *p_rxSrcAddr,
                                         uint8_t *p_rxDstAddr,
                                         uint8_t *p_rxPayload,
                                         uint8_t rxPayloadSize );
extern void PrivateLoRaDebugDispRadioEvent( void *vp_radioEvents );
extern void PrivateLoRaDebugDispRadioTxParams( void *vp_radioTxParam );
extern void PrivateLoRaDebugDispRadioRxParams( void *vp_radioRxParam, uint32_t maxRxWindow );


#endif  // __PRVLORAMACDEBUG_H__

#endif  // DEBUG_PRVLORA
