/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    privatelora_event.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "board.h"

#include "PrivateLoRa.h"

#include "privatelora_sample.h"

/*--------------------*/
/* function prototype */

static void AppPrvLoRaCallbackMlmeKeyCfm( PrvLoRaMlmeKeyCfm_t *p_keyCfm );
static void AppPrvLoRaCallbackMlmeDevInfoCfm( PrvLoRaMlmeDevInfoCfm_t *p_devInfoCfm );
static void AppPrvLoRaCallbackMlmeTxCycleCfm( PrvLoRaMlmeTxCycleCfm_t *p_txCycleCfm );

static void AppPrvLoRaCallbackMlmeKeyInd( PrvLoRaMlmeKeyInd_t *p_keyInd );
static void AppPrvLoRaCallbackMlmeTxCycleInd( PrvLoRaMlmeTxCycleInd_t *p_txCycleInd );

static void AppPrvLoRaCallbackMacNotifyUpdateRemoteDevice( PrvLoRaNotifyUpdatedRemoteDev_t *p_updtRemoteDev );

//--------------------------------------------------------------------------------------------------
// PrivateLoRa events (MCPS-Confirm)

void AppPrvLoRaCallbackMcpsConfirm( PrvLoRaMcpsCfm_t *p_mcpsCfm )
{
    uint16_t    txHandleMask;

    // Upper 2 bit of txHandle was set when AT+SEND/SENDHEX was executed.
    txHandleMask = APP_PRVLORA_MCPSHANDLE_ATSEND | APP_PRVLORA_MCPSHANDLE_ATSENDHEX;

    if( ( p_mcpsCfm->txHandle & txHandleMask ) != 0 )
    {
        // in case some AT commands are issued before completion of
        // non-blocking AT commands (AT+SEND, AT+SENDHEX)
        if( ( p_mcpsCfm->txHandle & APP_PRVLORA_MCPSHANDLE_ATSEND ) != 0 )
        {
            AtCmdSetCurrentCmd( appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_SEND ] );
        }
        else if( ( p_mcpsCfm->txHandle & APP_PRVLORA_MCPSHANDLE_ATSENDHEX ) != 0 )
        {
            AtCmdSetCurrentCmd( appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_SENDHEX ] );
        }

        switch( p_mcpsCfm->eventStatus )
        {
            case PRVLORA_EVENTINFO_STATUS_OK:
                AppAtOutputResultCode( "OK" );
                break;

            default:
                AppAtPrvLoRaEventResult( p_mcpsCfm->eventStatus );
                break;
        }
    }
    else
    {
        // PrivateLoRaMcpsRequest() was called without executing the AT command
    }
}

//--------------------------------------------------------------------------------------------------
// PrivateLoRa events (MCPS-Indication)

void AppPrvLoRaCallbackMcpsIndication( PrvLoRaMcpsInd_t *p_mcpsInd )
{
    AppPrvLoRaSettings_t    *p_appPrvLoraSettings;
    uint8_t                 msgBuff[ 3 ];
    uint8_t                 i;

    // init
    p_appPrvLoraSettings = &( appPrvLoRaNvmParameters.prvLoraSettings );

    // data
    AtPrintCmdHeader( "+RCVD" );
    print( " " );
    for( i = 0; i < PRVLORA_MACADDR_SIZE; i++ )
    {
        AppAtHexArray2HexStr( msgBuff, &( p_mcpsInd->p_srcMacAddr[ i ] ), 1 );
        print( (char *)msgBuff );
    }
    print( "," );
    print_dec( (long)p_mcpsInd->isSecurity, 1, '\0' );
    print( "," );
    print_dec( (long)p_mcpsInd->isAck, 1, '\0' );
    print( "," );
    print_dec( (long)p_mcpsInd->rxDataSize, 3, '\0' );
    if( p_mcpsInd->rxDataSize > 0 )
    {
        print( "," );
        for( i = 0; i < p_mcpsInd->rxDataSize; i++ )
        {
            AppAtHexArray2HexStr( msgBuff, &( p_mcpsInd->p_rxData[ i ] ), 1 );
            print( (char *)msgBuff );
        }
    }
    AtPrintTrailer();

    // RSSI
    if( p_appPrvLoraSettings->dispRssi == true )
    {
        AtPrintCmdHeader( "+RSSI" );
        print( " " );
        print_dec( p_mcpsInd->rssi, 3, '\0' );
        print( "," );
        print_dec( p_mcpsInd->snr, 3, '\0' );
        AtPrintTrailer();
    }
}

//--------------------------------------------------------------------------------------------------
// PrivateLoRa events (MLME-Confirm)

void AppPrvLoRaCallbackMlmeConfirm( PrvLoRaMlmeCfm_t *p_mlmeCfm )
{
    switch( p_mlmeCfm->mlmeType )
    {
        case PRVLORA_MLME_KEY:
            AppPrvLoRaCallbackMlmeKeyCfm( &( p_mlmeCfm->cfm.keyCfm ) );
            break;

        case PRVLORA_MLME_DEVINFO:
            AppPrvLoRaCallbackMlmeDevInfoCfm( &( p_mlmeCfm->cfm.devInfoCfm ) );
            break;

        case PRVLORA_MLME_TXCYCLE:
            AppPrvLoRaCallbackMlmeTxCycleCfm( &( p_mlmeCfm->cfm.txCycleCfm ) );
            break;

        default:
            break;
    }
}

static void AppPrvLoRaCallbackMlmeKeyCfm( PrvLoRaMlmeKeyCfm_t *p_keyCfm )
{
    // in case some AT commands are issued before completion of
    // non-blocking AT commands (AT+KEYREQ)
    AtCmdSetCurrentCmd( appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_KEYREQ ] );

    AtPrintCmdHeader( "+KEYREQ" );
    print( " " );

    switch( p_keyCfm->status )
    {
        case PRVLORA_EVENTINFO_STATUS_OK:
            print( "KEYREQ_SUCCESS" );
            break;

        default:
            print( "KEYREQ_FAILED" );
            break;
    }
    AtPrintTrailer();
}

static void AppPrvLoRaCallbackMlmeDevInfoCfm( PrvLoRaMlmeDevInfoCfm_t *p_devInfoCfm )
{
    // in case some AT commands are issued before completion of
    // non-blocking AT commands (AT+DEVINFO)
    AtCmdSetCurrentCmd( appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_DEVINFO ] );

    AtPrintCmdHeader( "+DEVINFO" );
    print( " " );

    switch( p_devInfoCfm->status )
    {
        case PRVLORA_EVENTINFO_STATUS_OK:
            print_dec( (long)p_devInfoCfm->txPower, 3, '\0' );
            print( "," );
            print_dec( (long)p_devInfoCfm->snr, 3, '\0' );
            print( "," );
            print_dec( (long)p_devInfoCfm->txCycleTime, 8, '\0' );
            break;

        default:
            print( "ERROR" );
            break;
    }
    AtPrintTrailer();
}

static void AppPrvLoRaCallbackMlmeTxCycleCfm( PrvLoRaMlmeTxCycleCfm_t *p_txCycleCfm )
{
    // in case some AT commands are issued before completion of
    // non-blocking AT commands (AT+DEVINFO)
    AtCmdSetCurrentCmd( appPrvLoRaAtCmdIndex[ APP_PRVLORA_ATCMD_INDEX_TXCYCLE ] );

    AtPrintCmdHeader( "+TXCYCLE" );
    print( " " );

    switch( p_txCycleCfm->status )
    {
        case PRVLORA_EVENTINFO_STATUS_OK:
            print( "OK" );
            break;

        default:
            print( "ERROR" );
            break;
    }
    AtPrintTrailer();
}


//--------------------------------------------------------------------------------------------------
// PrivateLoRa events (MLME-Indication)

void AppPrvLoRaCallbackMlmeIndication( PrvLoRaMlmeInd_t *p_mlmeInd )
{
    switch( p_mlmeInd->mlmeType )
    {
        case PRVLORA_MLME_KEY:
            AppPrvLoRaCallbackMlmeKeyInd( &( p_mlmeInd->ind.keyInd ) );
            break;

        case PRVLORA_MLME_TXCYCLE:
            AppPrvLoRaCallbackMlmeTxCycleInd( &( p_mlmeInd->ind.txCycleInd ) );
            break;

        default:
            break;
    }
}

static void AppPrvLoRaCallbackMlmeKeyInd( PrvLoRaMlmeKeyInd_t *p_keyInd )
{
    uint8_t     msgBuff[ 3 ];
    uint8_t     i;

    AtPrintCmdHeader( "+KEYIND" );
    print( " " );

    for( i = 0; i < PRVLORA_MACADDR_SIZE; i++ )
    {
        AppAtHexArray2HexStr( msgBuff, &( p_keyInd->srcMacAddr[ i ] ), 1 );
        print( (char *)msgBuff );
    }
    AtPrintTrailer();
}

static void AppPrvLoRaCallbackMlmeTxCycleInd( PrvLoRaMlmeTxCycleInd_t *p_txCycleInd )
{
    uint8_t     msgBuff[ 3 ];
    uint8_t     i;

    AtPrintCmdHeader( "+TXCYCLEIND" );
    print( " " );

    for( i = 0; i < PRVLORA_MACADDR_SIZE; i++ )
    {
        AppAtHexArray2HexStr( msgBuff, &( p_txCycleInd->srcMacAddr[ i ] ), 1 );
        print( (char *)msgBuff );
    }
    print( "," );
    print_dec( (long)p_txCycleInd->isSecurity, 1, '\0' );
    print( "," );
    print_dec( p_txCycleInd->txCycleTime, 6, '\0' );
    AtPrintTrailer();

    // for tx cycle
    AppPrvLoRaTxCycleSetParameter( p_txCycleInd->srcMacAddr, p_txCycleInd->txCycleTime );
}

//--------------------------------------------------------------------------------------------------
// PrivateLoRa events (Notification)

void AppPrvLoRaCallbackMacNotification( PrvLoRaNotification_t *p_notify )
{
    switch( p_notify->notifyType )
    {
        case PRVLORA_NOTIFY_UPDATE_REMOTEDEV:
            AppPrvLoRaCallbackMacNotifyUpdateRemoteDevice( &( p_notify->nty.updtRemoteDevNty ) );
            break;
        default:
            break;
    }
}

static void AppPrvLoRaCallbackMacNotifyUpdateRemoteDevice( PrvLoRaNotifyUpdatedRemoteDev_t *p_updtRemoteDev )
{
    if( ( p_updtRemoteDev->updatedParams & PRVLORA_REMOTEDEV_UPDATED_SESSION_KEY ) != 0 )
    {
        AppPrvLoRaRmtDevInfoUpdateSessionKey( p_updtRemoteDev->devAddress, 
                                              p_updtRemoteDev->secSessionKey );
    }

    if( ( p_updtRemoteDev->updatedParams & PRVLORA_REMOTEDEV_UPDATED_FCNT_TX ) != 0 )
    {
        AppPrvLoRaRmtDevInfoUpdateFrameCounterTx( p_updtRemoteDev->devAddress, 
                                                  p_updtRemoteDev->frameCounterTx );
    }

    if( ( p_updtRemoteDev->updatedParams & PRVLORA_REMOTEDEV_UPDATED_FCNT_RX ) != 0 )
    {
        AppPrvLoRaRmtDevInfoUpdateFrameCounterRx( p_updtRemoteDev->devAddress, 
                                                  p_updtRemoteDev->frameCounterRx );
    }

#ifdef DEBUG_PRVLORA
    AppPrvLoRaDebugDispMacNotifyRemoteDeviceInfo( p_updtRemoteDev );
#endif
}

//--------------------------------------------------------------------------------------------------
// MCU/board events
bool AppPrvLoRaCallbackBoardIsLowPowerAllowed( void )
{
    /* return true if users want MCU to low power, otherwise false */
    bool retLowPwrAllowed;

    // init
    retLowPwrAllowed = true;

#ifdef DEBUG_PRVLORA
    if( AppPrvLoRaDebugIsPseudoLowPowerAllowed() == false )
    {
        retLowPwrAllowed = false;
    }
#endif

    /* at-command (UART) */
    if( AtGetStateIsCommandReceived() == true )
    {
        retLowPwrAllowed = false;
    }

    return retLowPwrAllowed;
}

//--------------------------------------------------------------------------------------------------
// Timer (for tx cycle)
void AppPrvLoRaCallbackTimerTxCycle( void )
{
    AppPrvLoRaTxCycleMng_t  *p_txCycleMng;

    // init
    p_txCycleMng = &( appPrvLoRaNvmParameters.txCycleMng );

    p_txCycleMng->isStart = true;
}
