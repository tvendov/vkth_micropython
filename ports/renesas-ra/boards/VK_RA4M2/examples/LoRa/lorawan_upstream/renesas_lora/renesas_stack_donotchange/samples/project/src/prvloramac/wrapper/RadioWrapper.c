/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    RadioWrapper.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "board.h"

#include "radio.h"
#include "radiowrapper.h"

/*------------------------*/
/* typedef (struct/union) */
typedef struct _RadioWrapperManage_t
{
    RadioEvents_t       radioEvents[ MAXNUM_RADIOWRAP_LORAMODE ];
    RadioEvents_t       *p_radioEvents[ MAXNUM_RADIOWRAP_LORAMODE ];
    RadioEvents_t       *p_currentRadioEvents;
    RadioWrapLoRaMode_t loraMode;
} RadioWrapperManage_t;

/*-----------------*/
/* global variable */
RadioWrapperManage_t    RadioWrapMng = { 0 };
RadioEvents_t           RadioWrapEvents;

/*--------------------*/
/* function prototype */
static void RadioWrapOnRadioTxDone( void );
static void RadioWrapOnRadioRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );
static void RadioWrapOnRadioTxTimeout( void );
static void RadioWrapOnRadioRxError( void );
static void RadioWrapOnRadioRxTimeout( void );

//--------------------------------------------------------------------------------------------------

/*!
 * Wrap Radio.Init()
 */
RadioResult_t RadioWrapperInit( RadioWrapLoRaMode_t loraMode, RadioEvents_t *p_events )
{
    RadioResult_t   ret;

    // initial check
    if( ( loraMode >= MAXNUM_RADIOWRAP_LORAMODE ) || ( p_events == NULL ) )
    {
        return RADIO_ARG_IS_INVALID;
    }

    // initialize radio
    RadioWrapEvents.TxDone    = RadioWrapOnRadioTxDone;
    RadioWrapEvents.RxDone    = RadioWrapOnRadioRxDone;
    RadioWrapEvents.RxError   = RadioWrapOnRadioRxError;
    RadioWrapEvents.TxTimeout = RadioWrapOnRadioTxTimeout;
    RadioWrapEvents.RxTimeout = RadioWrapOnRadioRxTimeout;

    ret = Radio.Init( &RadioWrapEvents );
    if( ret == RADIO_SUCCESS )
    {
        memcpy1( (uint8_t *)&( RadioWrapMng.radioEvents[ loraMode ] ), 
                 (const uint8_t *)p_events, 
                 sizeof(RadioEvents_t) );
        RadioWrapMng.p_radioEvents[ loraMode ] = &( RadioWrapMng.radioEvents[ loraMode ] );
        RadioWrapperSetLoRaMode( loraMode );
    }

    return ret;
}

/*!
 * Set LoRa mode
 */
RadioResult_t RadioWrapperSetLoRaMode( RadioWrapLoRaMode_t loraMode )
{
    RadioResult_t   ret;

    // initial check
    if( loraMode >= MAXNUM_RADIOWRAP_LORAMODE )
    {
        return RADIO_ARG_IS_INVALID;
    }

    // init
    ret = RADIO_FAIL;  // init = specific loraMode is not initialized

    // set lora mode
    if( RadioWrapMng.p_radioEvents[ loraMode ] != NULL )
    {
        RadioWrapMng.p_currentRadioEvents = RadioWrapMng.p_radioEvents[ loraMode ];
        RadioWrapMng.loraMode = loraMode;
        ret = RADIO_SUCCESS;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------

/*!
 * Wrap radio event callback; TxDone
 */
static void RadioWrapOnRadioTxDone( void )
{
    if( ( RadioWrapMng.p_currentRadioEvents != NULL ) &&
        ( RadioWrapMng.p_currentRadioEvents->TxDone != NULL ) )
    {
        RadioWrapMng.p_currentRadioEvents->TxDone();
    }
}

/*!
 * Wrap radio event callback; RxDone
 */
static void RadioWrapOnRadioRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    if( ( RadioWrapMng.p_currentRadioEvents != NULL ) &&
        ( RadioWrapMng.p_currentRadioEvents->RxDone != NULL ) )
    {
        RadioWrapMng.p_currentRadioEvents->RxDone( payload, size, rssi, snr );
    }
}

/*!
 * Wrap radio event callback; TxTimeout
 */
static void RadioWrapOnRadioTxTimeout( void )
{
    if( ( RadioWrapMng.p_currentRadioEvents != NULL ) &&
        ( RadioWrapMng.p_currentRadioEvents->TxTimeout != NULL ) )
    {
        RadioWrapMng.p_currentRadioEvents->TxTimeout();
    }
}

/*!
 * Wrap radio event callback; RxError
 */
static void RadioWrapOnRadioRxError( void )
{
    if( ( RadioWrapMng.p_currentRadioEvents != NULL ) &&
        ( RadioWrapMng.p_currentRadioEvents->RxError != NULL ) )
    {
        RadioWrapMng.p_currentRadioEvents->RxError();
    }
}

/*!
 * Wrap radio event callback; RxTimeout
 */
static void RadioWrapOnRadioRxTimeout( void )
{
    if( ( RadioWrapMng.p_currentRadioEvents != NULL ) &&
        ( RadioWrapMng.p_currentRadioEvents->RxTimeout != NULL ) )
    {
        RadioWrapMng.p_currentRadioEvents->RxTimeout();
    }
}
