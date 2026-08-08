/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_debug.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifdef DEBUG_PRVLORA


#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "board.h"

#include "PrivateLoRa.h"

#include "privatelora_sample.h"
#include "privatelora_debug.h"

/*--------*/
/* define */
#define APP_PRVLORA_DEBUGMODE_MASK      ( APP_PRVLORA_DEBUG_APP_PSEUDO_MCULOWPWR | \
                                          APP_PRVLORA_DEBUG_APP_DISP_TXCYCLE | \
                                          APP_PRVLORA_DEBUG_APP_DISP_MACNOTIFY_RMTDEV )

/*-----------------*/
/* global variable */
uint32_t    appPrvLoraDebugMode;

/*--------------------*/
/* function prototype */

//--------------------------------------------------------------------------------------------------
// Debug mode

/*!
 * Init debug mode
 */
void AppPrvLoRaDebugInit( void )
{
    // init mode
    appPrvLoraDebugMode = 0;

#ifdef DEBUG_PRVLORA_DEFAULT_MODE
    AppPrvLoRaDebugSetMode( DEBUG_PRVLORA_DEFAULT_MODE );
#endif
}

/*!
 * Set debug mode
 */
void AppPrvLoRaDebugSetMode( uint32_t debugMode )
{
    appPrvLoraDebugMode = debugMode & APP_PRVLORA_DEBUGMODE_MASK;
    PrivateLoRaDebugSetMode( debugMode );
}

/*!
 * Get debug mode
 */
uint32_t AppPrvLoRaDebugGetMode( void )
{
    uint32_t    debugMode;

    debugMode  = appPrvLoraDebugMode;
    debugMode |= PrivateLoRaDebugGetMode();

    return debugMode;
}

//--------------------------------------------------------------------------------------------------
// Debug function

/*!
 * APP_PRVLORA_DEBUG_APP_PSEUDO_MCULOWPWR - Is allowed pseudo low power
 */
bool AppPrvLoRaDebugIsPseudoLowPowerAllowed( void )
{
    bool        retLowPwrAllowed;

    // init - no trigger to wake up from MCU low power for debug
    retLowPwrAllowed = true;

    return retLowPwrAllowed;
}

/*!
 * APP_PRVLORA_DEBUG_APP_PSEUDO_MCULOWPWR - Set pseudo low power
 */
int16_t AppPrvLoRaDebugSetPseudoLowPower( void )
{
    int16_t     procResult;
    uint32_t    _boardMcuWokeUpBy_prev;

    // init
    procResult = SUCCESS;

    BoardDisableAllIrq();
    
    if( RP_LOWPWR_COND_ENTRY )  // Do not enter STOP mode if there's no timer running
    {
        SetLowPowerFlag( WAKEUP_TRIGGER_READY );

        do
        {
            // for pseudo-sleep MCU
            _boardMcuWokeUpBy_prev = BoardMcuWokeUpBy;

            BoardEnableAllIrq( );
            while (1)
            {
                // loop until interrup
                if ((BoardMcuWokeUpBy != _boardMcuWokeUpBy_prev) ||
                    (BoardIsLowPowerAllowed() == false))
                {
                    break;
                }
            }
            BoardDisableAllIrq( );

            // Enter STOP mode again if MCU wakes up from adjustment interrupt by RTC or TRJ0.
        } while( RP_LOWPWR_COND_LOOP );
        
        ClearLowPowerFlag();
    }
    
    BoardEnableAllIrq();
    
    return procResult;      // Only returns SUCCESS for now
}


/*
 * APP_PRVLORA_DEBUG_APP_DISP_TXCYCLE - display tx cycle info
 */
void AppPrvLoRaDebugDispTxCycle( PrvLoRaStatus_t status )
{
    uint32_t                debugMode;
    AppPrvLoRaTxCycleMng_t  *p_txCycleMng;
    uint8_t                 i;

    // init
    p_txCycleMng = &( appPrvLoRaNvmParameters.txCycleMng );

    debugMode  = AppPrvLoRaDebugGetMode();
    debugMode &= APP_PRVLORA_DEBUG_APP_DISP_TXCYCLE;
    if( debugMode != 0 )
    {
        print( "[DBG][TxCycle]" );
        print_newline();
        print( " - Status   = " );
        print_dec( status, 3, '\0' );
        print_newline();
        print( " - Period   = " );
        print_dec( p_txCycleMng->period, 10, '\0' );
        print_newline();
        print( " - DstAddr  = " );
        for( i = 0; i < APP_PRVLORA_LEN_MACADDR; i++ )
        {
            print_hex( p_txCycleMng->dstAddr[ i ], 2 );
        }
        print_newline();
        print( " - DataSize = " );
        print_dec( p_txCycleMng->txDataLen, 3, '\0' );
        print( " Byte" );
        print_newline();
        print( " - Data     = " );
        for( i = 0; i < p_txCycleMng->txDataLen; i++ )
        {
            print_hex( p_txCycleMng->txData[ i ], 2 );
        }
        print_newline();
    }
}

/*
 * APP_PRVLORA_DEBUG_APP_DISP_MACNOTIFY_RMTDEV - MAC notify - display remote device info
 */
void AppPrvLoRaDebugDispMacNotifyRemoteDeviceInfo( PrvLoRaNotifyUpdatedRemoteDev_t *p_updtRemoteDev )
{
    uint32_t        debugMode;
    uint8_t         i;
    const uint8_t   updtLabel[] = "<updated>";

    debugMode  = AppPrvLoRaDebugGetMode();
    debugMode &= APP_PRVLORA_DEBUG_APP_DISP_MACNOTIFY_RMTDEV;
    if( debugMode != 0 )
    {
        print( "[DBG][MacNotiry][RmtDev]" );
        print_newline();
        print( " - DevAddress = " );
        for( i = 0; i < APP_PRVLORA_LEN_MACADDR; i++ )
        {
            print_hex( p_updtRemoteDev->devAddress[ i ], 2 );
        }
        print_newline();
        print( " - PSK        = " );
        for( i = 0; i < APP_PRVLORA_LEN_SECKEY; i++ )
        {
            print_hex( p_updtRemoteDev->secPsk[ i ], 2 );
        }
        print_newline();
        print( " - SessionKey = " );
        for( i = 0; i < APP_PRVLORA_LEN_SECKEY; i++ )
        {
            print_hex( p_updtRemoteDev->secSessionKey[ i ], 2 );
        }
        if( ( p_updtRemoteDev->updatedParams & PRVLORA_REMOTEDEV_UPDATED_SESSION_KEY ) != 0 )
        {
            print( " " );
            print( (char *)updtLabel );
        }
        print_newline();
        print( " - fCountTx   = 0x" );
        print_hex( p_updtRemoteDev->frameCounterTx, 8 );
        if( ( p_updtRemoteDev->updatedParams & PRVLORA_REMOTEDEV_UPDATED_FCNT_TX ) != 0 )
        {
            print( " " );
            print( (char *)updtLabel );
        }
        print_newline();
        print( " - fCountRx   = 0x" );
        print_hex( p_updtRemoteDev->frameCounterRx, 8 );
        if( ( p_updtRemoteDev->updatedParams & PRVLORA_REMOTEDEV_UPDATED_FCNT_RX ) != 0 )
        {
            print( " " );
            print( (char *)updtLabel );
        }
        print_newline();
    }
}

#endif  // DEBUG_PRVLORA
