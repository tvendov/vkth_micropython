/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacDebug.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifdef DEBUG_PRVLORA


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "board.h"

#include "PrivateLoRa.h"
#include "PrvLoRaMacFrame.h"
#include "PrvLoRaMacRadio.h"
#include "PrvLoRaMacDebug.h"


/*--------*/
/* define */
#define PRVLORA_DEBUGMODE_MASK      ( PRVLORA_DEBUGMODE_MAC_DISP_RXFRAME     |\
                                      PRVLORA_DEBUGMODE_MAC_DISP_RADIOEVT    |\
                                      PRVLORA_DEBUGMODE_MAC_DISP_RADIOPRM_RX |\
                                      PRVLORA_DEBUGMODE_MAC_DISP_RADIOPRM_TX )

/*------------------------*/
/* typedef (struct/union) */

typedef struct _PrvLoRaDebugMng_t
{
    bool        debugInitialized;
    bool        debugOn;
    uint32_t    debugMode;
} PrvLoRaDebugMng_t;

/*-----------------*/
/* global variable */
PrvLoRaDebugMng_t   PrvLoRaMacDebugMng = { .debugInitialized = false };

/*--------------------*/
/* function prototype */


//--------------------------------------------------------------------------------------------------
// PrivateLoRa debug mode

/*!
 * Init PrivateLoRa MAC debug mode
 */
void PrivateLoRaDebugInit( void )
{
    if( PrvLoRaMacDebugMng.debugInitialized == false )
    {
        memset1( (uint8_t *)&PrvLoRaMacDebugMng, 0x00, sizeof(PrvLoRaDebugMng_t) );
#ifdef DEBUG_PRVLORA_DEFAULT_MODE
        PrivateLoRaDebugSetMode( DEBUG_PRVLORA_DEFAULT_MODE );
#endif

        PrvLoRaMacDebugMng.debugInitialized = true;
    }
}

/*!
 * Set PrivateLoRa MAC debug mode
 */
void PrivateLoRaDebugSetMode( uint32_t debugMode )
{
    PrvLoRaMacDebugMng.debugMode = debugMode & PRVLORA_DEBUGMODE_MASK;
}

/*!
 * Get PrivateLoRa MAC debug mode
 */
uint32_t PrivateLoRaDebugGetMode( void )
{
    return PrvLoRaMacDebugMng.debugMode;
}

//--------------------------------------------------------------------------------------------------
// PrivateLoRa debug On/Off

/*!
 * Set PrivateLoRa MAC debug On/Off
 */
void PrivateLoRaDebugSetOnOff( bool debugOn )
{
    PrvLoRaMacDebugMng.debugOn = debugOn;
}

/*!
 * Get PrivateLoRa MAC debug On/Off
 */
bool PrivateLoRaDebugGetOnOff( void )
{
    return PrvLoRaMacDebugMng.debugOn;
}

//--------------------------------------------------------------------------------------------------
// PrivateLoRa debug function

/*!
 * Display RX frame (PRVLORA_DEBUGMODE_MAC_DISP_RXFRAME)
 */
void PrivateLoRaDebugDispRxFrame( void    *vp_rxFrameCtrl,
                                  uint8_t *p_rxSrcAddr,
                                  uint8_t *p_rxDstAddr,
                                  uint8_t *p_rxPayload,
                                  uint8_t rxPayloadSize )
{
    PrvLoRaFrameMhdrFrmCtrl_t   *p_rxFrameCtrl;
    uint32_t                    debugMode;
    uint8_t                     i, frmCtrl;

    // initial check (debug On/Off)
    if( PrivateLoRaDebugGetOnOff() == PRVLORA_DEBUGSWITCH_OFF )
    {
        return;
    }

    // init
    p_rxFrameCtrl = (PrvLoRaFrameMhdrFrmCtrl_t *)vp_rxFrameCtrl;

    debugMode  = PrivateLoRaDebugGetMode();
    debugMode &= PRVLORA_DEBUGMODE_MAC_DISP_RXFRAME;

    if( debugMode != 0 )
    {
        memcpy1( &frmCtrl, (uint8_t *)p_rxFrameCtrl, sizeof(uint8_t) );
        print( "[DBG][RX]" );
        print_newline();
        print( " - FrameCtrl   = 0x" );
        print_hex( frmCtrl, 2 );
        print( " : " );
        print( "FrameType=0x" );
        print_hex( p_rxFrameCtrl->frameType, 1 );
        print( ", " );
        print( "Ack=" );
        print_dec( p_rxFrameCtrl->ack, 1, '\0' );
        print( ", " );
        print( "AR=" );
        print_dec( p_rxFrameCtrl->ackRequest, 1, '\0' );
        print( ", " );
        print( "Sec=" );
        print_dec( p_rxFrameCtrl->secEnabled, 1, '\0' );
        print_newline();

        print( " - SrcAddr     = " );
        for( i = 0; i < PRVLORA_MACADDR_SIZE; i++ )
        {
            print_hex( p_rxSrcAddr[ i ], 2 );
        }
        print_newline();

        print( " - DstAddr     = " );
        for( i = 0; i < PRVLORA_MACADDR_SIZE; i++ )
        {
            print_hex( p_rxDstAddr[ i ], 2 );
        }
        print_newline();

        print( " - PayloadSize = " );
        print_dec( rxPayloadSize, 3, '\0' );
        print( " Byte" );
        print_newline();

        print( " - Payload     = " );
        for( i = 0; i < rxPayloadSize; i++ )
        {
            print_hex( p_rxPayload[ i ], 2 );
        }
        print_newline();
        print_newline();
    }
}

/*!
 * Display radio event (PRVLORA_DEBUGMODE_MAC_DISP_RADIOEVT)
 */
void PrivateLoRaDebugDispRadioEvent( void *vp_radioEvents )
{
    PrvLoRaRadioEvents_t    *p_radioEvents;
    uint32_t                debugMode;

    // initial check (debug On/Off)
    if( PrivateLoRaDebugGetOnOff() == PRVLORA_DEBUGSWITCH_OFF )
    {
        return;
    }

    // init
    p_radioEvents = (PrvLoRaRadioEvents_t *)vp_radioEvents;

    debugMode  = PrivateLoRaDebugGetMode();
    debugMode &= PRVLORA_DEBUGMODE_MAC_DISP_RADIOEVT;

    if( debugMode != 0 )
    {
        if( p_radioEvents->radioEvent.evtValue != 0 )
        {
            print( "[DBG][RadioEvt]" );

            if( p_radioEvents->radioEvent.events.TxDone != 0 )
            {
                print( " - TxDone " );
                print_newline();
            }
            if( p_radioEvents->radioEvent.events.TxTimeout != 0 )
            {
                print( " - TxTimeout " );
                print_newline();
            }
            if( p_radioEvents->radioEvent.events.RxDone != 0 )
            {
                print( " - RxDone " );
                print_newline();
            }
            if( p_radioEvents->radioEvent.events.RxError != 0 )
            {
                print( " - RxError " );
                print_newline();
            }
            if( p_radioEvents->radioEvent.events.RxTimeout != 0 )
            {
                print( " - RxTimeout " );
                print_newline();
            }
            print_newline();
        }
    }
}

/*!
 * Display Radio parameter (PRVLORA_DEBUGMODE_MAC_DISP_RADIOPRM_TX) - Tx
 */
void PrivateLoRaDebugDispRadioTxParams( void *vp_radioTxParam )
{
    PrvLoRaRadioTxParams_t  *p_radioTxParam;
    uint32_t                debugMode;

    // initial check (debug On/Off)
    if( PrivateLoRaDebugGetOnOff() == PRVLORA_DEBUGSWITCH_OFF )
    {
        return;
    }

    // init
    p_radioTxParam = (PrvLoRaRadioTxParams_t *)vp_radioTxParam;

    debugMode  = PrivateLoRaDebugGetMode();
    debugMode &= PRVLORA_DEBUGMODE_MAC_DISP_RADIOPRM_TX;

    if( debugMode != 0 )
    {
        print( "[DBG][RadioTx]" );
        print_newline();
        print( " - Modem        = " );
        if( p_radioTxParam->modem == PRVLORA_MODEM_LORA )
        {
            print( "LoRa" );
            print_newline();
        }
        else if( p_radioTxParam->modem == PRVLORA_MODEM_FSK )
        {
            print( "FSK" );
            print_newline();
        }
        else
        {
            print( "???" );
            print_newline();
        }
        print( " - Frequency    = " );
        print_dec( p_radioTxParam->frequency, 10, '\0' );
        print_newline();
        print( " - DataRate     = " );
        print_dec( (uint32_t)p_radioTxParam->dataRate, 10, '\0' );
        print_newline();
        print( " - BandWidth    = " );
        print_dec( p_radioTxParam->bandWidth, 10, '\0' );
        print_newline();
        print( " - MaxFrameSize = " );
        print_dec( (uint32_t)p_radioTxParam->maxFrameSize, 10, '\0' );
        print_newline();
        print( " - txPower      = " );
        print_dec( (int32_t)p_radioTxParam->txPower, 10, '\0' );
        print_newline();
        // p_radioTxParam->txTimeout
    }
}

/*!
 * Display Radio parameter (PRVLORA_DEBUGMODE_MAC_DISP_RADIOPRM_RX) - Rx
 */
void PrivateLoRaDebugDispRadioRxParams( void *vp_radioRxParam, uint32_t maxRxWindow )
{
    PrvLoRaRadioRxParams_t  *p_radioRxParam;
    uint32_t                debugMode;

    // initial check (debug On/Off)
    if( PrivateLoRaDebugGetOnOff() == PRVLORA_DEBUGSWITCH_OFF )
    {
        return;
    }

    // init
    p_radioRxParam = (PrvLoRaRadioRxParams_t *)vp_radioRxParam;

    debugMode  = PrivateLoRaDebugGetMode();
    debugMode &= PRVLORA_DEBUGMODE_MAC_DISP_RADIOPRM_RX;

    if( debugMode != 0 )
    {
        print( "[DBG][RadioRx]" );
        print_newline();
        print( " - Modem        = " );
        if( p_radioRxParam->modem == PRVLORA_MODEM_LORA )
        {
            print( "LoRa" );
            print_newline();
        }
        else if( p_radioRxParam->modem == PRVLORA_MODEM_FSK )
        {
            print( "FSK" );
            print_newline();
        }
        else
        {
            print( "???" );
            print_newline();
        }
        print( " - Frequency    = " );
        print_dec( p_radioRxParam->frequency, 10, '\0' );
        print_newline(); 
        print( " - DataRate     = " );
        print_dec( (uint32_t)p_radioRxParam->dataRate, 10, '\0' );
        print_newline(); 
        print( " - BandWidth    = " );
        print_dec( p_radioRxParam->bandWidth, 10, '\0' );
        print_newline(); 
        print( " - MaxFrameSize = " );
        print_dec( (uint32_t)p_radioRxParam->maxFrameSize, 10, '\0' );
        print_newline(); 
        print( " - MaxRxWindow  = " );
        print_dec( (uint32_t)maxRxWindow, 10, '\0' );
        print_newline(); 
        // p_radioRxParam->maxRxWindow
        // p_radioRxParam->minRxSymbols
        // p_radioRxParam->rxError
        // p_radioRxParam->windowTimeout
        // p_radioRxParam->windowOffset
    }
}

#endif  // DEBUG_PRVLORA
